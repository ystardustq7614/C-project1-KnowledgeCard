#include "practice.h"
#include "globals.h"
#include "utils.h"
#include "tui.h"
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
    renderPageHeader("随机抽查测试", "从当前有效卡片中随机抽题，不影响正式复习计划。");

    vector<int> pool = getActiveCardIndices();
    if (pool.empty()) {
        printTuiNotice(TuiNoticeLevel::Warning, "当前没有可用卡片，无法进行练习。");
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
            printTuiNotice(TuiNoticeLevel::Warning, "输入无效，系统将使用默认值 10。");
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
        const Card& c = cards[pool[i]];
        renderPageHeader("随机自测 (" + std::to_string(i + 1) + "/" + std::to_string(count) + ")",
                         c.subject + " / " + c.chapter);
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
    renderPageHeader("自测练习结束", "本次自测属于无压练习，不影响日常复习计划与系统掌握度。");
    cout << "共完成 " << count << " 道题。\n";
    cout << "答对 " << correctCount << " 道，正确率：" << (count > 0 ? (correctCount * 100 / count) : 0) << "%\n\n";
    printTuiNotice(TuiNoticeLevel::Info, "本次自测属于无压练习，不影响日常复习计划与系统掌握度。");
    pauseScreen();
}

void startWeaknessPractice() {
    clearScreen();
    renderPageHeader("薄弱点专项突破练习", "根据智能推荐锁定当前最薄弱章节。");

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
        printTuiNotice(TuiNoticeLevel::Warning, "当前没有任何可用数据，无法计算薄弱点。");
        pauseScreen();
        return;
    }

    // 专项练习只取最薄弱章节，保证一次练习目标明确。
    vector<RecommendResult> recs = calculateWeakestChapters(inputs, 1);
    if (recs.empty()) {
        printTuiNotice(TuiNoticeLevel::Warning, "无法计算薄弱点。");
        pauseScreen();
        return;
    }

    string targetSubject = recs[0].subject;
    string targetChapter = recs[0].chapter;

    printTuiNotice(TuiNoticeLevel::Info,
                   "系统判定您的最薄弱盲区为：" + targetSubject + " - " + targetChapter +
                   "，平均掌握度：" + std::to_string(recs[0].avgMastery) + " 分。");
    cout << "正在为您抽取该章节的相关知识卡片...\n";

    vector<int> cardPool;
    for (size_t i = 0; i < cards.size(); ++i) {
        if (cards[i].userId == currentUserId && cards[i].active && 
            cards[i].subject == targetSubject && cards[i].chapter == targetChapter) {
            cardPool.push_back(i);
        }
    }

    if (cardPool.empty()) {
        printTuiNotice(TuiNoticeLevel::Warning, "该薄弱点下暂时没有相关知识卡片可用于测试。");
        cout << "建议先在错题管理中将部分错题转化为知识卡片。\n";
        pauseScreen();
        return;
    }

    cout << "找到 " << cardPool.size() << " 张相关卡片。按回车键开始专项练习...\n";
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
        renderPageHeader("专项突破 (" + std::to_string(i + 1) + "/" + std::to_string(count) + ")",
                         targetSubject + " / " + targetChapter);
        
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
    renderPageHeader("专项练习结束", "本次专项练习不写入正式复习状态。");
    cout << "本次专项训练共完成 " << count << " 道题，成功答对 " << correctCount << " 道。\n";
    cout << "正确率：" << (count > 0 ? (correctCount * 100 / count) : 0) << "%\n\n";
    
    if (correctCount == count) {
        printTuiNotice(TuiNoticeLevel::Success, "本轮专项练习全部答对，下次系统可能会推荐新的章节。");
    } else if (correctCount * 100 / count >= 60) {
        printTuiNotice(TuiNoticeLevel::Info, "本轮表现基本稳定，建议结合错题本继续巩固。");
    } else {
        printTuiNotice(TuiNoticeLevel::Warning, "正确率偏低，建议到今日复习中强化该章节记忆。");
    }
    pauseScreen();
}

void showPracticeMenu() {
    while (true) {
        clearScreen();
        renderSubMenu("自测练习中心", "不改动复习计划的主动练习入口", {
            {"1", "随机抽查测试", "quick check"},
            {"2", "薄弱点专项突破", "recommended"},
            {"0", "返回主菜单", "back"}
        });

        string line;
        if (!getline(cin, line)) return;
        int choice;
        if (!parseInt(line, choice)) {
            printTuiNotice(TuiNoticeLevel::Error, "输入无效，请重新输入。");
            pauseScreen();
            continue;
        }

        switch (choice) {
            case 1: startRandomPractice(); break;
            case 2: startWeaknessPractice(); break;
            case 0: return;
            default:
                printTuiNotice(TuiNoticeLevel::Error, "菜单选项不存在。");
                pauseScreen();
        }
    }
}
