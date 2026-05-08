#include "maintenance.h"
#include "globals.h"
#include "utils.h"
#include "storage.h"
#include "tui.h"
#include <iostream>
#include <string>
#include <algorithm>
#include <map>
#include <set>
#include <vector>

using std::cout;
using std::cin;
using std::getline;
using std::map;
using std::remove_if;
using std::string;
using std::to_string;
using std::vector;

/*
模块职责：
- 维护逻辑删除后的回收站体验，并对数据文件中的安全可修复问题做检查和归正。

关键约束：
- 自动修复只处理不会改变用户意图的问题：失效关联、字段范围、非法日期兜底。
- 重复 ID 可能需要人工判断合并或删除，当前只报告，不自动改写。
*/

// ========== 表现层映射 ==========
// 回收站操作也使用展示序号，因此需要独立维护“已删除列表”的下标映射。
static vector<int> currentDeletedCardMap;
static vector<int> currentDeletedWrongMap;

// 表示：一次数据一致性扫描的汇总结果。
// 注意：invalidLinkedWrongIndexes 存 wrongs 容器下标，repair 阶段依赖当前容器未被重排。
struct DataConsistencyReport {
    int issueCount = 0;
    int autoFixableCount = 0;
    int duplicateIssueCount = 0;
    int fieldIssueCount = 0;
    vector<int> invalidLinkedWrongIndexes;
};

static int findCardIndexById(int cardId) {
    for (size_t i = 0; i < cards.size(); ++i) {
        if (cards[i].cardId == cardId) {
            return static_cast<int>(i);
        }
    }
    return -1;
}

static int clampToRange(int value, int low, int high) {
    if (value < low) return low;
    if (value > high) return high;
    return value;
}

static string fallbackReviewDate(const string& createDate) {
    // 下次复习日期非法时优先回退到有效创建日期，保留“创建后可复习”的业务语义。
    return isValidDate(createDate) ? createDate : getTodayDate();
}

static bool isUserInScope(int recordUserId, int userIdFilter) {
    return userIdFilter == -1 || recordUserId == userIdFilter;
}

static string describeUserScope(int userIdFilter) {
    return userIdFilter == -1 ? "全部用户" : ("指定用户 " + to_string(userIdFilter));
}

static int printDuplicateIds(const map<int, int>& counts, const string& label) {
    int duplicateCount = 0;
    for (const auto& item : counts) {
        if (item.second > 1) {
            duplicateCount++;
        }
    }
    if (duplicateCount > 0) {
        cout << "\n[重复记录] " << label << "："
             << duplicateCount << " 组重复记录（具体底层值已隐藏）。\n";
    }
    return duplicateCount;
}

