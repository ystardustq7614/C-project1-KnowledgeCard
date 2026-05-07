#include "stats.h"
#include "globals.h"
#include "utils.h"
#include <iostream>
#include <map>

using namespace std;

/*
[导读]
- 本文件只做统计展示，适合学习“如何从全局容器聚合出用户可读视图”。

[对应流程图]
- cards/wrongs/logs -> 当前用户过滤 -> 分组/计数/分档 -> 控制台展示。

[输入输出]
- 输入：当前内存数据和 currentUserId。
- 输出：控制台统计结果；不修改容器，不保存文件。

[易错点]
- 统计不负责修复异常记录；字段范围和坏关联要交给 maintenance.cpp。
- 卡片/错题统计默认只看 active=true，复习日志则按 userId 保留历史轨迹。

[实验]
- 调整 mastery 的三档阈值，再对照 README 和测试数据确认统计口径是否也要同步。
*/

// ================================================================
// 1. 卡片总数统计
// ================================================================

void statCardCount() {
    clearScreen();
    cout << "==============================\n";
    cout << "     卡片总数统计\n";
    cout << "==============================\n";

    int count = 0;
    for (size_t i = 0; i < cards.size(); ++i) {
        if (cards[i].userId == currentUserId && cards[i].active) {
            count++;
        }
    }
    cout << "当前用户有效卡片总数：" << count << "\n";
    pauseScreen();
}

// ================================================================
// 2. 错题总数统计
// ================================================================

void statWrongCount() {
    clearScreen();
    cout << "==============================\n";
    cout << "     错题总数统计\n";
    cout << "==============================\n";

    int count = 0;
    for (size_t i = 0; i < wrongs.size(); ++i) {
        if (wrongs[i].userId == currentUserId && wrongs[i].active) {
            count++;
        }
    }
    cout << "当前用户有效错题总数：" << count << "\n";
    pauseScreen();
}

// ================================================================
// 3. 各学科分布统计
// ================================================================

void statSubjectDistribution() {
    clearScreen();
    cout << "==============================\n";
    cout << "     各学科分布统计\n";
    cout << "==============================\n";

    // 卡片和错题分开统计，避免用户误以为同一学科下两类材料数量可以直接合并。
    map<string, int> cardSubjects;
    map<string, int> wrongSubjects;

    for (size_t i = 0; i < cards.size(); ++i) {
        if (cards[i].userId == currentUserId && cards[i].active) {
            cardSubjects[cards[i].subject]++;
        }
    }
    for (size_t i = 0; i < wrongs.size(); ++i) {
        if (wrongs[i].userId == currentUserId && wrongs[i].active) {
            wrongSubjects[wrongs[i].subject]++;
        }
    }

    if (cardSubjects.empty() && wrongSubjects.empty()) {
        cout << "当前没有任何数据。\n";
        pauseScreen();
        return;
    }

    // 先合并学科集合，确保只出现在错题或只出现在卡片中的学科也能显示。
    map<string, bool> allSubjects;
    for (auto it = cardSubjects.begin(); it != cardSubjects.end(); ++it) {
        allSubjects[it->first] = true;
    }
    for (auto it = wrongSubjects.begin(); it != wrongSubjects.end(); ++it) {
        allSubjects[it->first] = true;
    }

    cout << "学科             卡片数   错题数\n";
    cout << "------------------------------\n";
    for (auto it = allSubjects.begin(); it != allSubjects.end(); ++it) {
        const string& subj = it->first;
        int cCount = 0, wCount = 0;
        if (cardSubjects.count(subj))  cCount = cardSubjects[subj];
        if (wrongSubjects.count(subj)) wCount = wrongSubjects[subj];

        // 控制台没有表格组件，按字节宽度近似对齐；中文宽度在不同终端可能略有偏差。
        cout << "  " << subj;
        int pad = 15 - static_cast<int>(subj.size());
        if (pad < 1) pad = 1;
        for (int p = 0; p < pad; ++p) cout << " ";
        cout << cCount << "        " << wCount << "\n";
    }

    pauseScreen();
}

// ================================================================
// 4. 今日待复习统计
// ================================================================

