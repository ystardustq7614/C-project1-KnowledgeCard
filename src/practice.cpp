// 引入主动练习模块头文件：练习菜单和两类练习入口声明都在这里。
#include "practice.h"

// 引入全局状态：cards、wrongs、currentUserId 等来自这里。
#include "globals.h"

// 引入通用工具：trim、parseInt、暂停/清屏等来自这里。
#include "utils.h"

// 引入薄弱章节推荐算法，用于专项突破练习选择目标章节。
#include "algo_recommend.h"

// iostream 提供 cin/cout，用于控制台练习交互。
#include <iostream>

// vector 用于保存卡片下标池和推荐输入。
#include <vector>

// algorithm 提供 shuffle，用于随机打乱题目顺序。
#include <algorithm>

// random 提供 default_random_engine。
#include <random>

// chrono 提供时间种子，用于轻量随机抽题。
#include <chrono>

// 只引入本文件实际使用的标准库名字，避免命名空间整体引入污染。
namespace chrono = std::chrono;
using std::cin;
using std::cout;
using std::default_random_engine;
using std::getline;
using std::shuffle;
using std::string;
using std::vector;

/*
[导读]
- 本文件提供主动练习流程，和 review.cpp 的“正式复习计划”刻意分离。

[对应流程图]
- 随机练习：当前用户有效 Card -> 随机抽样 -> 用户自评 -> 会话内正确率。
- 薄弱点练习：Card/WrongQuestion -> algo_recommend -> 目标章节 Card -> 会话内正确率。

[输入输出]
- 输入：当前用户有效卡片/错题、控制台答题反馈。
- 输出：本次练习统计；不写 ReviewLog，不修改 mastery/nextReviewDate。

[易错点]
- 自测练习不能改变正式复习计划，否则“随手练习”和“今日复习”的语义会混在一起。
- 专项练习用错题参与推荐，但实际出题来源只取知识卡片；错题要先转卡片才会出题。
- 练习正确率只是本次会话统计，不写入 review_logs.txt。

[实验]
- 把专项练习最多 15 张改成更小的数，观察长章节练习时的会话长度变化。
- 把随机练习默认 10 张改成 5 张，观察空输入时的抽题数量变化。
*/

// 返回 cards 容器下标，不是 cardId；调用方只在本次函数内使用，避免跨修改缓存。
static vector<int> getActiveCardIndices() {
    // 返回当前用户有效卡片的 cards 容器下标列表。
    vector<int> res;

    // 扫描所有卡片。
    for (size_t i = 0; i < cards.size(); ++i) {
        // 只收集当前用户 active=true 的卡片。
        if (cards[i].userId == currentUserId && cards[i].active) {
            res.push_back(i);
        }
    }

    // 返回下标池，调用方只在当前函数内使用，不跨操作缓存。
    return res;
}