static DataConsistencyReport inspectDataConsistency(int userIdFilter) {
    DataConsistencyReport report;
    map<int, int> cardIdCounts;
    map<int, int> wrongIdCounts;

    for (const Card& c : cards) {
        cardIdCounts[c.cardId]++;
    }
    for (const WrongQuestion& w : wrongs) {
        wrongIdCounts[w.wrongId]++;
    }

    report.duplicateIssueCount += printDuplicateIds(cardIdCounts, "知识卡片");
    report.duplicateIssueCount += printDuplicateIds(wrongIdCounts, "错题");
    report.issueCount += report.duplicateIssueCount;

    cout << "\n[错题关联检查]\n";
    for (size_t i = 0; i < wrongs.size(); ++i) {
        const WrongQuestion& w = wrongs[i];
        if (!isUserInScope(w.userId, userIdFilter) || w.linkedCardId == -1) continue;

        int cardIndex = findCardIndexById(w.linkedCardId);
        bool valid = false;
        string reason;
        if (cardIndex == -1) {
            reason = "目标卡片不存在";
        } else if (cards[cardIndex].userId != w.userId) {
            reason = "目标卡片属于其他用户";
        } else if (!cards[cardIndex].active) {
            reason = "目标卡片已被逻辑删除，知识卡片管理中不可见";
        } else {
            valid = true;
        }

        if (!valid) {
            // 失效关联可安全重置为 -1，用户随后可重新执行“错题转知识卡片”。
            cout << "  第 " << (i + 1) << " 条错题关联的卡片无效：" << reason << "\n";
            report.invalidLinkedWrongIndexes.push_back(static_cast<int>(i));
            report.issueCount++;
            report.autoFixableCount++;
        }
    }
    if (report.invalidLinkedWrongIndexes.empty()) {
        cout << "  未发现失效的错题转卡片关联。\n";
    }

    cout << "\n[字段范围检查]\n";
    for (size_t i = 0; i < cards.size(); ++i) {
        const Card& c = cards[i];
        if (!isUserInScope(c.userId, userIdFilter)) continue;
        string recordLabel = "第 " + to_string(i + 1) + " 条卡片";
        if (c.difficulty < 1 || c.difficulty > 5) {
            cout << "  " << recordLabel << " 难度超出 1~5：" << c.difficulty << "\n";
            report.fieldIssueCount++;
            report.autoFixableCount++;
        }
        if (c.mastery < 0 || c.mastery > 100) {
            cout << "  " << recordLabel << " 掌握度超出 0~100：" << c.mastery << "\n";
            report.fieldIssueCount++;
            report.autoFixableCount++;
        }
        if (c.intervalDays < 1) {
            cout << "  " << recordLabel << " 复习间隔小于 1：" << c.intervalDays << "\n";
            report.fieldIssueCount++;
            report.autoFixableCount++;
        }
        if (!isValidDate(c.createDate)) {
            cout << "  " << recordLabel << " 创建日期无效：" << c.createDate << "\n";
            report.fieldIssueCount++;
            report.autoFixableCount++;
        }
        if (!c.lastReviewDate.empty() && !isValidDate(c.lastReviewDate)) {
            cout << "  " << recordLabel << " 最近复习日期无效：" << c.lastReviewDate << "\n";
            report.fieldIssueCount++;
            report.autoFixableCount++;
        }
        if (!isValidDate(c.nextReviewDate)) {
            cout << "  " << recordLabel << " 下次复习日期无效：" << c.nextReviewDate << "\n";
            report.fieldIssueCount++;
            report.autoFixableCount++;
        }
    }

    for (size_t i = 0; i < wrongs.size(); ++i) {
        const WrongQuestion& w = wrongs[i];
        if (!isUserInScope(w.userId, userIdFilter)) continue;
        string recordLabel = "第 " + to_string(i + 1) + " 条错题";
        if (w.mastery < 0 || w.mastery > 100) {
            cout << "  " << recordLabel << " 掌握度超出 0~100：" << w.mastery << "\n";
            report.fieldIssueCount++;
            report.autoFixableCount++;
        }
        if (w.intervalDays < 1) {
            cout << "  " << recordLabel << " 复习间隔小于 1：" << w.intervalDays << "\n";
            report.fieldIssueCount++;
            report.autoFixableCount++;
        }
        if (!isValidDate(w.createDate)) {
            cout << "  " << recordLabel << " 创建日期无效：" << w.createDate << "\n";
            report.fieldIssueCount++;
            report.autoFixableCount++;
        }
        if (!w.lastReviewDate.empty() && !isValidDate(w.lastReviewDate)) {
            cout << "  " << recordLabel << " 最近复习日期无效：" << w.lastReviewDate << "\n";
            report.fieldIssueCount++;
            report.autoFixableCount++;
        }
        if (!isValidDate(w.nextReviewDate)) {
            cout << "  " << recordLabel << " 下次复习日期无效：" << w.nextReviewDate << "\n";
            report.fieldIssueCount++;
            report.autoFixableCount++;
        }
    }
    if (report.fieldIssueCount == 0) {
        cout << "  未发现范围内的字段范围问题。\n";
    }
    report.issueCount += report.fieldIssueCount;
    return report;
}

static void printConsistencySummary(const DataConsistencyReport& report) {
    cout << "\n";
    printTuiSection("检查结果");
    cout << "  发现问题：" << report.issueCount << " 项\n";
    cout << "  可自动修复：" << report.autoFixableCount << " 项\n";
    cout << "  需人工处理：" << (report.issueCount - report.autoFixableCount) << " 项\n";
}