void statDueTodayCount() {
    clearScreen();
    cout << "==============================\n";
    cout << "     今日待复习统计\n";
    cout << "==============================\n";

    string today = getTodayDate();
    int cardDue = 0, wrongDue = 0;

    for (size_t i = 0; i < cards.size(); ++i) {
        if (cards[i].userId == currentUserId && cards[i].active) {
            if (compareDate(cards[i].nextReviewDate, today) <= 0) {
                cardDue++;
            }
        }
    }
    for (size_t i = 0; i < wrongs.size(); ++i) {
        if (wrongs[i].userId == currentUserId && wrongs[i].active) {
            if (compareDate(wrongs[i].nextReviewDate, today) <= 0) {
                wrongDue++;
            }
        }
    }

    cout << "今日待复习卡片：" << cardDue << " 条\n";
    cout << "今日待复习错题：" << wrongDue << " 条\n";
    cout << "合计待复习：    " << (cardDue + wrongDue) << " 条\n";
    pauseScreen();
}

// ================================================================
// 5. 掌握情况统计（按 0~39、40~69、70~100 三档）
// ================================================================

void statMasteryDistribution() {
    clearScreen();
    cout << "==============================\n";
    cout << "     掌握情况统计\n";
    cout << "==============================\n";

    // 三档阈值是面向用户的统计口径，修改后需要同步 README 和演示脚本。
    int cLow = 0, cMid = 0, cHigh = 0;
    for (size_t i = 0; i < cards.size(); ++i) {
        if (cards[i].userId != currentUserId || !cards[i].active) continue;
        int m = cards[i].mastery;
        if (m <= 39)      cLow++;
        else if (m <= 69)  cMid++;
        else               cHigh++;
    }

    int wLow = 0, wMid = 0, wHigh = 0;
    for (size_t i = 0; i < wrongs.size(); ++i) {
        if (wrongs[i].userId != currentUserId || !wrongs[i].active) continue;
        int m = wrongs[i].mastery;
        if (m <= 39)      wLow++;
        else if (m <= 69)  wMid++;
        else               wHigh++;
    }

    int cTotal = cLow + cMid + cHigh;
    int wTotal = wLow + wMid + wHigh;

    cout << "【知识卡片掌握分布】（共 " << cTotal << " 条）\n";
    cout << "  薄弱 (0~39) ：" << cLow << " 条\n";
    cout << "  一般 (40~69)：" << cMid << " 条\n";
    cout << "  熟练 (70~100)：" << cHigh << " 条\n";

    cout << "\n【错题掌握分布】（共 " << wTotal << " 条）\n";
    cout << "  薄弱 (0~39) ：" << wLow << " 条\n";
    cout << "  一般 (40~69)：" << wMid << " 条\n";
    cout << "  熟练 (70~100)：" << wHigh << " 条\n";

    // 每条记录一个 #，适合小型本地项目；大量数据时应改为按比例缩放。
    if (cTotal > 0 || wTotal > 0) {
        cout << "\n【可视化】（# = 1 条）\n";
        if (cTotal > 0) {
            cout << "卡片 薄弱 : ";
            for (int j = 0; j < cLow; ++j) cout << "#";
            cout << " " << cLow << "\n";
            cout << "卡片 一般 : ";
            for (int j = 0; j < cMid; ++j) cout << "#";
            cout << " " << cMid << "\n";
            cout << "卡片 熟练 : ";
            for (int j = 0; j < cHigh; ++j) cout << "#";
            cout << " " << cHigh << "\n";
        }
        if (wTotal > 0) {
            cout << "错题 薄弱 : ";
            for (int j = 0; j < wLow; ++j) cout << "#";
            cout << " " << wLow << "\n";
            cout << "错题 一般 : ";
            for (int j = 0; j < wMid; ++j) cout << "#";
            cout << " " << wMid << "\n";
            cout << "错题 熟练 : ";
            for (int j = 0; j < wHigh; ++j) cout << "#";
            cout << " " << wHigh << "\n";
        }
    }

    pauseScreen();
}

// ================================================================
// 6. 复习频率统计（最近 7 天）
// ================================================================

