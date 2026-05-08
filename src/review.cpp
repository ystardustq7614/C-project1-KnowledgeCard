// 引入复习模块头文件：ReviewTask、公开入口函数声明都在这里。
#include "review.h"

// 引入全局状态：cards、wrongs、logs、currentUserId 等来自这里。
#include "globals.h"

// 引入存储层接口：saveCards/saveWrongs/saveLogs/getNextLogId 等来自这里。
#include "storage.h"

// 引入通用工具：日期比较、日期加减、输入解析、暂停/清屏等。
#include "utils.h"

// iostream 提供 cin/cout，用于控制台复习交互。
#include <iostream>

// vector 用于生成今日复习任务列表和日志索引列表。
#include <vector>

// 引入 SM2 简化算法：calculateNextReview 根据评分计算新掌握度和新间隔。
#include "algo_sm2.h"

// algorithm 提供 sort，用于按优先级排序今日任务。
#include <algorithm>

// 只引入本文件实际使用的标准库名字，避免依赖头文件中的命名空间整体引入。
using std::cin;
using std::cout;
using std::getline;
using std::sort;
using std::string;
using std::vector;

/*
[导读]
- 本文件是学习系统的主闭环：生成待复习列表、执行复习、更新状态、写入复习日志。

[对应流程图]
- Card/WrongQuestion -> ReviewTask -> 用户评分 -> algo_sm2 -> 回写 Card/WrongQuestion -> ReviewLog。

[输入输出]
- 输入：当前用户的有效卡片/错题、今天日期、用户评分 1/2/3。
- 输出：新的 mastery/intervalDays/nextReviewDate、review_logs.txt、控制台复习结果。

[学习重点]
- ReviewTask 是运行时快照，不落盘；真正复习前还要按 itemId/currentUserId/active 再查一次对象。
- 复习日志只用于审计和统计，不反向恢复卡片/错题当前状态。
- 复习状态更新分两步：先调用算法层纯函数，再由本文件把结果写回全局容器并保存。

[易错点]
- 卡片和错题共用同一套评分语义，不能让 ReviewLog.result 出现两套含义。
- 今日任务列表是生成时快照，用户进入复习会话后仍可能遇到对象已删除/不可见，所以复习前要二次校验。
- addReviewLog 要记录旧值和新值，旧值必须在调用算法前保存。

[实验]
- 修改 calcTaskPriority() 中 wrongBonus 的值，观察今日复习列表中错题和卡片排序如何变化。
- 修改复习评分 1/2/3 的含义时，要同时改 algo_sm2.cpp、测试、review.cpp 显示文案和统计解释。
*/

// ================================================================
// 今日复习任务生成
// ================================================================

// 优先级公式：priority = overdueDays * 10 + wrongBonus + (100 - mastery)
// wrongBonus: 错题 = 20, 卡片 = 0
// 次级排序：dueDate 更早优先
// [公式对应] priority = overdueDays * 10 + wrongBonus + (100 - mastery)。
static int calcTaskPriority(const string& itemType, const string& dueDate, int mastery) {
    // 获取今天日期，作为逾期天数计算基准。
    string today = getTodayDate();

    // overdueDays = 今天 - 到期日；正数表示已经逾期。
    int overdueDays = daysBetween(dueDate, today);

    // 未到期理论上不会进入今日任务，但这里仍做保护，避免优先级出现负逾期贡献。
    if (overdueDays < 0) overdueDays = 0;

    // 错题包含明确错误经历，复习优先级高于普通卡片；10 和 20 是产品排序权重，需与测试同步维护。
    int wrongBonus = (itemType == "wrong") ? 20 : 0;

    // 掌握度越低，100 - mastery 越大，优先级越高。
    return overdueDays * 10 + wrongBonus + (100 - mastery);
}