static int repairDataConsistency(const DataConsistencyReport& report, int userIdFilter) {
    int repairedCount = 0;

    for (int idx : report.invalidLinkedWrongIndexes) {
        if (idx >= 0 && idx < static_cast<int>(wrongs.size()) && wrongs[idx].linkedCardId != -1) {
            wrongs[idx].linkedCardId = -1;
            repairedCount++;
        }
    }

    for (Card& c : cards) {
        if (!isUserInScope(c.userId, userIdFilter)) continue;

        int oldDifficulty = c.difficulty;
        int oldMastery = c.mastery;
        int oldInterval = c.intervalDays;
        string oldCreateDate = c.createDate;
        string oldLastReviewDate = c.lastReviewDate;
        string oldNextReviewDate = c.nextReviewDate;

        // 字段修复只做范围钳制和日期兜底，不尝试推断用户原始意图。
        c.difficulty = clampToRange(c.difficulty, 1, 5);
        c.mastery = clampToRange(c.mastery, 0, 100);
        if (c.intervalDays < 1) c.intervalDays = 1;
        if (!isValidDate(c.createDate)) c.createDate = getTodayDate();
        if (!c.lastReviewDate.empty() && !isValidDate(c.lastReviewDate)) c.lastReviewDate = "";
        if (!isValidDate(c.nextReviewDate)) c.nextReviewDate = fallbackReviewDate(c.createDate);

        if (oldDifficulty != c.difficulty) repairedCount++;
        if (oldMastery != c.mastery) repairedCount++;
        if (oldInterval != c.intervalDays) repairedCount++;
        if (oldCreateDate != c.createDate) repairedCount++;
        if (oldLastReviewDate != c.lastReviewDate) repairedCount++;
        if (oldNextReviewDate != c.nextReviewDate) repairedCount++;
    }

    for (WrongQuestion& w : wrongs) {
        if (!isUserInScope(w.userId, userIdFilter)) continue;

        int oldMastery = w.mastery;
        int oldInterval = w.intervalDays;
        string oldCreateDate = w.createDate;
        string oldLastReviewDate = w.lastReviewDate;
        string oldNextReviewDate = w.nextReviewDate;

        w.mastery = clampToRange(w.mastery, 0, 100);
        if (w.intervalDays < 1) w.intervalDays = 1;
        if (!isValidDate(w.createDate)) w.createDate = getTodayDate();
        if (!w.lastReviewDate.empty() && !isValidDate(w.lastReviewDate)) w.lastReviewDate = "";
        if (!isValidDate(w.nextReviewDate)) w.nextReviewDate = fallbackReviewDate(w.createDate);

        if (oldMastery != w.mastery) repairedCount++;
        if (oldInterval != w.intervalDays) repairedCount++;
        if (oldCreateDate != w.createDate) repairedCount++;
        if (oldLastReviewDate != w.lastReviewDate) repairedCount++;
        if (oldNextReviewDate != w.nextReviewDate) repairedCount++;
    }

    if (repairedCount > 0) {
        saveCards();
        saveWrongs();
    }
    return repairedCount;
}

void showDeletedCardsOfCurrentUser() {
    clearScreen();
    renderPageHeader("已删除的知识卡片", "查看当前用户回收站中的知识卡片。");
    
    currentDeletedCardMap.clear();
    for (size_t i = 0; i < cards.size(); ++i) {
        if (cards[i].userId == currentUserId && !cards[i].active) {
            currentDeletedCardMap.push_back(i);
        }
    }
    
    if (currentDeletedCardMap.empty()) {
        printTuiNotice(TuiNoticeLevel::Info, "当前没有已删除的卡片。");
    } else {
        for (size_t i = 0; i < currentDeletedCardMap.size(); ++i) {
            const Card& c = cards[currentDeletedCardMap[i]];
            cout << "  [" << (i + 1) << "] " << c.title << " | " << c.subject << "\n";
        }
        cout << "\n共 " << currentDeletedCardMap.size() << " 条已删除记录。\n";
    }
    pauseScreen();
}