void statReviewFrequency() {
    clearScreen();
    cout << "==============================\n";
    cout << "   最近 7 天复习频率统计\n";
    cout << "==============================\n";

    string today = getTodayDate();

    // 固定 7 天窗口便于用户快速判断近期复习连续性，避免长期日志把视图撑大。
    string dates[7];
    for (int d = 6; d >= 0; --d) {
        dates[6 - d] = addDays(today, -d);
    }

    int counts[7] = {0};
    for (size_t i = 0; i < logs.size(); ++i) {
        if (logs[i].userId != currentUserId) continue;
        for (int d = 0; d < 7; ++d) {
            if (logs[i].reviewDate == dates[d]) {
                counts[d]++;
                break;
            }
        }
    }

    int total = 0;
    int maxCount = 0;
    for (int d = 0; d < 7; ++d) {
        total += counts[d];
        if (counts[d] > maxCount) maxCount = counts[d];
    }

    cout << "日期          复习次数\n";
    cout << "------------------------------\n";
    for (int d = 0; d < 7; ++d) {
        string marker = (dates[d] == today) ? " (今天)" : "";
        cout << "  " << dates[d] << marker;
        int pad = 10 - static_cast<int>(marker.size());
        if (pad < 1) pad = 1;
        for (int p = 0; p < pad; ++p) cout << " ";
        cout << counts[d] << "  ";
        // 这里使用块字符增强可读性；不参与自动化断言。
        for (int j = 0; j < counts[d]; ++j) cout << "█";
        cout << "\n";
    }
    cout << "------------------------------\n";
    cout << "近 7 天合计：" << total << " 次\n";

    pauseScreen();
}

// ================================================================
// 7. 错因分类统计（V1.1）
// ================================================================

void statErrorTypeDistribution() {
    clearScreen();
    cout << "==============================\n";
    cout << "     错因分类统计\n";
    cout << "==============================\n";

    // 固定错因列表与 wrong.cpp 保持同一统计口径；空值统一归入“未分类”。
    const string types[] = {
        "概念不清", "记忆错误", "粗心", "审题失误", "计算错误", "方法不会"
    };
    const int typeCount = 6;

    int counts[6] = {0};
    int uncategorized = 0;
    int total = 0;

    for (size_t i = 0; i < wrongs.size(); ++i) {
        if (wrongs[i].userId != currentUserId || !wrongs[i].active) continue;
        total++;

        bool matched = false;
        for (int t = 0; t < typeCount; ++t) {
            if (wrongs[i].errorType == types[t]) {
                counts[t]++;
                matched = true;
                break;
            }
        }
        if (!matched) uncategorized++;
    }

    if (total == 0) {
        cout << "当前没有有效错题数据。\n";
        pauseScreen();
        return;
    }

    cout << "当前用户有效错题共 " << total << " 条\n\n";
    cout << "错因类型         数量\n";
    cout << "------------------------------\n";
    for (int t = 0; t < typeCount; ++t) {
        cout << "  " << types[t];
        int pad = 13 - static_cast<int>(types[t].size());
        if (pad < 1) pad = 1;
        for (int p = 0; p < pad; ++p) cout << " ";
        cout << counts[t] << "\n";
    }
    cout << "  未分类         " << uncategorized << "\n";
    cout << "------------------------------\n";

    // 错因分类通常数量不大，直接按条数绘制即可。
    cout << "\n【可视化】（# = 1 条）\n";
    for (int t = 0; t < typeCount; ++t) {
        if (counts[t] > 0) {
            cout << types[t] << " : ";
            for (int j = 0; j < counts[t]; ++j) cout << "#";
            cout << " " << counts[t] << "\n";
        }
    }
    if (uncategorized > 0) {
        cout << "未分类 : ";
        for (int j = 0; j < uncategorized; ++j) cout << "#";
        cout << " " << uncategorized << "\n";
    }

    pauseScreen();
}

// ================================================================
// 统计分析子菜单
// ================================================================

void showStatsMenu() {
    while (true) {
        clearScreen();
        cout << "==============================\n";
        cout << "       统计分析\n";
        cout << "==============================\n";
        cout << "1. 卡片总数统计\n";
        cout << "2. 错题总数统计\n";
        cout << "3. 各学科分布统计\n";
        cout << "4. 今日待复习统计\n";
        cout << "5. 掌握情况统计\n";
        cout << "6. 复习频率统计\n";
        cout << "7. 错因分类统计\n";
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
            case 1: statCardCount();            break;
            case 2: statWrongCount();            break;
            case 3: statSubjectDistribution();   break;
            case 4: statDueTodayCount();         break;
            case 5: statMasteryDistribution();   break;
            case 6: statReviewFrequency();       break;
            case 7: statErrorTypeDistribution(); break;
            case 0: return;
            default:
                cout << "菜单选项不存在。\n";
                pauseScreen();
        }
    }
}
