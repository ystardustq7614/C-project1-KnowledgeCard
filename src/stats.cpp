// 引入统计模块头文件：统计函数和统计菜单声明都在这里。
#include "stats.h"

// 引入全局状态：cards、wrongs、logs、currentUserId 等来自这里。
#include "globals.h"

// 引入通用工具：日期处理、输入解析、暂停/清屏等。
#include "utils.h"

// iostream 提供 cout/cin，用于控制台统计展示和菜单输入。
#include <iostream>

// map 用于按学科分组，并保持键的稳定顺序。
#include <map>

// 只引入本文件实际使用的标准库名字，避免命名空间整体引入污染。
using std::cin;
using std::cout;
using std::getline;
using std::map;
using std::string;

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
- 统计展示是只读视图，不能在这里顺手修数据或写文件。

[实验]
- 调整 mastery 的三档阈值，再对照 README 和测试数据确认统计口径是否也要同步。
- 人工改一条日志日期，再看最近 7 天统计如何变化，理解日志与当前对象状态是两套数据。
*/

// ================================================================
// 1. 卡片总数统计
// ================================================================

void statCardCount() {
    // 进入卡片总数统计页面。
    clearScreen();
    cout << "==============================\n";
    cout << "     卡片总数统计\n";
    cout << "==============================\n";

    // 统计当前用户 active=true 的卡片数量。
    int count = 0;

    // 扫描全局 cards 容器。
    for (size_t i = 0; i < cards.size(); ++i) {
        // 只统计当前用户且未逻辑删除的卡片。
        if (cards[i].userId == currentUserId && cards[i].active) {
            count++;
        }
    }

    // 输出统计结果。
    cout << "当前用户有效卡片总数：" << count << "\n";
    pauseScreen();
}

// ================================================================
// 2. 错题总数统计
// ================================================================

void statWrongCount() {
    // 进入错题总数统计页面。
    clearScreen();
    cout << "==============================\n";
    cout << "     错题总数统计\n";
    cout << "==============================\n";

    // 统计当前用户 active=true 的错题数量。
    int count = 0;

    // 扫描全局 wrongs 容器。
    for (size_t i = 0; i < wrongs.size(); ++i) {
        // 只统计当前用户且未逻辑删除的错题。
        if (wrongs[i].userId == currentUserId && wrongs[i].active) {
            count++;
        }
    }

    // 输出统计结果。
    cout << "当前用户有效错题总数：" << count << "\n";
    pauseScreen();
}

// ================================================================
// 3. 各学科分布统计
// ================================================================

void statSubjectDistribution() {
    // 进入各学科分布统计页面。
    clearScreen();
    cout << "==============================\n";
    cout << "     各学科分布统计\n";
    cout << "==============================\n";

    // 卡片和错题分开统计，避免用户误以为同一学科下两类材料数量可以直接合并。
    map<string, int> cardSubjects;
    map<string, int> wrongSubjects;

    // 第一段：统计各学科下的有效卡片数。
    for (size_t i = 0; i < cards.size(); ++i) {
        // 只统计当前用户且 active=true 的卡片。
        if (cards[i].userId == currentUserId && cards[i].active) {
            // 以 subject 为分组键，数量加 1。
            cardSubjects[cards[i].subject]++;
        }
    }

    // 第二段：统计各学科下的有效错题数。
    for (size_t i = 0; i < wrongs.size(); ++i) {
        // 只统计当前用户且 active=true 的错题。
        if (wrongs[i].userId == currentUserId && wrongs[i].active) {
            // 以 subject 为分组键，数量加 1。
            wrongSubjects[wrongs[i].subject]++;
        }
    }

    // 两类数据都为空时，没有可展示的学科分布。
    if (cardSubjects.empty() && wrongSubjects.empty()) {
        cout << "当前没有任何数据。\n";
        pauseScreen();
        return;
    }

    // 先合并学科集合，确保只出现在错题或只出现在卡片中的学科也能显示。
    map<string, bool> allSubjects;

    // 把卡片学科加入全集。
    for (auto it = cardSubjects.begin(); it != cardSubjects.end(); ++it) {
        allSubjects[it->first] = true;
    }

    // 把错题学科加入全集。
    for (auto it = wrongSubjects.begin(); it != wrongSubjects.end(); ++it) {
        allSubjects[it->first] = true;
    }

    // 输出表头。
    cout << "学科             卡片数   错题数\n";
    cout << "------------------------------\n";

    // 遍历学科全集，逐科输出卡片数和错题数。
    for (auto it = allSubjects.begin(); it != allSubjects.end(); ++it) {
        const string& subj = it->first;

        // 默认数量为 0，避免某类数据缺失时访问不到。
        int cCount = 0, wCount = 0;

        // 如果该学科出现在卡片统计中，读取卡片数。
        if (cardSubjects.count(subj))  cCount = cardSubjects[subj];

        // 如果该学科出现在错题统计中，读取错题数。
        if (wrongSubjects.count(subj)) wCount = wrongSubjects[subj];

        // 控制台没有表格组件，按字节宽度近似对齐；中文宽度在不同终端可能略有偏差。
        cout << "  " << subj;

        // 根据 subject 字符串长度补空格。
        int pad = 15 - static_cast<int>(subj.size());
        if (pad < 1) pad = 1;
        for (int p = 0; p < pad; ++p) cout << " ";

        // 输出该学科的卡片数和错题数。
        cout << cCount << "        " << wCount << "\n";
    }

    pauseScreen();
}