void startRandomPractice() {
    // 进入随机抽查测试页面。
    clearScreen();
    cout << "==============================\n";
    cout << "       随机抽查测试\n";
    cout << "==============================\n";

    // 获取当前用户所有可练习卡片下标。
    vector<int> pool = getActiveCardIndices();

    // 没有卡片时无法练习。
    if (pool.empty()) {
        cout << "当前没有可用卡片，无法进行练习。\n";
        pauseScreen();
        return;
    }

    // 告知用户当前可抽题范围。
    cout << "当前可用知识卡片总数：" << pool.size() << "\n";

    // 读取用户希望抽取的数量。
    cout << "请输入要抽取的卡片数量（直接回车默认 10 张）：";
    string line;
    getline(cin, line);
    line = trim(line);
    
    // 默认抽 10 张。
    int count = 10;

    // 用户输入非空时尝试解析。
    if (!line.empty()) {
        // 非正整数无效，回退默认 10。
        if (!parseInt(line, count) || count <= 0) {
            cout << "输入无效，系统将使用默认值 10。\n";
            count = 10;
        }
    }
    
    // 不能抽超过现有卡片数。
    if (count > static_cast<int>(pool.size())) {
        count = pool.size();
    }

    // 使用时间种子仅满足轻量随机抽查，不用于可复现实验；测试不依赖抽题顺序。
    unsigned seed = chrono::system_clock::now().time_since_epoch().count();

    // 打乱卡片下标池，后面取前 count 个。
    shuffle(pool.begin(), pool.end(), default_random_engine(seed));

    // 统计本次会话中用户自评答对的数量。
    int correctCount = 0;

    // 逐题练习。
    for (int i = 0; i < count; ++i) {
        // 每道题单独清屏展示。
        clearScreen();
        cout << "==============================\n";
        cout << " 随机自测 (" << (i+1) << "/" << count << ")\n";
        cout << "==============================\n";
        
        // 通过下标取出本题卡片。
        const Card& c = cards[pool[i]];

        // 展示题目所属学科和章节。
        cout << "【" << c.subject << " - " << c.chapter << "】\n\n";

        // 展示卡片正面。
        cout << "题目：\n" << c.front << "\n\n";
        
        // 等待用户思考完成。
        cout << "(按回车键查看答案...)\n";
        getline(cin, line);
        
        // 展示卡片背面答案。
        cout << "------------------------------\n";
        cout << "答案：\n" << c.back << "\n";
        cout << "------------------------------\n\n";
        
        // 练习只做自评，不进入 SM2 算法。
        cout << "你答对了吗？(y/n，直接回车默认算对)：";
        getline(cin, line);
        line = trim(line);

        // 只有明确输入 n/N 才算错。
        if (line == "n" || line == "N") {
            // 自测不扣分，避免用户因为主动练习影响正式复习计划。
        } else {
            // 默认和其他输入都算答对。
            correctCount++;
        }
    }

    // 输出本次随机练习总结。
    clearScreen();
    cout << "==============================\n";
    cout << "       自测练习结束\n";
    cout << "==============================\n";

    // 输出完成数量和正确率。
    cout << "共完成 " << count << " 道题。\n";
    cout << "答对 " << correctCount << " 道，正确率：" << (count > 0 ? (correctCount * 100 / count) : 0) << "%\n\n";

    // 明确提醒：本练习不影响正式计划。
    cout << "注：本次自测属于无压练习，不影响日常复习计划与系统掌握度。\n";
    pauseScreen();
}