void showDeletedWrongsOfCurrentUser() {
    clearScreen();
    renderPageHeader("已删除的错题", "查看当前用户回收站中的错题记录。");
    
    currentDeletedWrongMap.clear();
    for (size_t i = 0; i < wrongs.size(); ++i) {
        if (wrongs[i].userId == currentUserId && !wrongs[i].active) {
            currentDeletedWrongMap.push_back(i);
        }
    }
    
    if (currentDeletedWrongMap.empty()) {
        printTuiNotice(TuiNoticeLevel::Info, "当前没有已删除的错题。");
    } else {
        for (size_t i = 0; i < currentDeletedWrongMap.size(); ++i) {
            const WrongQuestion& w = wrongs[currentDeletedWrongMap[i]];
            cout << "  [" << (i + 1) << "] " 
                 << (w.question.size() > 20 ? w.question.substr(0, 17) + "..." : w.question) 
                 << " | " << w.subject << "\n";
        }
        cout << "\n共 " << currentDeletedWrongMap.size() << " 条已删除记录。\n";
    }
    pauseScreen();
}

bool restoreCard() {
    clearScreen();
    renderPageHeader("恢复知识卡片", "根据回收站展示序号恢复逻辑删除的卡片。");

    if (currentDeletedCardMap.empty()) {
        printTuiNotice(TuiNoticeLevel::Warning, "请先使用“查看已删除卡片”生成列表。");
        return false;
    }

    cout << "\n请输入要恢复的卡片序号（1~" << currentDeletedCardMap.size() << "，直接回车取消）：";
    string line;
    getline(cin, line);
    line = trim(line);
    if (line.empty()) return false;

    int displayIdx;
    if (!parseInt(line, displayIdx) || displayIdx < 1 || displayIdx > static_cast<int>(currentDeletedCardMap.size())) {
        printTuiNotice(TuiNoticeLevel::Error, "序号无效。");
        return false;
    }
    
    int found = currentDeletedCardMap[displayIdx - 1];
    if (cards[found].active) {
        printTuiNotice(TuiNoticeLevel::Info, "该卡片已经是正常状态，无需恢复。");
        return false;
    }

    // 恢复只改变 active，不重算复习计划；用户可在今日复习或数据检查中继续处理状态。
    cards[found].active = true;
    saveCards();
    printTuiNotice(TuiNoticeLevel::Success, "卡片恢复成功。");
    
    // 恢复后最好清空 map，防止旧映射错乱
    currentDeletedCardMap.clear();
    return true;
}

bool restoreWrong() {
    clearScreen();
    renderPageHeader("恢复错题", "根据回收站展示序号恢复逻辑删除的错题。");

    if (currentDeletedWrongMap.empty()) {
        printTuiNotice(TuiNoticeLevel::Warning, "请先使用“查看已删除错题”生成列表。");
        return false;
    }

    cout << "\n请输入要恢复的错题序号（1~" << currentDeletedWrongMap.size() << "，直接回车取消）：";
    string line;
    getline(cin, line);
    line = trim(line);
    if (line.empty()) return false;

    int displayIdx;
    if (!parseInt(line, displayIdx) || displayIdx < 1 || displayIdx > static_cast<int>(currentDeletedWrongMap.size())) {
        printTuiNotice(TuiNoticeLevel::Error, "序号无效。");
        return false;
    }
    
    int found = currentDeletedWrongMap[displayIdx - 1];
    if (wrongs[found].active) {
        printTuiNotice(TuiNoticeLevel::Info, "该错题已经是正常状态，无需恢复。");
        return false;
    }

    // 关联卡片是否仍有效由数据一致性检查负责，恢复错题本身不自动生成卡片。
    wrongs[found].active = true;
    saveWrongs();
    printTuiNotice(TuiNoticeLevel::Success, "错题恢复成功。");
    
    currentDeletedWrongMap.clear();
    return true;
}

