#include "practice.h"
#include "globals.h"
#include "utils.h"
#include "algo_recommend.h"
#include <iostream>
#include <vector>
#include <algorithm>
#include <random>
#include <chrono>

using std::cout;
using std::cin;
using std::default_random_engine;
using std::getline;
using std::shuffle;
using std::string;
using std::vector;

/*
模块职责：
- 提供不影响正式复习计划的主动练习流程。

关键约束：
- 练习结果只在本次会话内统计，不写 ReviewLog，不修改 mastery/nextReviewDate。
- 题目池只取当前用户 active=true 的卡片，避免练习中心绕过数据隔离。
*/

// 返回 cards 容器下标，不是 cardId；调用方只在本次函数内使用，避免跨修改缓存。
static vector<int> getActiveCardIndices() {
    vector<int> res;
    for (size_t i = 0; i < cards.size(); ++i) {
        if (cards[i].userId == currentUserId && cards[i].active) {
            res.push_back(i);
        }
    }
    return res;
}

void startRandomPractice() {
    clearScreen();
    cout << "==============================\n";
    cout << "       随机抽查测试\n";
    cout << "==============================\n";

    vector<int> pool = getActiveCardIndices();
    if (pool.empty()) {
        cout << "当前没有可用卡片，无法进行练习。\n";
        pauseScreen();
        return;
    }

    cout << "当前可用知识卡片总数：" << pool.size() << "\n";
    cout << "请输入要抽取的卡片数量（直接回车默认 10 张）：";
    string line;
    getline(cin, line);
    line = trim(line);
    
    int count = 10;
    if (!line.empty()) {
        if (!parseInt(line, count) || count <= 0) {
            cout << "输入无效，系统将使用默认值 10。\n";
            count = 10;
        }
    }
    
    if (count > static_cast<int>(pool.size())) {
        count = pool.size();
    }

    // 使用时间种子仅满足轻量随机抽查，不用于可复现实验；测试不依赖抽题顺序。
    unsigned seed = std::chrono::system_clock::now().time_since_epoch().count();
    shuffle(pool.begin(), pool.end(), default_random_engine(seed));

    int correctCount = 0;
    for (int i = 0; i < count; ++i) {
        clearScreen();
        cout << "==============================\n";
        cout << " 随机自测 (" << (i+1) << "/" << count << ")\n";
        cout << "==============================\n";
        
        const Card& c = cards[pool[i]];
        cout << "【" << c.subject << " - " << c.chapter << "】\n\n";
        cout << "题目：\n" << c.front << "\n\n";
        
        cout << "(按回车键查看答案...)\n";
        getline(cin, line);
        
        cout << "------------------------------\n";
        cout << "答案：\n" << c.back << "\n";
        cout << "------------------------------\n\n";
        
        cout << "你答对了吗？(y/n，直接回车默认算对)：";
        getline(cin, line);
        line = trim(line);
        if (line == "n" || line == "N") {
            // 自测不扣分，避免用户因为主动练习影响正式复习计划。
        } else {
            correctCount++;
        }
    }

    clearScreen();
    cout << "==============================\n";
    cout << "       自测练习结束\n";
    cout << "==============================\n";
    cout << "共完成 " << count << " 道题。\n";
    cout << "答对 " << correctCount << " 道，正确率：" << (count > 0 ? (correctCount * 100 / count) : 0) << "%\n\n";
    cout << "注：本次自测属于无压练习，不影响日常复习计划与系统掌握度。\n";
    pauseScreen();
}