// [输入输出] 当前用户 Card/WrongQuestion -> 已按优先级排序的 ReviewTask 列表。
vector<ReviewTask> generateTodayTasks() {
    // 保存运行时任务列表；ReviewTask 不会写入文件。
    vector<ReviewTask> tasks;

    // 今日日期用于判断 nextReviewDate 是否到期。
    string today = getTodayDate();

    // 只收集当前用户有效记录，保证多用户数据隔离不会被复习列表绕过。
    // 第一段：扫描知识卡片。
    for (size_t i = 0; i < cards.size(); ++i) {
        const Card& c = cards[i];

        // 跳过其他用户和逻辑删除的卡片。
        if (c.userId != currentUserId || !c.active) continue;

        // nextReviewDate <= today 表示今天应复习或已经逾期。
        if (compareDate(c.nextReviewDate, today) <= 0) {
            // 创建一条复习任务快照。
            ReviewTask t;

            // itemId 保存 cardId，不保存 cards 下标，避免容器重排影响任务身份。
            t.itemId   = c.cardId;

            // itemType 区分后续按卡片还是错题执行复习。
            t.itemType = "card";

            // 展示用学科。
            t.subject  = c.subject;

            // 展示用标题。
            t.title    = c.title;

            // 到期日期，用于列表展示和次级排序。
            t.dueDate  = c.nextReviewDate;

            // 计算排序优先级。
            t.priority = calcTaskPriority("card", c.nextReviewDate, c.mastery);

            // 加入今日任务列表。
            tasks.push_back(t);
        }
    }

    // 第二段：扫描错题。
    for (size_t i = 0; i < wrongs.size(); ++i) {
        const WrongQuestion& w = wrongs[i];

        // 跳过其他用户和逻辑删除的错题。
        if (w.userId != currentUserId || !w.active) continue;

        // nextReviewDate <= today 表示今天应复习或已经逾期。
        if (compareDate(w.nextReviewDate, today) <= 0) {
            // 创建一条复习任务快照。
            ReviewTask t;

            // itemId 保存 wrongId，不保存 wrongs 下标。
            t.itemId   = w.wrongId;

            // 标记为错题任务。
            t.itemType = "wrong";

            // 展示用学科。
            t.subject  = w.subject;

            // 错题题目可能很长，列表里截断成 20 字符左右。
            t.title    = w.question.size() > 20
                         ? w.question.substr(0, 17) + "..."
                         : w.question;

            // 到期日期。
            t.dueDate  = w.nextReviewDate;

            // 错题会获得 wrongBonus，因此同等条件下优先级更高。
            t.priority = calcTaskPriority("wrong", w.nextReviewDate, w.mastery);

            // 加入今日任务列表。
            tasks.push_back(t);
        }
    }

    // 排序规则影响用户每天先看到什么内容，不能只按容器插入顺序展示。
    sort(tasks.begin(), tasks.end(), [](const ReviewTask& a, const ReviewTask& b) {
        // 第一排序条件：priority 越大越靠前。
        if (a.priority != b.priority) return a.priority > b.priority;

        // 第二排序条件：优先级相同时，到期更早的任务靠前。
        return compareDate(a.dueDate, b.dueDate) < 0;
    });

    // 返回排序后的今日任务。
    return tasks;
}

// ================================================================
// 展示今日待复习列表
// ================================================================

void showTodayTasks() {
    // 进入今日待复习列表页面。
    clearScreen();
    cout << "==============================\n";
    cout << "     今日待复习列表\n";
    cout << "==============================\n";

    // 现场生成今日任务，保证展示的是当前最新数据。
    vector<ReviewTask> tasks = generateTodayTasks();

    // 没有到期内容时直接返回。
    if (tasks.empty()) {
        cout << "今天没有需要复习的内容，太棒了！\n";
        pauseScreen();
        return;
    }

    // 输出任务总数。
    cout << "共 " << tasks.size() << " 项待复习：\n\n";

    // 按排序后的任务顺序输出列表。
    for (size_t i = 0; i < tasks.size(); ++i) {
        const ReviewTask& t = tasks[i];

        // 根据 itemType 显示卡片/错题标签。
        string typeLabel = (t.itemType == "card") ? "[卡片]" : "[错题]";

        // 展示任务身份、标题、学科、到期日和优先级，便于理解排序原因。
        cout << "  " << (i + 1) << ". " << typeLabel
             << " [" << t.itemId << "] "
             << t.title
             << " | " << t.subject
             << " | 到期:" << t.dueDate
             << " | 优先级:" << t.priority
             << "\n";
    }
    pauseScreen();
}

