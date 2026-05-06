#include "review.h"
#include "globals.h"
#include "storage.h"
#include "utils.h"
#include <iostream>
#include <vector>
#include "algo_sm2.h"
#include <algorithm>

/*
模块职责：
- 将卡片和错题统一抽象为 ReviewTask，完成“生成待复习列表 -> 用户复习 -> 更新状态 -> 写日志”的闭环。

关键约束：
- generateTodayTasks 返回的是当前时刻快照；真正复习前仍会再次按 itemId/currentUserId/active 查找对象。
- 复习日志用于审计和统计，不作为恢复卡片/错题当前状态的来源。
*/

// ================================================================
// 今日复习任务生成
// ================================================================

// 优先级公式：priority = overdueDays * 10 + wrongBonus + (100 - mastery)
// wrongBonus: 错题 = 20, 卡片 = 0
// 次级排序：dueDate 更早优先
static int calcTaskPriority(const string& itemType, const string& dueDate, int mastery) {
    string today = getTodayDate();
    int overdueDays = daysBetween(dueDate, today);
    if (overdueDays < 0) overdueDays = 0;

    // 错题包含明确错误经历，复习优先级高于普通卡片；10 和 20 是产品排序权重，需与测试同步维护。
    int wrongBonus = (itemType == "wrong") ? 20 : 0;
    return overdueDays * 10 + wrongBonus + (100 - mastery);
}

vector<ReviewTask> generateTodayTasks() {
    vector<ReviewTask> tasks;
    string today = getTodayDate();

    // 只收集当前用户有效记录，保证多用户数据隔离不会被复习列表绕过。
    for (size_t i = 0; i < cards.size(); ++i) {
        const Card& c = cards[i];
        if (c.userId != currentUserId || !c.active) continue;
        if (compareDate(c.nextReviewDate, today) <= 0) {
            ReviewTask t;
            t.itemId   = c.cardId;
            t.itemType = "card";
            t.subject  = c.subject;
            t.title    = c.title;
            t.dueDate  = c.nextReviewDate;
            t.priority = calcTaskPriority("card", c.nextReviewDate, c.mastery);
            tasks.push_back(t);
        }
    }

    for (size_t i = 0; i < wrongs.size(); ++i) {
        const WrongQuestion& w = wrongs[i];
        if (w.userId != currentUserId || !w.active) continue;
        if (compareDate(w.nextReviewDate, today) <= 0) {
            ReviewTask t;
            t.itemId   = w.wrongId;
            t.itemType = "wrong";
            t.subject  = w.subject;
            t.title    = w.question.size() > 20
                         ? w.question.substr(0, 17) + "..."
                         : w.question;
            t.dueDate  = w.nextReviewDate;
            t.priority = calcTaskPriority("wrong", w.nextReviewDate, w.mastery);
            tasks.push_back(t);
        }
    }

    // 排序规则影响用户每天先看到什么内容，不能只按容器插入顺序展示。
    sort(tasks.begin(), tasks.end(), [](const ReviewTask& a, const ReviewTask& b) {
        if (a.priority != b.priority) return a.priority > b.priority;
        return compareDate(a.dueDate, b.dueDate) < 0;
    });

    return tasks;
}

// ================================================================
// 展示今日待复习列表
// ================================================================