// ================================================================
// 4. 今日待复习统计
// ================================================================

void statDueTodayCount() {
    // 进入今日待复习统计页面。
    clearScreen();
    cout << "==============================\n";
    cout << "     今日待复习统计\n";
    cout << "==============================\n";

    // 今日日期用于判断 nextReviewDate 是否已经到期。
    string today = getTodayDate();

    // 分别统计卡片和错题数量。
    int cardDue = 0, wrongDue = 0;

    // 第一段：统计今天应复习的卡片。
    for (size_t i = 0; i < cards.size(); ++i) {
        // 只统计当前用户有效卡片。
        if (cards[i].userId == currentUserId && cards[i].active) {
            // nextReviewDate <= today 表示今天到期或已经逾期。
            if (compareDate(cards[i].nextReviewDate, today) <= 0) {
                cardDue++;
            }
        }
    }

    // 第二段：统计今天应复习的错题。
    for (size_t i = 0; i < wrongs.size(); ++i) {
        // 只统计当前用户有效错题。
        if (wrongs[i].userId == currentUserId && wrongs[i].active) {
            // nextReviewDate <= today 表示今天到期或已经逾期。
            if (compareDate(wrongs[i].nextReviewDate, today) <= 0) {
                wrongDue++;
            }
        }
    }

    // 输出卡片、错题和合计数量。
    cout << "今日待复习卡片：" << cardDue << " 条\n";
    cout << "今日待复习错题：" << wrongDue << " 条\n";
    cout << "合计待复习：    " << (cardDue + wrongDue) << " 条\n";
    pauseScreen();
}

// ================================================================
// 5. 掌握情况统计（按 0~39、40~69、70~100 三档）
// ================================================================