void physicalDeleteInvalidRecords() {
    clearScreen();
    renderPageHeader("彻底清理回收站", "永久删除当前用户已逻辑删除的卡片和错题。");
    
    printTuiNotice(TuiNoticeLevel::Warning, "此操作将永久抹除当前用户所有已删除的卡片和错题，无法恢复。");
    cout << "确定要继续吗？(y/n): ";
    string line;
    getline(cin, line);
    line = trim(line);
    if (line != "y" && line != "Y") {
        printTuiNotice(TuiNoticeLevel::Info, "已取消物理删除。");
        return;
    }

    size_t beforeCards = cards.size();
    size_t beforeWrongs = wrongs.size();

    // 物理清理限定当前用户已逻辑删除记录，避免误删其他用户或仍在使用的数据。
    cards.erase(
        remove_if(cards.begin(), cards.end(), [](const Card& c) { 
            return !c.active && c.userId == currentUserId; 
        }),
        cards.end()
    );
    
    wrongs.erase(
        remove_if(wrongs.begin(), wrongs.end(), [](const WrongQuestion& w) { 
            return !w.active && w.userId == currentUserId; 
        }),
        wrongs.end()
    );

    saveCards();
    saveWrongs();

    size_t deletedCards = beforeCards - cards.size();
    size_t deletedWrongs = beforeWrongs - wrongs.size();

    printTuiNotice(TuiNoticeLevel::Success, "回收站清理完毕。");
    cout << "共彻底物理删除 " << deletedCards << " 张知识卡片。\n";
    cout << "共彻底物理删除 " << deletedWrongs << " 道错题记录。\n";
    
    // 容器 erase 后旧下标全部失效，必须清空展示映射。
    currentDeletedCardMap.clear();
    currentDeletedWrongMap.clear();
}

void runDataConsistencyCheck() {
    clearScreen();
    renderPageHeader("数据一致性检查工具", "扫描失效关联、字段范围和非法日期。");
    printTuiNotice(TuiNoticeLevel::Info, "检查范围：" + describeUserScope(currentUserId));

    DataConsistencyReport report = inspectDataConsistency(currentUserId);
    printConsistencySummary(report);

    if (report.autoFixableCount == 0) {
        pauseScreen();
        return;
    }

    cout << "\n是否自动修复可修复问题？(y/n)：";
    string line;
    getline(cin, line);
    line = trim(line);
    if (line != "y" && line != "Y") {
        printTuiNotice(TuiNoticeLevel::Info, "已取消自动修复。");
        pauseScreen();
        return;
    }

    int repairedCount = repairDataConsistency(report, currentUserId);

    printTuiNotice(TuiNoticeLevel::Success, "自动修复完成，已修复 " + to_string(repairedCount) + " 条记录或关联。");
    pauseScreen();
}

int runDataConsistencyCli(bool fix, int userIdFilter) {
    cout << "数据一致性检查工具（非交互模式）\n";
    cout << "检查范围：" << describeUserScope(userIdFilter) << "\n";
    cout << "自动修复：" << (fix ? "开启" : "关闭") << "\n";

    DataConsistencyReport report = inspectDataConsistency(userIdFilter);
    printConsistencySummary(report);

    int remainingIssues = report.issueCount;
    if (fix && report.autoFixableCount > 0) {
        int repairedCount = repairDataConsistency(report, userIdFilter);
        cout << "\n[自动修复]\n";
        cout << "  已修复：" << repairedCount << " 条记录或关联\n";
        remainingIssues = report.issueCount - report.autoFixableCount;
        cout << "  剩余需人工处理：" << remainingIssues << " 项\n";
    }

    if (remainingIssues == 0) {
        cout << "\n检查通过。\n";
        return 0;
    }

    cout << "\n检查未通过。\n";
    return 1;
}

void showMaintenanceMenu() {
    while (true) {
        clearScreen();
        renderSubMenu("数据维护", "恢复误删内容，清理回收站并检查数据一致性", {
            {"1", "查看已删除卡片", "cards"},
            {"2", "恢复知识卡片", "restore"},
            {"3", "查看已删除错题", "wrongs"},
            {"4", "恢复错题", "restore"},
            {"5", "彻底清空回收站", "purge"},
            {"6", "数据一致性检查", "check"},
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
            case 1: showDeletedCardsOfCurrentUser(); break;
            case 2: 
                restoreCard(); 
                pauseScreen();
                break;
            case 3: showDeletedWrongsOfCurrentUser(); break;
            case 4: 
                restoreWrong(); 
                pauseScreen();
                break;
            case 5:
                physicalDeleteInvalidRecords();
                pauseScreen();
                break;
            case 6:
                runDataConsistencyCheck();
                break;
            case 0: return;
            default:
                printTuiNotice(TuiNoticeLevel::Error, "菜单选项不存在。");
                pauseScreen();
        }
    }
}