// ================================================================
// 复习单个卡片
// ================================================================

// [学习重点] 单张卡片复习会同时更新卡片状态和追加一条 ReviewLog。
static void reviewOneCard(int cardIndex) {
    // cardIndex 是 cards 全局容器下标；调用方已在复习前做 currentUserId/active 校验。
    Card& c = cards[cardIndex];

    // 先展示卡片正面。
    cout << "------------------------------\n";
    cout << "【卡片复习】\n";
    cout << "学科：" << c.subject << "\n";
    cout << "标题：" << c.title << "\n";
    cout << "问题：" << c.front << "\n";
    cout << "------------------------------\n";
    cout << "（思考后按回车查看答案）";

    // 等待用户自测完成。
    string dummy;
    getline(cin, dummy);

    // 再展示卡片背面。
    cout << "\n答案：" << c.back << "\n";
    cout << "------------------------------\n";

    // 复习反馈只允许 1/2/3，避免旧版 0/1/2 分值混入新算法。
    int result = -1;
    while (true) {
        cout << "请评价掌握程度 (1=忘记  2=模糊  3=记牢)：";
        string line;

        // 输入流关闭时直接结束本张卡片复习。
        if (!getline(cin, line)) return;

        // 必须完整解析为 1~3 的整数。
        if (parseInt(line, result) && result >= 1 && result <= 3) {
            break;
        }
        cout << "输入无效，请输入 1、2 或 3。\n";
    }

    // 先保存旧值，日志需要记录复习前后的状态差异。
    int oldInterval = c.intervalDays;
    int oldMastery  = c.mastery;

    // 算法层只返回计算结果，本模块负责把结果应用到具体卡片并持久化。
    // 输入：旧掌握度、旧间隔、旧连续答对次数、用户评分。
    ReviewResult rr = calculateNextReview(c.mastery, c.intervalDays, c.correctStreak, result);

    // 回写新的掌握度。
    c.mastery       = rr.newMastery;

    // 回写新的复习间隔。
    c.intervalDays  = rr.newInterval;

    // 回写新的连续答对次数。
    c.correctStreak = rr.correctStreak;

    // 使用今天作为本次复习日期。
    string today = getTodayDate();

    // 复习次数加 1。
    c.reviewCount   += 1;

    // 最近复习日期更新为今天。
    c.lastReviewDate = today;

    // 下次复习日期 = 今天 + 新间隔。
    c.nextReviewDate = addDays(today, c.intervalDays);

    // 追加复习日志，记录本次评分和新旧状态。
    addReviewLog(c.cardId, "card", result, oldInterval, c.intervalDays, oldMastery, c.mastery);

    // 保存卡片状态。
    saveCards();

    // 展示更新结果
    // 把数字评分转成用户可读文案。
    string resultLabel = (result == 1) ? "忘记" : (result == 2) ? "模糊" : "记牢";

    // 输出本次复习带来的状态变化。
    cout << "\n复习结果：" << resultLabel << "\n";
    cout << "掌握度：" << oldMastery << " → " << c.mastery << "\n";
    cout << "间  隔：" << oldInterval << " → " << c.intervalDays << " 天\n";
    cout << "下次复习：" << c.nextReviewDate << "\n";
}

// ================================================================
// 复习单个错题
// ================================================================