void statMasteryDistribution() {
    // 进入掌握情况统计页面。
    clearScreen();
    cout << "==============================\n";
    cout << "     掌握情况统计\n";
    cout << "==============================\n";

    // 三档阈值是面向用户的统计口径，修改后需要同步 README 和演示脚本。
    // 卡片掌握度分档计数。
    int cLow = 0, cMid = 0, cHigh = 0;

    // 扫描当前用户有效卡片。
    for (size_t i = 0; i < cards.size(); ++i) {
        // 跳过其他用户和逻辑删除的卡片。
        if (cards[i].userId != currentUserId || !cards[i].active) continue;

        // 读取掌握度。
        int m = cards[i].mastery;

        // 0~39：薄弱。
        if (m <= 39)      cLow++;

        // 40~69：一般。
        else if (m <= 69)  cMid++;

        // 70~100：熟练；如果维护数据异常超过 100，这里也会落到熟练档。
        else               cHigh++;
    }

    // 错题掌握度分档计数。
    int wLow = 0, wMid = 0, wHigh = 0;

    // 扫描当前用户有效错题。
    for (size_t i = 0; i < wrongs.size(); ++i) {
        // 跳过其他用户和逻辑删除的错题。
        if (wrongs[i].userId != currentUserId || !wrongs[i].active) continue;

        // 读取掌握度。
        int m = wrongs[i].mastery;

        // 0~39：薄弱。
        if (m <= 39)      wLow++;

        // 40~69：一般。
        else if (m <= 69)  wMid++;

        // 70~100：熟练；如果维护数据异常超过 100，这里也会落到熟练档。
        else               wHigh++;
    }

    // 卡片总数。
    int cTotal = cLow + cMid + cHigh;

    // 错题总数。
    int wTotal = wLow + wMid + wHigh;

    // 输出卡片掌握分布。
    cout << "【知识卡片掌握分布】（共 " << cTotal << " 条）\n";
    cout << "  薄弱 (0~39) ：" << cLow << " 条\n";
    cout << "  一般 (40~69)：" << cMid << " 条\n";
    cout << "  熟练 (70~100)：" << cHigh << " 条\n";

    // 输出错题掌握分布。
    cout << "\n【错题掌握分布】（共 " << wTotal << " 条）\n";
    cout << "  薄弱 (0~39) ：" << wLow << " 条\n";
    cout << "  一般 (40~69)：" << wMid << " 条\n";
    cout << "  熟练 (70~100)：" << wHigh << " 条\n";

    // 每条记录一个 #，适合小型本地项目；大量数据时应改为按比例缩放。
    // 没有任何数据时不显示可视化区域。
    if (cTotal > 0 || wTotal > 0) {
        cout << "\n【可视化】（# = 1 条）\n";

        // 卡片三档可视化。
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

        // 错题三档可视化。
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
    // 进入最近 7 天复习频率统计页面。
    clearScreen();
    cout << "==============================\n";
    cout << "   最近 7 天复习频率统计\n";
    cout << "==============================\n";

    // 今天日期用于生成最近 7 天窗口。
    string today = getTodayDate();

    // 固定 7 天窗口便于用户快速判断近期复习连续性，避免长期日志把视图撑大。
    // dates[0] 是 6 天前，dates[6] 是今天。
    string dates[7];

    // 从 6 天前到今天依次填充日期。
    for (int d = 6; d >= 0; --d) {
        dates[6 - d] = addDays(today, -d);
    }

    // counts[d] 对应 dates[d] 当天的复习次数。
    int counts[7] = {0};

    // 扫描复习日志，而不是扫描卡片/错题当前状态。
    for (size_t i = 0; i < logs.size(); ++i) {
        // 只统计当前用户日志。
        if (logs[i].userId != currentUserId) continue;

        // 判断日志日期是否落在最近 7 天窗口内。
        for (int d = 0; d < 7; ++d) {
            if (logs[i].reviewDate == dates[d]) {
                // 命中某一天，次数加 1。
                counts[d]++;
                break;
            }
        }
    }

    // 计算近 7 天总次数和最大单日次数。
    int total = 0;
    int maxCount = 0;
    for (int d = 0; d < 7; ++d) {
        total += counts[d];
        if (counts[d] > maxCount) maxCount = counts[d];
    }

    // 输出表头。
    cout << "日期          复习次数\n";
    cout << "------------------------------\n";

    // 逐日输出复习次数和简单条形图。
    for (int d = 0; d < 7; ++d) {
        // 今天额外标记，方便用户定位当前日期。
        string marker = (dates[d] == today) ? " (今天)" : "";
        cout << "  " << dates[d] << marker;

        // 按标记长度补空格，做近似对齐。
        int pad = 10 - static_cast<int>(marker.size());
        if (pad < 1) pad = 1;
        for (int p = 0; p < pad; ++p) cout << " ";

        // 输出当天复习次数。
        cout << counts[d] << "  ";

        // 这里使用块字符增强可读性；不参与自动化断言。
        for (int j = 0; j < counts[d]; ++j) cout << "█";
        cout << "\n";
    }
    cout << "------------------------------\n";

    // 输出近 7 天合计。
    cout << "近 7 天合计：" << total << " 次\n";

    pauseScreen();
}

// ================================================================
// 7. 错因分类统计（V1.1）
// ================================================================

void statErrorTypeDistribution() {
    // 进入错因分类统计页面。
    clearScreen();
    cout << "==============================\n";
    cout << "     错因分类统计\n";
    cout << "==============================\n";

    // 固定错因列表与 wrong.cpp 保持同一统计口径；空值统一归入“未分类”。
    // [谨慎改] 如果新增错因类型，必须同步 wrong.cpp 的 VALID_ERROR_TYPES。
    const string types[] = {
        "概念不清", "记忆错误", "粗心", "审题失误", "计算错误", "方法不会"
    };

    // 合法错因类型数量。
    const int typeCount = 6;

    // counts[t] 对应 types[t] 的错题数量。
    int counts[6] = {0};

    // 未匹配任何合法类型的错题数量，包括空字符串和历史脏值。
    int uncategorized = 0;

    // 当前用户有效错题总数。
    int total = 0;

    // 扫描所有错题。
    for (size_t i = 0; i < wrongs.size(); ++i) {
        // 只统计当前用户有效错题。
        if (wrongs[i].userId != currentUserId || !wrongs[i].active) continue;

        // 有效错题总数加 1。
        total++;

        // 标记本条错题是否命中合法错因类型。
        bool matched = false;

        // 遍历合法错因类型。
        for (int t = 0; t < typeCount; ++t) {
            // 完全匹配某个类型时，对应计数加 1。
            if (wrongs[i].errorType == types[t]) {
                counts[t]++;
                matched = true;
                break;
            }
        }

        // 没命中任何合法类型，则归为未分类。
        if (!matched) uncategorized++;
    }

    // 没有有效错题时直接返回。
    if (total == 0) {
        cout << "当前没有有效错题数据。\n";
        pauseScreen();
        return;
    }

    // 输出总数和表格。
    cout << "当前用户有效错题共 " << total << " 条\n\n";
    cout << "错因类型         数量\n";
    cout << "------------------------------\n";

    // 逐个合法错因类型输出数量。
    for (int t = 0; t < typeCount; ++t) {
        cout << "  " << types[t];

        // 按字节长度近似补空格；中文宽度在不同终端可能略有偏差。
        int pad = 13 - static_cast<int>(types[t].size());
        if (pad < 1) pad = 1;
        for (int p = 0; p < pad; ++p) cout << " ";

        // 输出该类型数量。
        cout << counts[t] << "\n";
    }

    // 输出未分类数量。
    cout << "  未分类         " << uncategorized << "\n";
    cout << "------------------------------\n";

    // 错因分类通常数量不大，直接按条数绘制即可。
    cout << "\n【可视化】（# = 1 条）\n";

    // 只展示数量大于 0 的合法类型，减少空行。
    for (int t = 0; t < typeCount; ++t) {
        if (counts[t] > 0) {
            cout << types[t] << " : ";
            for (int j = 0; j < counts[t]; ++j) cout << "#";
            cout << " " << counts[t] << "\n";
        }
    }

    // 未分类数量大于 0 时也展示。
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
    // 统计分析子菜单循环，直到用户选择 0 返回主菜单。
    while (true) {
        // 每轮菜单前清屏。
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

        // 根据菜单项分发到具体统计函数。
        switch (choice) {
            // 卡片总数统计。
            case 1: statCardCount();            break;

            // 错题总数统计。
            case 2: statWrongCount();            break;

            // 各学科卡片/错题分布。
            case 3: statSubjectDistribution();   break;

            // 今日待复习数量。
            case 4: statDueTodayCount();         break;

            // 掌握度三档分布。
            case 5: statMasteryDistribution();   break;

            // 最近 7 天复习频率。
            case 6: statReviewFrequency();       break;

            // 错因类型分布。
            case 7: statErrorTypeDistribution(); break;

            // 返回主菜单。
            case 0: return;

            default:
                // 其他整数不是合法菜单项。
                cout << "菜单选项不存在。\n";
                pauseScreen();
        }
    }
}