void startWeaknessPractice() {
    // 进入薄弱点专项突破练习页面。
    clearScreen();
    cout << "==============================\n";
    cout << "     薄弱点专项突破练习\n";
    cout << "==============================\n";

    // 推荐算法输入只需要 subject、chapter、mastery。
    vector<RecommendInputItem> inputs;

    // 第一段：当前用户有效卡片参与薄弱章节计算。
    for (const Card& c : cards) {
        // 跳过其他用户和逻辑删除卡片。
        if (c.userId != currentUserId || !c.active) continue;

        // 加入推荐输入。
        inputs.push_back({c.subject, c.chapter, c.mastery});
    }

    // 第二段：当前用户有效错题也参与薄弱章节计算。
    for (const WrongQuestion& w : wrongs) {
        // 跳过其他用户和逻辑删除错题。
        if (w.userId != currentUserId || !w.active) continue;

        // 错题的低 mastery 会拉低对应章节平均掌握度。
        inputs.push_back({w.subject, w.chapter, w.mastery});
    }

    // 没有任何材料时无法计算薄弱点。
    if (inputs.empty()) {
        cout << "当前没有任何可用数据，无法计算薄弱点。\n";
        pauseScreen();
        return;
    }

    // 专项练习只取最薄弱章节，保证一次练习目标明确。
    vector<RecommendResult> recs = calculateWeakestChapters(inputs, 1);

    // 推荐结果为空时说明输入无法形成有效 subject/chapter 分组。
    if (recs.empty()) {
        cout << "无法计算薄弱点。\n";
        pauseScreen();
        return;
    }

    // 取 Top 1 作为本次专项练习目标学科。
    string targetSubject = recs[0].subject;

    // 取 Top 1 作为本次专项练习目标章节。
    string targetChapter = recs[0].chapter;

    // 展示系统判定的薄弱点。
    cout << "系统判定您的【最薄弱盲区】为：\n";
    cout << "【" << targetSubject << " - " << targetChapter << "】 (平均掌握度低至: " << recs[0].avgMastery << " 分)\n\n";
    
    cout << "正在为您抽取该章节的相关知识卡片...\n";

    // 专项练习的实际出题池只使用知识卡片。
    vector<int> cardPool;

    // 扫描所有卡片，找出目标学科+章节。
    for (size_t i = 0; i < cards.size(); ++i) {
        // 必须当前用户、active=true、学科章节都匹配。
        if (cards[i].userId == currentUserId && cards[i].active && 
            cards[i].subject == targetSubject && cards[i].chapter == targetChapter) {
            cardPool.push_back(i);
        }
    }

    // 如果薄弱点主要来自错题，但还没有转成卡片，就无法用卡片形式出题。
    if (cardPool.empty()) {
        cout << "\n呃...该薄弱点下似乎全都是错题，暂时没有建立相关知识卡片用于测试。\n";
        cout << "建议您先去【错题管理】中将部分错题转化为知识卡片！\n";
        pauseScreen();
        return;
    }

    // 告知用户题量，并等待确认开始。
    cout << "找到 " << cardPool.size() << " 张相关卡片。按回车键开始专项歼灭战...\n";
    string line;
    getline(cin, line);

    // 使用时间种子随机打乱本章节卡片顺序。
    unsigned seed = chrono::system_clock::now().time_since_epoch().count();
    shuffle(cardPool.begin(), cardPool.end(), default_random_engine(seed));

    // 默认练习全部匹配卡片。
    int count = cardPool.size();

    // 单次最多 15 张，避免薄弱章节卡片过多导致一次练习时间失控。
    if (count > 15) count = 15;

    // 统计本次专项练习中用户自评答对的数量。
    int correctCount = 0;

    // 逐题练习。
    for (int i = 0; i < count; ++i) {
        // 每道题单独清屏展示。
        clearScreen();
        cout << "==============================\n";
        cout << " 专项突破 (" << (i+1) << "/" << count << ") - " << targetChapter << "\n";
        cout << "==============================\n";
        
        // 通过下标取出本题卡片。
        const Card& c = cards[cardPool[i]];

        // 展示卡片正面。
        cout << "题目：\n" << c.front << "\n\n";
        
        // 等待用户思考完成。
        cout << "(按回车键查看答案...)\n";
        getline(cin, line);
        
        // 展示卡片背面答案。
        cout << "------------------------------\n";
        cout << "答案：\n" << c.back << "\n";
        cout << "------------------------------\n\n";
        
        // 专项练习只做自评，不进入 SM2 算法。
        cout << "你答对了吗？(y/n，直接回车默认算对)：";
        getline(cin, line);
        line = trim(line);

        // 只有明确输入 n/N 才算错。
        if (line == "n" || line == "N") {
            // 专项练习仍不写正式复习状态，保持“主动练习”和“今日复习”职责分离。
        } else {
            // 默认和其他输入都算答对。
            correctCount++;
        }
    }

    // 输出本次专项练习总结。
    clearScreen();
    cout << "==============================\n";
    cout << "       专项练习结束\n";
    cout << "==============================\n";

    // 输出完成数量和正确率。
    cout << "本次专项训练共完成 " << count << " 道题，成功答对 " << correctCount << " 道。\n";
    cout << "正确率：" << (count > 0 ? (correctCount * 100 / count) : 0) << "%\n\n";
    
    // 根据正确率给出即时反馈；这只是提示，不写入数据。
    if (correctCount == count) {
        cout << "太棒了！看来你已经克服了这个薄弱点，下次系统应该会推荐新的章节了！\n";
    } else if (correctCount * 100 / count >= 60) {
        cout << "表现尚可，不过还有提升空间，建议结合错题本继续巩固！\n";
    } else {
        cout << "革命尚未成功，同志仍需努力！请务必去【今日复习】强化一下该章节的记忆。\n";
    }
    pauseScreen();
}

void showPracticeMenu() {
    // 自测练习子菜单循环，直到用户选择 0 返回主菜单。
    while (true) {
        // 每轮菜单前清屏。
        clearScreen();
        cout << "==============================\n";
        cout << "       自测练习中心\n";
        cout << "==============================\n";
        cout << "1. 随机抽查测试（脱敏无压测验）\n";
        cout << "2. 薄弱点专项突破（基于智能推荐）\n";
        cout << "0. 返回主菜单\n";
        cout << "请选择：";

        // 读取菜单输入。
        string line;
        if (!getline(cin, line)) return;
        int choice;

        // 菜单输入必须是整数。
        if (!parseInt(line, choice)) {
            cout << "输入无效，请重新输入。\n";
            pauseScreen();
            continue;
        }

        // 根据菜单项分发到具体练习流程。
        switch (choice) {
            // 随机抽查测试。
            case 1: startRandomPractice(); break;

            // 基于推荐算法的薄弱点专项突破。
            case 2: startWeaknessPractice(); break;

            // 返回主菜单。
            case 0: return;

            default:
                // 其他整数不是合法菜单项。
                cout << "菜单选项不存在。\n";
                pauseScreen();
        }
    }
}