// [学习重点] 单道错题复习复用卡片同一套算法，保证两类材料调度口径一致。
static void reviewOneWrong(int wrongIndex) {
    // wrongIndex 是 wrongs 全局容器下标；调用方已在复习前做 currentUserId/active 校验。
    WrongQuestion& w = wrongs[wrongIndex];

    // 先展示错题题目和用户当时的错误答案。
    cout << "------------------------------\n";
    cout << "【错题复习】\n";
    cout << "学科：" << w.subject << "\n";
    cout << "题目：" << w.question << "\n";
    cout << "你的错误答案：" << w.wrongAnswer << "\n";

    // 如果有错因分析，也一起展示，帮助用户先回忆错误原因。
    if (!w.reason.empty()) {
        cout << "错因分析：" << w.reason << "\n";
    }
    cout << "------------------------------\n";
    cout << "（思考后按回车查看正确答案）";

    // 等待用户自测完成。
    string dummy;
    getline(cin, dummy);

    // 再展示正确答案。
    cout << "\n正确答案：" << w.correctAnswer << "\n";
    cout << "------------------------------\n";

    // 与卡片保持同一评分语义，保证 ReviewLog.result 可以统一统计。
    int result = -1;
    while (true) {
        cout << "请评价掌握程度 (1=忘记  2=模糊  3=记牢)：";
        string line;
        getline(cin, line);

        // 必须完整解析为 1~3 的整数。
        if (parseInt(line, result) && result >= 1 && result <= 3) {
            break;
        }
        cout << "输入无效，请输入 1、2 或 3。\n";
    }

    // 旧值用于审计日志，不能在调用算法后再读取。
    int oldInterval = w.intervalDays;
    int oldMastery  = w.mastery;

    // 错题复用同一套间隔算法，避免两类复习材料出现不可解释的调度差异。
    // 输入：旧掌握度、旧间隔、旧连续答对次数、用户评分。
    ReviewResult rr = calculateNextReview(w.mastery, w.intervalDays, w.correctStreak, result);

    // 回写新的掌握度。
    w.mastery       = rr.newMastery;

    // 回写新的复习间隔。
    w.intervalDays  = rr.newInterval;

    // 回写新的连续答对次数。
    w.correctStreak = rr.correctStreak;

    // 使用今天作为本次复习日期。
    string today = getTodayDate();

    // 复习次数加 1。
    w.reviewCount   += 1;

    // 最近复习日期更新为今天。
    w.lastReviewDate = today;

    // 下次复习日期 = 今天 + 新间隔。
    w.nextReviewDate = addDays(today, w.intervalDays);

    // 追加复习日志，记录本次评分和新旧状态。
    addReviewLog(w.wrongId, "wrong", result, oldInterval, w.intervalDays, oldMastery, w.mastery);

    // 保存错题状态。
    saveWrongs();

    // 展示更新结果
    // 把数字评分转成用户可读文案。
    string resultLabel = (result == 1) ? "忘记" : (result == 2) ? "模糊" : "记牢";

    // 输出本次复习带来的状态变化。
    cout << "\n复习结果：" << resultLabel << "\n";
    cout << "掌握度：" << oldMastery << " → " << w.mastery << "\n";
    cout << "间  隔：" << oldInterval << " → " << w.intervalDays << " 天\n";
    cout << "下次复习：" << w.nextReviewDate << "\n";
}

// ================================================================
// 开始复习会话
// ================================================================