void startWeaknessPractice() {
    clearScreen();
    cout << "==============================\n";
    cout << "     薄弱点专项突破练习\n";
    cout << "==============================\n";

    vector<RecommendInputItem> inputs;
    for (const Card& c : cards) {
        if (c.userId != currentUserId || !c.active) continue;
        inputs.push_back({c.subject, c.chapter, c.mastery});
    }
    for (const WrongQuestion& w : wrongs) {
        if (w.userId != currentUserId || !w.active) continue;
        inputs.push_back({w.subject, w.chapter, w.mastery});
    }

    if (inputs.empty()) {
        cout << "当前没有任何可用数据，无法计算薄弱点。\n";
        pauseScreen();
        return;
    }

    // 专项练习只取最薄弱章节，保证一次练习目标明确。
    vector<RecommendResult> recs = calculateWeakestChapters(inputs, 1);
    if (recs.empty()) {
        cout << "无法计算薄弱点。\n";
        pauseScreen();
        return;
    }

    string targetSubject = recs[0].subject;
    string targetChapter = recs[0].chapter;

    cout << "系统判定您的【最薄弱盲区】为：\n";
    cout << "【" << targetSubject << " - " << targetChapter << "】 (平均掌握度低至: " << recs[0].avgMastery << " 分)\n\n";
    
    cout << "正在为您抽取该章节的相关知识卡片...\n";

    vector<int> cardPool;
    for (size_t i = 0; i < cards.size(); ++i) {
        if (cards[i].userId == currentUserId && cards[i].active && 
            cards[i].subject == targetSubject && cards[i].chapter == targetChapter) {
            cardPool.push_back(i);
        }
    }

    if (cardPool.empty()) {
        cout << "\n呃...该薄弱点下似乎全都是错题，暂时没有建立相关知识卡片用于测试。\n";
        cout << "建议您先去【错题管理】中将部分错题转化为知识卡片！\n";
        pauseScreen();
        return;
    }

    cout << "找到 " << cardPool.size() << " 张相关卡片。按回车键开始专项歼灭战...\n";
    string line;
    getline(cin, line);

    unsigned seed = std::chrono::system_clock::now().time_since_epoch().count();
    shuffle(cardPool.begin(), cardPool.end(), default_random_engine(seed));

    int count = cardPool.size();
    // 单次最多 15 张，避免薄弱章节卡片过多导致一次练习时间失控。
    if (count > 15) count = 15;

    int correctCount = 0;
    for (int i = 0; i < count; ++i) {
        clearScreen();
        cout << "==============================\n";
        cout << " 专项突破 (" << (i+1) << "/" << count << ") - " << targetChapter << "\n";
        cout << "==============================\n";
        
        const Card& c = cards[cardPool[i]];
        cout << "题目：\n" << c.front << "\n\n";
        
        cout << "(按回车键查看答案...)\n";
        getline(cin, line);
        
        cout << "------------------------------\n";
        cout << "答案：\n" << c.back << "\n";
        cout << "------------------------------\n\n";
        
        cout << "你答对了吗？(y/n，直接回车默认算对)：";
        getline(cin, line);
        line = trim(line);
        if (line == "n" || line == "N") {
            // 专项练习仍不写正式复习状态，保持“主动练习”和“今日复习”职责分离。
        } else {
            correctCount++;
        }
    }

    clearScreen();
    cout << "==============================\n";
    cout << "       专项练习结束\n";
    cout << "==============================\n";
    cout << "本次专项训练共完成 " << count << " 道题，成功答对 " << correctCount << " 道。\n";
    cout << "正确率：" << (count > 0 ? (correctCount * 100 / count) : 0) << "%\n\n";
    
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
    while (true) {
        clearScreen();
        cout << "==============================\n";
        cout << "       自测练习中心\n";
        cout << "==============================\n";
        cout << "1. 随机抽查测试（脱敏无压测验）\n";
        cout << "2. 薄弱点专项突破（基于智能推荐）\n";
        cout << "0. 返回主菜单\n";
        cout << "请选择：";

        string line;
        if (!getline(cin, line)) return;
        int choice;
        if (!parseInt(line, choice)) {
            cout << "输入无效，请重新输入。\n";
            pauseScreen();
            continue;
        }

        switch (choice) {
            case 1: startRandomPractice(); break;
            case 2: startWeaknessPractice(); break;
            case 0: return;
            default:
                cout << "菜单选项不存在。\n";
                pauseScreen();
        }
    }
}