void showTodayTasks() {
    clearScreen();
    cout << "==============================\n";
    cout << "     今日待复习列表\n";
    cout << "==============================\n";

    vector<ReviewTask> tasks = generateTodayTasks();
    if (tasks.empty()) {
        cout << "今天没有需要复习的内容，太棒了！\n";
        pauseScreen();
        return;
    }

    cout << "共 " << tasks.size() << " 项待复习：\n\n";
    for (size_t i = 0; i < tasks.size(); ++i) {
        const ReviewTask& t = tasks[i];
        string typeLabel = (t.itemType == "card") ? "[卡片]" : "[错题]";
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

static void reviewOneCard(int cardIndex) {
    Card& c = cards[cardIndex];

    cout << "------------------------------\n";
    cout << "【卡片复习】\n";
    cout << "学科：" << c.subject << "\n";
    cout << "标题：" << c.title << "\n";
    cout << "问题：" << c.front << "\n";
    cout << "------------------------------\n";
    cout << "（思考后按回车查看答案）";
    string dummy;
    getline(cin, dummy);

    cout << "\n答案：" << c.back << "\n";
    cout << "------------------------------\n";

    // 复习反馈只允许 1/2/3，避免旧版 0/1/2 分值混入新算法。
    int result = -1;
    while (true) {
        cout << "请评价掌握程度 (1=忘记  2=模糊  3=记牢)：";
        string line;
        if (!getline(cin, line)) return;
        if (parseInt(line, result) && result >= 1 && result <= 3) {
            break;
        }
        cout << "输入无效，请输入 1、2 或 3。\n";
    }

    // 先保存旧值，日志需要记录复习前后的状态差异。
    int oldInterval = c.intervalDays;
    int oldMastery  = c.mastery;

    // 算法层只返回计算结果，本模块负责把结果应用到具体卡片并持久化。
    ReviewResult rr = calculateNextReview(c.mastery, c.intervalDays, c.correctStreak, result);
    c.mastery       = rr.newMastery;
    c.intervalDays  = rr.newInterval;
    c.correctStreak = rr.correctStreak;

    string today = getTodayDate();
    c.reviewCount   += 1;
    c.lastReviewDate = today;
    c.nextReviewDate = addDays(today, c.intervalDays);

    addReviewLog(c.cardId, "card", result, oldInterval, c.intervalDays, oldMastery, c.mastery);

    saveCards();

    // 展示更新结果
    string resultLabel = (result == 1) ? "忘记" : (result == 2) ? "模糊" : "记牢";
    cout << "\n复习结果：" << resultLabel << "\n";
    cout << "掌握度：" << oldMastery << " → " << c.mastery << "\n";
    cout << "间  隔：" << oldInterval << " → " << c.intervalDays << " 天\n";
    cout << "下次复习：" << c.nextReviewDate << "\n";
}

// ================================================================
// 复习单个错题
// ================================================================

static void reviewOneWrong(int wrongIndex) {
    WrongQuestion& w = wrongs[wrongIndex];

    cout << "------------------------------\n";
    cout << "【错题复习】\n";
    cout << "学科：" << w.subject << "\n";
    cout << "题目：" << w.question << "\n";
    cout << "你的错误答案：" << w.wrongAnswer << "\n";
    if (!w.reason.empty()) {
        cout << "错因分析：" << w.reason << "\n";
    }
    cout << "------------------------------\n";
    cout << "（思考后按回车查看正确答案）";
    string dummy;
    getline(cin, dummy);

    cout << "\n正确答案：" << w.correctAnswer << "\n";
    cout << "------------------------------\n";

    // 与卡片保持同一评分语义，保证 ReviewLog.result 可以统一统计。
    int result = -1;
    while (true) {
        cout << "请评价掌握程度 (1=忘记  2=模糊  3=记牢)：";
        string line;
        getline(cin, line);
        if (parseInt(line, result) && result >= 1 && result <= 3) {
            break;
        }
        cout << "输入无效，请输入 1、2 或 3。\n";
    }

    // 旧值用于审计日志，不能在调用算法后再读取。
    int oldInterval = w.intervalDays;
    int oldMastery  = w.mastery;

    // 错题复用同一套间隔算法，避免两类复习材料出现不可解释的调度差异。
    ReviewResult rr = calculateNextReview(w.mastery, w.intervalDays, w.correctStreak, result);
    w.mastery       = rr.newMastery;
    w.intervalDays  = rr.newInterval;
    w.correctStreak = rr.correctStreak;

    string today = getTodayDate();
    w.reviewCount   += 1;
    w.lastReviewDate = today;
    w.nextReviewDate = addDays(today, w.intervalDays);

    addReviewLog(w.wrongId, "wrong", result, oldInterval, w.intervalDays, oldMastery, w.mastery);

    saveWrongs();

    // 展示更新结果
    string resultLabel = (result == 1) ? "忘记" : (result == 2) ? "模糊" : "记牢";
    cout << "\n复习结果：" << resultLabel << "\n";
    cout << "掌握度：" << oldMastery << " → " << w.mastery << "\n";
    cout << "间  隔：" << oldInterval << " → " << w.intervalDays << " 天\n";
    cout << "下次复习：" << w.nextReviewDate << "\n";
}

// ================================================================
// 开始复习会话
// ================================================================

void startReviewSession() {
    clearScreen();
    cout << "==============================\n";
    cout << "       开始复习\n";
    cout << "==============================\n";

    vector<ReviewTask> tasks = generateTodayTasks();
    if (tasks.empty()) {
        cout << "今天没有需要复习的内容，太棒了！\n";
        pauseScreen();
        return;
    }

    cout << "共 " << tasks.size() << " 项待复习，现在开始！\n\n";

    int completed = 0;
    for (size_t ti = 0; ti < tasks.size(); ++ti) {
        const ReviewTask& task = tasks[ti];

        cout << "\n========== 第 " << (ti + 1) << "/" << tasks.size() << " 项 ==========\n\n";

        if (task.itemType == "card") {
            // 任务生成后记录可能被删除或切换用户；复习前按 ID 重新校验可见性。
            int idx = -1;
            for (size_t i = 0; i < cards.size(); ++i) {
                if (cards[i].cardId == task.itemId &&
                    cards[i].userId == currentUserId &&
                    cards[i].active) {
                    idx = static_cast<int>(i);
                    break;
                }
            }
            if (idx == -1) {
                cout << "卡片 [" << task.itemId << "] 已不可用，跳过。\n";
            } else {
                reviewOneCard(idx);
                completed++;
            }
        } else {
            int idx = -1;
            for (size_t i = 0; i < wrongs.size(); ++i) {
                if (wrongs[i].wrongId == task.itemId &&
                    wrongs[i].userId == currentUserId &&
                    wrongs[i].active) {
                    idx = static_cast<int>(i);
                    break;
                }
            }
            if (idx == -1) {
                cout << "错题 [" << task.itemId << "] 已不可用，跳过。\n";
            } else {
                reviewOneWrong(idx);
                completed++;
            }
        }

        // 已完成项即时保存；中途退出不会回滚前面已经写入的复习状态和日志。
        if (ti < tasks.size() - 1) {
            cout << "\n继续下一项？(回车继续 / q 退出复习)：";
            string line;
            getline(cin, line);
            line = trim(line);
            if (line == "q" || line == "Q") {
                cout << "已退出复习。\n";
                break;
            }
        }
    }

    cout << "\n本次复习完成 " << completed << " 项。\n";
    pauseScreen();
}

// ================================================================
// 复习日志
// ================================================================

void addReviewLog(int itemId, const string& itemType, int result,
                  int oldInterval, int newInterval,
                  int oldMastery, int newMastery) {
    ReviewLog lg;
    lg.logId       = getNextLogId();
    lg.userId      = currentUserId;
    lg.itemId      = itemId;
    lg.itemType    = itemType;
    lg.reviewDate  = getTodayDate();
    lg.result      = result;
    lg.oldInterval = oldInterval;
    lg.newInterval = newInterval;
    lg.oldMastery  = oldMastery;
    lg.newMastery  = newMastery;

    // 追加日志后立即保存，降低复习完成后异常退出导致审计记录丢失的概率。
    logs.push_back(lg);
    saveLogs();
}

void showReviewHistory() {
    clearScreen();
    cout << "==============================\n";
    cout << "       复习历史\n";
    cout << "==============================\n";

    // 日志按用户过滤，但不校验 itemId 当前是否仍可见；历史记录应保留被删除对象的复习轨迹。
    vector<int> userLogIndices;
    for (size_t i = 0; i < logs.size(); ++i) {
        if (logs[i].userId == currentUserId) {
            userLogIndices.push_back(static_cast<int>(i));
        }
    }

    if (userLogIndices.empty()) {
        cout << "暂无复习记录。\n";
        pauseScreen();
        return;
    }

    cout << "共 " << userLogIndices.size() << " 条复习记录（最新在前）：\n\n";

    int showCount = 0;
    for (int i = static_cast<int>(userLogIndices.size()) - 1; i >= 0; --i) {
        const ReviewLog& lg = logs[userLogIndices[i]];
        string typeLabel = (lg.itemType == "card") ? "卡片" : "错题";
        string resultLabel = (lg.result == 1) ? "忘记" : (lg.result == 2) ? "模糊" : (lg.result == 3) ? "记牢" : "未知";

        cout << "  [" << lg.logId << "] "
             << lg.reviewDate
             << " | " << typeLabel << "#" << lg.itemId
             << " | 结果:" << resultLabel
             << " | 掌握:" << lg.oldMastery << "→" << lg.newMastery
             << " | 间隔:" << lg.oldInterval << "→" << lg.newInterval << "天"
             << "\n";

        showCount++;
        // 固定 20 条分页，防止长期使用后一次性刷屏影响控制台可读性。
        if (showCount % 20 == 0 && i > 0) {
            cout << "\n按回车查看更多，输入 q 返回：";
            string line;
            getline(cin, line);
            if (trim(line) == "q" || trim(line) == "Q") break;
        }
    }

    cout << "\n共显示 " << showCount << " 条记录。\n";
    pauseScreen();
}

// ================================================================
// 复习子菜单
// ================================================================

void showReviewMenu() {
    while (true) {
        clearScreen();
        cout << "==============================\n";
        cout << "       今日复习\n";
        cout << "==============================\n";
        cout << "1. 查看今日待复习列表\n";
        cout << "2. 开始复习\n";
        cout << "3. 查看复习历史\n";
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
            case 1: showTodayTasks();      break;
            case 2: startReviewSession();  break;
            case 3: showReviewHistory();   break;
            case 0: return;
            default:
                cout << "菜单选项不存在。\n";
                pauseScreen();
        }
    }
}