void startReviewSession() {
    // 进入复习会话页面。
    clearScreen();
    cout << "==============================\n";
    cout << "       开始复习\n";
    cout << "==============================\n";

    // 会话开始时生成一份今日任务快照。
    vector<ReviewTask> tasks = generateTodayTasks();

    // 没有任务时直接返回。
    if (tasks.empty()) {
        cout << "今天没有需要复习的内容，太棒了！\n";
        pauseScreen();
        return;
    }

    // 告知用户本轮会话规模。
    cout << "共 " << tasks.size() << " 项待复习，现在开始！\n\n";

    // 记录本轮实际完成了多少项；跳过不可用对象不计入完成。
    int completed = 0;

    // 按优先级顺序逐项复习。
    for (size_t ti = 0; ti < tasks.size(); ++ti) {
        const ReviewTask& task = tasks[ti];

        // 输出当前进度。
        cout << "\n========== 第 " << (ti + 1) << "/" << tasks.size() << " 项 ==========\n\n";

        // 根据任务类型分别查找卡片或错题。
        if (task.itemType == "card") {
            // 任务生成后记录可能被删除或切换用户；复习前按 ID 重新校验可见性。
            int idx = -1;

            // 用 cardId 查找当前仍可见的目标卡片。
            for (size_t i = 0; i < cards.size(); ++i) {
                if (cards[i].cardId == task.itemId &&
                    cards[i].userId == currentUserId &&
                    cards[i].active) {
                    // 找到后保存 cards 下标。
                    idx = static_cast<int>(i);
                    break;
                }
            }

            // 找不到说明任务快照已经过期，直接跳过。
            if (idx == -1) {
                cout << "卡片 [" << task.itemId << "] 已不可用，跳过。\n";
            } else {
                // 执行单张卡片复习。
                reviewOneCard(idx);

                // 成功复习后完成数加 1。
                completed++;
            }
        } else {
            // 错题任务同样要按 wrongId 二次查找当前仍可见的对象。
            int idx = -1;

            // 用 wrongId 查找目标错题。
            for (size_t i = 0; i < wrongs.size(); ++i) {
                if (wrongs[i].wrongId == task.itemId &&
                    wrongs[i].userId == currentUserId &&
                    wrongs[i].active) {
                    // 找到后保存 wrongs 下标。
                    idx = static_cast<int>(i);
                    break;
                }
            }

            // 找不到说明任务快照已经过期，直接跳过。
            if (idx == -1) {
                cout << "错题 [" << task.itemId << "] 已不可用，跳过。\n";
            } else {
                // 执行单道错题复习。
                reviewOneWrong(idx);

                // 成功复习后完成数加 1。
                completed++;
            }
        }

        // 已完成项即时保存；中途退出不会回滚前面已经写入的复习状态和日志。
        if (ti < tasks.size() - 1) {
            // 每项之间允许用户退出本轮复习。
            cout << "\n继续下一项？(回车继续 / q 退出复习)：";
            string line;
            getline(cin, line);
            line = trim(line);

            // q/Q 表示中途结束会话；已经完成的项保持保存。
            if (line == "q" || line == "Q") {
                cout << "已退出复习。\n";
                break;
            }
        }
    }

    // 输出本轮实际完成数量。
    cout << "\n本次复习完成 " << completed << " 项。\n";
    pauseScreen();
}

// ================================================================
// 复习日志
// ================================================================

void addReviewLog(int itemId, const string& itemType, int result,
                  int oldInterval, int newInterval,
                  int oldMastery, int newMastery) {
    // 创建一条新的复习日志。
    ReviewLog lg;

    // 分配全局唯一日志 ID。
    lg.logId       = getNextLogId();

    // 日志归属当前登录用户。
    lg.userId      = currentUserId;

    // 被复习对象的业务 ID：cardId 或 wrongId。
    lg.itemId      = itemId;

    // 被复习对象类型：只能是 "card" 或 "wrong"。
    lg.itemType    = itemType;

    // 复习日期使用系统本地今天。
    lg.reviewDate  = getTodayDate();

    // 用户评分：1=忘记，2=模糊，3=记牢。
    lg.result      = result;

    // 记录复习前间隔。
    lg.oldInterval = oldInterval;

    // 记录复习后间隔。
    lg.newInterval = newInterval;

    // 记录复习前掌握度。
    lg.oldMastery  = oldMastery;

    // 记录复习后掌握度。
    lg.newMastery  = newMastery;

    // 追加日志后立即保存，降低复习完成后异常退出导致审计记录丢失的概率。
    logs.push_back(lg);

    // 保存到 review_logs.txt。
    saveLogs();
}

void showReviewHistory() {
    // 进入复习历史页面。
    clearScreen();
    cout << "==============================\n";
    cout << "       复习历史\n";
    cout << "==============================\n";

    // 日志按用户过滤，但不校验 itemId 当前是否仍可见；历史记录应保留被删除对象的复习轨迹。
    // 这里保存 logs 下标，不复制日志对象。
    vector<int> userLogIndices;

    // 扫描全部日志。
    for (size_t i = 0; i < logs.size(); ++i) {
        // 只展示当前用户的复习记录。
        if (logs[i].userId == currentUserId) {
            userLogIndices.push_back(static_cast<int>(i));
        }
    }

    // 没有记录时直接返回。
    if (userLogIndices.empty()) {
        cout << "暂无复习记录。\n";
        pauseScreen();
        return;
    }

    // 历史记录按最新在前展示。
    cout << "共 " << userLogIndices.size() << " 条复习记录（最新在前）：\n\n";

    // 已显示条数，用于分页和最终统计。
    int showCount = 0;

    // userLogIndices 按原日志顺序收集，这里倒序遍历，实现最新在前。
    for (int i = static_cast<int>(userLogIndices.size()) - 1; i >= 0; --i) {
        const ReviewLog& lg = logs[userLogIndices[i]];

        // 日志对象类型显示文案。
        string typeLabel = (lg.itemType == "card") ? "卡片" : "错题";

        // 数字评分显示文案；未知值用于兼容旧数据或损坏数据。
        string resultLabel = (lg.result == 1) ? "忘记" : (lg.result == 2) ? "模糊" : (lg.result == 3) ? "记牢" : "未知";

        // 输出单条日志摘要。
        cout << "  [" << lg.logId << "] "
             << lg.reviewDate
             << " | " << typeLabel << "#" << lg.itemId
             << " | 结果:" << resultLabel
             << " | 掌握:" << lg.oldMastery << "→" << lg.newMastery
             << " | 间隔:" << lg.oldInterval << "→" << lg.newInterval << "天"
             << "\n";

        // 统计已显示条数。
        showCount++;

        // 固定 20 条分页，防止长期使用后一次性刷屏影响控制台可读性。
        if (showCount % 20 == 0 && i > 0) {
            cout << "\n按回车查看更多，输入 q 返回：";
            string line;
            getline(cin, line);

            // q/Q 提前结束历史查看。
            if (trim(line) == "q" || trim(line) == "Q") break;
        }
    }

    // 输出本次实际显示数量。
    cout << "\n共显示 " << showCount << " 条记录。\n";
    pauseScreen();
}

// ================================================================
// 复习子菜单
// ================================================================

void showReviewMenu() {
    // 复习子菜单循环，直到用户选择 0 返回主菜单。
    while (true) {
        // 每轮菜单前清屏。
        clearScreen();
        cout << "==============================\n";
        cout << "       今日复习\n";
        cout << "==============================\n";
        cout << "1. 查看今日待复习列表\n";
        cout << "2. 开始复习\n";
        cout << "3. 查看复习历史\n";
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

        // 根据菜单项分发到具体业务函数。
        switch (choice) {
            // 查看今日待复习列表。
            case 1: showTodayTasks();      break;

            // 开始复习会话。
            case 2: startReviewSession();  break;

            // 查看复习历史。
            case 3: showReviewHistory();   break;

            // 返回主菜单。
            case 0: return;

            default:
                // 其他整数不是合法菜单项。
                cout << "菜单选项不存在。\n";
                pauseScreen();
        }
    }
}
