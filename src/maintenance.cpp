// 引入数据维护模块头文件：公开维护函数声明都在这里。
#include "maintenance.h"

// 引入全局状态：cards、wrongs、users、currentUserId 等来自这里。
#include "globals.h"

// 引入通用工具：trim、parseInt、日期校验、暂停/清屏等。
#include "utils.h"

// 引入存储层接口：saveCards/saveWrongs 等持久化能力来自这里。
#include "storage.h"

// iostream 提供 cin/cout，用于交互式维护和 CLI 报告输出。
#include <iostream>

// string 提供 std::string。
#include <string>

// algorithm 提供 remove_if，用于回收站物理清理。
#include <algorithm>

// map 用于统计重复 ID 次数。
#include <map>

// set 当前保留为维护模块扩展用；本文件已有历史 include。
#include <set>

// vector 用于回收站展示映射和一致性报告中的索引列表。
#include <vector>

// 只引入本文件实际使用的标准库名字，避免命名空间整体引入污染。
using std::cin;
using std::cout;
using std::getline;
using std::map;
using std::remove_if;
using std::string;
using std::to_string;
using std::vector;

/*
[导读]
- 本文件负责“数据维护”：回收站恢复/物理清理，以及非交互数据一致性检查。

[对应流程图]
- 回收站：active=false 的 Card/WrongQuestion -> 展示序号 -> 恢复或物理删除。
- 一致性检查：全局容器 -> 检查坏关联/字段范围/非法日期/重复 ID -> 报告或自动修复。

[输入输出]
- 输入：cards/wrongs/users 和可选 userIdFilter。
- 输出：控制台报告、修复后的 cards.txt/wrongs.txt、CLI 退出码。

[学习重点]
- 自动修复只处理不会改变用户意图的问题，如失效 linkedCardId、字段范围和日期兜底。
- 交互模式只检查当前登录用户；CLI 模式可检查全部用户或指定用户。
- 回收站恢复只改 active，不重算复习计划，也不自动修复关联。

[易错点]
- 重复 ID 可能需要人工判断合并或删除，当前只报告，不自动改写。
- inspectDataConsistency() 记录的是 wrongs 容器下标，修复前不能重排 wrongs。
- physicalDeleteInvalidRecords() 会 erase 容器，执行后所有旧下标映射都必须清空。

[实验]
- 在 fixture 中制造一个 linkedCardId 指向其他用户卡片的错题，再运行 --check-data --fix 观察修复结果。
- 在 fixture 中制造非法日期和越界 mastery，再比较 --check-data 与 --check-data --fix 的输出差异。
*/

// ========== 表现层映射 ==========
// 回收站操作也使用展示序号，因此需要独立维护“已删除列表”的下标映射。
// 保存最近一次“已删除卡片列表”的展示序号 -> cards 下标。
static vector<int> currentDeletedCardMap;

// 保存最近一次“已删除错题列表”的展示序号 -> wrongs 下标。
static vector<int> currentDeletedWrongMap;

// 表示：一次数据一致性扫描的汇总结果。
// 注意：invalidLinkedWrongIndexes 存 wrongs 容器下标，repair 阶段依赖当前容器未被重排。
struct DataConsistencyReport {
    // 检查发现的问题总数。
    int issueCount = 0;

    // 其中可以自动修复的问题数。
    int autoFixableCount = 0;

    // 重复 ID 问题数；当前只报告，不自动修。
    int duplicateIssueCount = 0;

    // 字段范围/日期问题数；通常可以自动修。
    int fieldIssueCount = 0;

    // linkedCardId 失效的 wrongs 容器下标列表。
    vector<int> invalidLinkedWrongIndexes;
};

static int findCardIndexById(int cardId) {
    // 遍历全局 cards 容器。
    for (size_t i = 0; i < cards.size(); ++i) {
        // cardId 是持久化 ID，不是容器下标。
        if (cards[i].cardId == cardId) {
            return static_cast<int>(i);
        }
    }

    // 没找到时返回 -1。
    return -1;
}

static int clampToRange(int value, int low, int high) {
    // 小于下限时钳到下限。
    if (value < low) return low;

    // 大于上限时钳到上限。
    if (value > high) return high;

    // 已在范围内则保持原值。
    return value;
}

static string fallbackReviewDate(const string& createDate) {
    // 下次复习日期非法时优先回退到有效创建日期，保留“创建后可复习”的业务语义。
    // 如果创建日期也非法，就回退到今天，避免继续写入坏日期。
    return isValidDate(createDate) ? createDate : getTodayDate();
}

static bool isUserInScope(int recordUserId, int userIdFilter) {
    // userIdFilter=-1 表示全部用户；否则只检查指定用户。
    return userIdFilter == -1 || recordUserId == userIdFilter;
}

static string describeUserScope(int userIdFilter) {
    // 把 userIdFilter 转成人类可读的检查范围说明。
    return userIdFilter == -1 ? "全部用户" : ("用户 ID " + to_string(userIdFilter));
}

static int printDuplicateIds(const map<int, int>& counts, const string& label) {
    // 返回发现了多少个重复 ID 值。
    int duplicateCount = 0;

    // 控制标题只打印一次。
    bool hasDuplicate = false;

    // 遍历 ID -> 出现次数。
    for (const auto& item : counts) {
        // 出现次数大于 1 表示重复 ID。
        if (item.second > 1) {
            if (!hasDuplicate) {
                cout << "\n[重复编号] " << label << "：\n";
                hasDuplicate = true;
            }
            // 输出重复 ID 和出现次数。
            cout << "  ID " << item.first << " 出现 " << item.second << " 次\n";
            duplicateCount++;
        }
    }

    // 返回重复 ID 项数，不是重复记录总条数。
    return duplicateCount;
}

static DataConsistencyReport inspectDataConsistency(int userIdFilter) {
    // 创建本次检查报告。
    DataConsistencyReport report;

    // cardId -> 出现次数，用于发现重复卡片编号。
    map<int, int> cardIdCounts;

    // wrongId -> 出现次数，用于发现重复错题编号。
    map<int, int> wrongIdCounts;

    // 统计所有卡片 ID 出现次数；重复 ID 是全局问题，不按 userIdFilter 缩小范围。
    for (const Card& c : cards) {
        cardIdCounts[c.cardId]++;
    }

    // 统计所有错题 ID 出现次数。
    for (const WrongQuestion& w : wrongs) {
        wrongIdCounts[w.wrongId]++;
    }

    // 打印并累计重复卡片编号问题。
    report.duplicateIssueCount += printDuplicateIds(cardIdCounts, "知识卡片编号");

    // 打印并累计重复错题编号问题。
    report.duplicateIssueCount += printDuplicateIds(wrongIdCounts, "错题编号");

    // 重复 ID 属于问题总数，但不计入自动修复。
    report.issueCount += report.duplicateIssueCount;

    // 检查 wrongs.linkedCardId 是否仍指向合法卡片。
    cout << "\n[错题关联检查]\n";

    // 遍历全部错题。
    for (size_t i = 0; i < wrongs.size(); ++i) {
        const WrongQuestion& w = wrongs[i];

        // 不在检查范围内的用户跳过；未关联卡片 linkedCardId=-1 也跳过。
        if (!isUserInScope(w.userId, userIdFilter) || w.linkedCardId == -1) continue;

        // 查找 linkedCardId 指向的卡片。
        int cardIndex = findCardIndexById(w.linkedCardId);

        // valid 表示关联是否有效。
        bool valid = false;

        // reason 用于报告无效原因。
        string reason;

        // 情况 1：目标卡片不存在。
        if (cardIndex == -1) {
            reason = "目标卡片不存在";

        // 情况 2：目标卡片存在，但属于其他用户。
        } else if (cards[cardIndex].userId != w.userId) {
            reason = "目标卡片属于其他用户";

        // 情况 3：目标卡片已被逻辑删除，普通卡片管理不可见。
        } else if (!cards[cardIndex].active) {
            reason = "目标卡片已被逻辑删除，知识卡片管理中不可见";

        // 目标存在、同用户、active=true，关联有效。
        } else {
            valid = true;
        }

        if (!valid) {
            // 失效关联可安全重置为 -1，用户随后可重新执行“错题转知识卡片”。
            cout << "  错题 ID " << w.wrongId << " -> 卡片 ID " << w.linkedCardId
                 << " 无效：" << reason << "\n";

            // 记录 wrongs 容器下标，供 repairDataConsistency 精准修复。
            report.invalidLinkedWrongIndexes.push_back(static_cast<int>(i));

            // 问题总数加 1。
            report.issueCount++;

            // 失效关联可自动修复为 -1。
            report.autoFixableCount++;
        }
    }

    // 没有失效关联时输出明确结论。
    if (report.invalidLinkedWrongIndexes.empty()) {
        cout << "  未发现失效的错题转卡片关联。\n";
    }

    // 检查字段范围和日期合法性。
    cout << "\n[字段范围检查]\n";

    // 第一段：检查卡片字段。
    for (const Card& c : cards) {
        // 只检查指定用户范围内的记录。
        if (!isUserInScope(c.userId, userIdFilter)) continue;

        // 难度应在 1~5。
        if (c.difficulty < 1 || c.difficulty > 5) {
            cout << "  卡片 ID " << c.cardId << " 难度超出 1~5：" << c.difficulty << "\n";
            report.fieldIssueCount++;
            report.autoFixableCount++;
        }

        // 掌握度应在 0~100。
        if (c.mastery < 0 || c.mastery > 100) {
            cout << "  卡片 ID " << c.cardId << " 掌握度超出 0~100：" << c.mastery << "\n";
            report.fieldIssueCount++;
            report.autoFixableCount++;
        }

        // 复习间隔最小为 1。
        if (c.intervalDays < 1) {
            cout << "  卡片 ID " << c.cardId << " 复习间隔小于 1：" << c.intervalDays << "\n";
            report.fieldIssueCount++;
            report.autoFixableCount++;
        }

        // 创建日期必须合法。
        if (!isValidDate(c.createDate)) {
            cout << "  卡片 ID " << c.cardId << " 创建日期无效：" << c.createDate << "\n";
            report.fieldIssueCount++;
            report.autoFixableCount++;
        }

        // 最近复习日期允许为空；非空时必须合法。
        if (!c.lastReviewDate.empty() && !isValidDate(c.lastReviewDate)) {
            cout << "  卡片 ID " << c.cardId << " 最近复习日期无效：" << c.lastReviewDate << "\n";
            report.fieldIssueCount++;
            report.autoFixableCount++;
        }

        // 下次复习日期必须合法。
        if (!isValidDate(c.nextReviewDate)) {
            cout << "  卡片 ID " << c.cardId << " 下次复习日期无效：" << c.nextReviewDate << "\n";
            report.fieldIssueCount++;
            report.autoFixableCount++;
        }
    }

    // 第二段：检查错题字段。
    for (const WrongQuestion& w : wrongs) {
        // 只检查指定用户范围内的记录。
        if (!isUserInScope(w.userId, userIdFilter)) continue;

        // 掌握度应在 0~100。
        if (w.mastery < 0 || w.mastery > 100) {
            cout << "  错题 ID " << w.wrongId << " 掌握度超出 0~100：" << w.mastery << "\n";
            report.fieldIssueCount++;
            report.autoFixableCount++;
        }

        // 复习间隔最小为 1。
        if (w.intervalDays < 1) {
            cout << "  错题 ID " << w.wrongId << " 复习间隔小于 1：" << w.intervalDays << "\n";
            report.fieldIssueCount++;
            report.autoFixableCount++;
        }

        // 创建日期必须合法。
        if (!isValidDate(w.createDate)) {
            cout << "  错题 ID " << w.wrongId << " 创建日期无效：" << w.createDate << "\n";
            report.fieldIssueCount++;
            report.autoFixableCount++;
        }

        // 最近复习日期允许为空；非空时必须合法。
        if (!w.lastReviewDate.empty() && !isValidDate(w.lastReviewDate)) {
            cout << "  错题 ID " << w.wrongId << " 最近复习日期无效：" << w.lastReviewDate << "\n";
            report.fieldIssueCount++;
            report.autoFixableCount++;
        }

        // 下次复习日期必须合法。
        if (!isValidDate(w.nextReviewDate)) {
            cout << "  错题 ID " << w.wrongId << " 下次复习日期无效：" << w.nextReviewDate << "\n";
            report.fieldIssueCount++;
            report.autoFixableCount++;
        }
    }

    // 没有字段问题时输出明确结论。
    if (report.fieldIssueCount == 0) {
        cout << "  未发现范围内的字段范围问题。\n";
    }

    // 字段问题加入问题总数。
    report.issueCount += report.fieldIssueCount;

    // 返回完整检查报告。
    return report;
}

static void printConsistencySummary(const DataConsistencyReport& report) {
    // 输出本次检查的汇总结果。
    cout << "\n[检查结果]\n";

    // 所有问题数，包括可修复和不可修复。
    cout << "  发现问题：" << report.issueCount << " 项\n";

    // 自动修复只包含失效关联、字段范围、非法日期等安全修复。
    cout << "  可自动修复：" << report.autoFixableCount << " 项\n";

    // 需人工处理通常是重复 ID 这类无法安全推断意图的问题。
    cout << "  需人工处理：" << (report.issueCount - report.autoFixableCount) << " 项\n";
}

static int repairDataConsistency(const DataConsistencyReport& report, int userIdFilter) {
    // 记录本轮实际修改了多少个字段或关联。
    int repairedCount = 0;

    // 第一段：修复失效 linkedCardId。
    for (int idx : report.invalidLinkedWrongIndexes) {
        // idx 是 inspectDataConsistency 记录的 wrongs 容器下标；这里再次做边界保护。
        if (idx >= 0 && idx < static_cast<int>(wrongs.size()) && wrongs[idx].linkedCardId != -1) {
            // 失效关联重置为 -1，表示“未关联卡片”。
            wrongs[idx].linkedCardId = -1;
            repairedCount++;
        }
    }

    // 第二段：修复卡片字段范围和日期。
    for (Card& c : cards) {
        // 只修复指定用户范围内的记录。
        if (!isUserInScope(c.userId, userIdFilter)) continue;

        // 保存旧值，用于判断是否真的发生修复。
        int oldDifficulty = c.difficulty;
        int oldMastery = c.mastery;
        int oldInterval = c.intervalDays;
        string oldCreateDate = c.createDate;
        string oldLastReviewDate = c.lastReviewDate;
        string oldNextReviewDate = c.nextReviewDate;

        // 字段修复只做范围钳制和日期兜底，不尝试推断用户原始意图。
        // 难度钳制到 1~5。
        c.difficulty = clampToRange(c.difficulty, 1, 5);

        // 掌握度钳制到 0~100。
        c.mastery = clampToRange(c.mastery, 0, 100);

        // 复习间隔最小修复到 1。
        if (c.intervalDays < 1) c.intervalDays = 1;

        // 创建日期非法时回退到今天。
        if (!isValidDate(c.createDate)) c.createDate = getTodayDate();

        // 最近复习日期非法时清空，表示“尚无可信最近复习日期”。
        if (!c.lastReviewDate.empty() && !isValidDate(c.lastReviewDate)) c.lastReviewDate = "";

        // 下次复习日期非法时，优先回退到有效创建日期，否则回退到今天。
        if (!isValidDate(c.nextReviewDate)) c.nextReviewDate = fallbackReviewDate(c.createDate);

        // 逐项比较新旧值，统计实际修复数量。
        if (oldDifficulty != c.difficulty) repairedCount++;
        if (oldMastery != c.mastery) repairedCount++;
        if (oldInterval != c.intervalDays) repairedCount++;
        if (oldCreateDate != c.createDate) repairedCount++;
        if (oldLastReviewDate != c.lastReviewDate) repairedCount++;
        if (oldNextReviewDate != c.nextReviewDate) repairedCount++;
    }

    // 第三段：修复错题字段范围和日期。
    for (WrongQuestion& w : wrongs) {
        // 只修复指定用户范围内的记录。
        if (!isUserInScope(w.userId, userIdFilter)) continue;

        // 保存旧值，用于判断是否真的发生修复。
        int oldMastery = w.mastery;
        int oldInterval = w.intervalDays;
        string oldCreateDate = w.createDate;
        string oldLastReviewDate = w.lastReviewDate;
        string oldNextReviewDate = w.nextReviewDate;

        // 掌握度钳制到 0~100。
        w.mastery = clampToRange(w.mastery, 0, 100);

        // 复习间隔最小修复到 1。
        if (w.intervalDays < 1) w.intervalDays = 1;

        // 创建日期非法时回退到今天。
        if (!isValidDate(w.createDate)) w.createDate = getTodayDate();

        // 最近复习日期非法时清空。
        if (!w.lastReviewDate.empty() && !isValidDate(w.lastReviewDate)) w.lastReviewDate = "";

        // 下次复习日期非法时，优先回退到有效创建日期，否则回退到今天。
        if (!isValidDate(w.nextReviewDate)) w.nextReviewDate = fallbackReviewDate(w.createDate);

        // 逐项比较新旧值，统计实际修复数量。
        if (oldMastery != w.mastery) repairedCount++;
        if (oldInterval != w.intervalDays) repairedCount++;
        if (oldCreateDate != w.createDate) repairedCount++;
        if (oldLastReviewDate != w.lastReviewDate) repairedCount++;
        if (oldNextReviewDate != w.nextReviewDate) repairedCount++;
    }

    // 只有真的改了数据才写文件，避免无意义落盘。
    if (repairedCount > 0) {
        saveCards();
        saveWrongs();
    }

    // 返回实际修复数量。
    return repairedCount;
}

void showDeletedCardsOfCurrentUser() {
    // 进入已删除卡片列表页面。
    clearScreen();
    cout << "==============================\n";
    cout << "     已删除的知识卡片\n";
    cout << "==============================\n";
    
    // 重建已删除卡片展示映射。
    currentDeletedCardMap.clear();

    // 扫描所有卡片。
    for (size_t i = 0; i < cards.size(); ++i) {
        // 只展示当前用户 active=false 的卡片。
        if (cards[i].userId == currentUserId && !cards[i].active) {
            currentDeletedCardMap.push_back(i);
        }
    }
    
    // 没有已删除卡片时提示。
    if (currentDeletedCardMap.empty()) {
        cout << "当前没有已删除的卡片。\n";
    } else {
        // 输出回收站列表，序号从 1 开始。
        for (size_t i = 0; i < currentDeletedCardMap.size(); ++i) {
            const Card& c = cards[currentDeletedCardMap[i]];
            cout << "  [" << (i + 1) << "] " << c.title << " | " << c.subject << "\n";
        }

        // 输出总数。
        cout << "\n共 " << currentDeletedCardMap.size() << " 条已删除记录。\n";
    }
    pauseScreen();
}

void showDeletedWrongsOfCurrentUser() {
    // 进入已删除错题列表页面。
    clearScreen();
    cout << "==============================\n";
    cout << "      已删除的错题\n";
    cout << "==============================\n";
    
    // 重建已删除错题展示映射。
    currentDeletedWrongMap.clear();

    // 扫描所有错题。
    for (size_t i = 0; i < wrongs.size(); ++i) {
        // 只展示当前用户 active=false 的错题。
        if (wrongs[i].userId == currentUserId && !wrongs[i].active) {
            currentDeletedWrongMap.push_back(i);
        }
    }
    
    // 没有已删除错题时提示。
    if (currentDeletedWrongMap.empty()) {
        cout << "当前没有已删除的错题。\n";
    } else {
        // 输出回收站列表，题目过长时截断。
        for (size_t i = 0; i < currentDeletedWrongMap.size(); ++i) {
            const WrongQuestion& w = wrongs[currentDeletedWrongMap[i]];
            cout << "  [" << (i + 1) << "] " 
                 << (w.question.size() > 20 ? w.question.substr(0, 17) + "..." : w.question) 
                 << " | " << w.subject << "\n";
        }

        // 输出总数。
        cout << "\n共 " << currentDeletedWrongMap.size() << " 条已删除记录。\n";
    }
    pauseScreen();
}

bool restoreCard() {
    // 恢复依赖最近一次“查看已删除卡片”生成的展示映射。
    if (currentDeletedCardMap.empty()) {
        cout << "\n请先使用“1. 查看已删除卡片”生成列表。\n";
        return false;
    }

    // 用户输入展示序号。
    cout << "\n请输入要恢复的卡片序号（1~" << currentDeletedCardMap.size() << "，直接回车取消）：";
    string line;
    getline(cin, line);
    line = trim(line);

    // 直接回车表示取消恢复。
    if (line.empty()) return false;

    // 校验展示序号。
    int displayIdx;
    if (!parseInt(line, displayIdx) || displayIdx < 1 || displayIdx > static_cast<int>(currentDeletedCardMap.size())) {
        cout << "序号无效。\n";
        return false;
    }
    
    // 转换成 cards 容器下标。
    int found = currentDeletedCardMap[displayIdx - 1];

    // 如果已经 active=true，说明旧映射已经过期。
    if (cards[found].active) {
        cout << "该卡片已经是正常状态，无需恢复。\n";
        return false;
    }

    // 恢复只改变 active，不重算复习计划；用户可在今日复习或数据检查中继续处理状态。
    cards[found].active = true;

    // 保存恢复结果。
    saveCards();
    cout << "卡片恢复成功！\n";
    
    // 恢复后最好清空 map，防止旧映射错乱
    currentDeletedCardMap.clear();

    // 返回 true 表示确实完成恢复。
    return true;
}

bool restoreWrong() {
    // 恢复依赖最近一次“查看已删除错题”生成的展示映射。
    if (currentDeletedWrongMap.empty()) {
        cout << "\n请先使用“3. 查看已删除错题”生成列表。\n";
        return false;
    }

    // 用户输入展示序号。
    cout << "\n请输入要恢复的错题序号（1~" << currentDeletedWrongMap.size() << "，直接回车取消）：";
    string line;
    getline(cin, line);
    line = trim(line);

    // 直接回车表示取消恢复。
    if (line.empty()) return false;

    // 校验展示序号。
    int displayIdx;
    if (!parseInt(line, displayIdx) || displayIdx < 1 || displayIdx > static_cast<int>(currentDeletedWrongMap.size())) {
        cout << "序号无效。\n";
        return false;
    }
    
    // 转换成 wrongs 容器下标。
    int found = currentDeletedWrongMap[displayIdx - 1];

    // 如果已经 active=true，说明旧映射已经过期。
    if (wrongs[found].active) {
        cout << "该错题已经是正常状态，无需恢复。\n";
        return false;
    }

    // 关联卡片是否仍有效由数据一致性检查负责，恢复错题本身不自动生成卡片。
    wrongs[found].active = true;

    // 保存恢复结果。
    saveWrongs();
    cout << "错题恢复成功！\n";
    
    // 恢复后清空旧映射，防止下次操作使用过期下标。
    currentDeletedWrongMap.clear();

    // 返回 true 表示确实完成恢复。
    return true;
}

void physicalDeleteInvalidRecords() {
    // 进入物理清理页面。
    clearScreen();
    cout << "==============================\n";
    cout << "    彻底清理回收站 (物理删除)\n";
    cout << "==============================\n";
    
    cout << "警告：此操作将永久抹除当前用户所有已删除的卡片和错题，无法恢复！\n";
    cout << "确定要继续吗？(y/n): ";
    string line;
    getline(cin, line);
    line = trim(line);

    // 只有 y/Y 才继续执行物理删除。
    if (line != "y" && line != "Y") {
        cout << "已取消物理删除。\n";
        return;
    }

    // 记录删除前容器大小，用于计算删除数量。
    size_t beforeCards = cards.size();
    size_t beforeWrongs = wrongs.size();

    // 物理清理限定当前用户已逻辑删除记录，避免误删其他用户或仍在使用的数据。
    // 删除当前用户 active=false 的卡片记录。
    cards.erase(
        remove_if(cards.begin(), cards.end(), [](const Card& c) { 
            return !c.active && c.userId == currentUserId; 
        }),
        cards.end()
    );
    
    // 删除当前用户 active=false 的错题记录。
    wrongs.erase(
        remove_if(wrongs.begin(), wrongs.end(), [](const WrongQuestion& w) { 
            return !w.active && w.userId == currentUserId; 
        }),
        wrongs.end()
    );

    // 保存物理删除后的数据文件。
    saveCards();
    saveWrongs();

    // 计算实际删除数量。
    size_t deletedCards = beforeCards - cards.size();
    size_t deletedWrongs = beforeWrongs - wrongs.size();

    // 输出清理结果。
    cout << "清理完毕！\n";
    cout << "共彻底物理删除 " << deletedCards << " 张知识卡片。\n";
    cout << "共彻底物理删除 " << deletedWrongs << " 道错题记录。\n";
    
    // 容器 erase 后旧下标全部失效，必须清空展示映射。
    currentDeletedCardMap.clear();
    currentDeletedWrongMap.clear();
}

void runDataConsistencyCheck() {
    // 进入交互式数据一致性检查页面。
    clearScreen();
    cout << "==============================\n";
    cout << "     数据一致性检查工具\n";
    cout << "==============================\n";

    // 交互模式只检查当前登录用户的数据。
    cout << "检查范围：" << describeUserScope(currentUserId) << "\n";

    // 执行检查并打印问题详情。
    DataConsistencyReport report = inspectDataConsistency(currentUserId);

    // 打印汇总。
    printConsistencySummary(report);

    // 没有可自动修复项时直接结束。
    if (report.autoFixableCount == 0) {
        pauseScreen();
        return;
    }

    // 有可修复项时询问用户是否执行修复。
    cout << "\n是否自动修复可修复问题？(y/n)：";
    string line;
    getline(cin, line);
    line = trim(line);

    // 非 y/Y 都视为取消修复。
    if (line != "y" && line != "Y") {
        cout << "已取消自动修复。\n";
        pauseScreen();
        return;
    }

    // 执行当前用户范围内的安全修复。
    int repairedCount = repairDataConsistency(report, currentUserId);

    // 输出修复数量。
    cout << "\n自动修复完成，已修复 " << repairedCount << " 条记录或关联。\n";
    pauseScreen();
}

int runDataConsistencyCli(bool fix, int userIdFilter) {
    // CLI 模式不清屏、不暂停，方便脚本捕获输出和退出码。
    cout << "数据一致性检查工具（非交互模式）\n";

    // 输出检查范围。
    cout << "检查范围：" << describeUserScope(userIdFilter) << "\n";

    // 输出是否开启自动修复。
    cout << "自动修复：" << (fix ? "开启" : "关闭") << "\n";

    // 执行检查并打印问题详情。
    DataConsistencyReport report = inspectDataConsistency(userIdFilter);

    // 打印汇总。
    printConsistencySummary(report);

    // remainingIssues 表示最终仍然存在的问题数，用于决定退出码。
    int remainingIssues = report.issueCount;

    // fix=true 且存在可修复项时执行自动修复。
    if (fix && report.autoFixableCount > 0) {
        int repairedCount = repairDataConsistency(report, userIdFilter);
        cout << "\n[自动修复]\n";
        cout << "  已修复：" << repairedCount << " 条记录或关联\n";

        // 自动修复后只剩不可自动处理的问题，例如重复 ID。
        remainingIssues = report.issueCount - report.autoFixableCount;
        cout << "  剩余需人工处理：" << remainingIssues << " 项\n";
    }

    // 没有剩余问题时 CLI 返回 0。
    if (remainingIssues == 0) {
        cout << "\n检查通过。\n";
        return 0;
    }

    // 仍有问题时 CLI 返回 1；参数错误的退出码 2 由 main.cpp 负责。
    cout << "\n检查未通过。\n";
    return 1;
}

void showMaintenanceMenu() {
    // 数据维护子菜单循环，直到用户选择 0 返回主菜单。
    while (true) {
        // 每轮菜单前清屏。
        clearScreen();
        cout << "==============================\n";
        cout << "       数据维护\n";
        cout << "==============================\n";
        cout << "1. 查看已删除卡片\n";
        cout << "2. 恢复知识卡片\n";
        cout << "3. 查看已删除错题\n";
        cout << "4. 恢复错题\n";
        cout << "5. 彻底清空回收站\n";
        cout << "6. 数据一致性检查\n";
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

        // 根据菜单项分发到具体维护函数。
        switch (choice) {
            // 查看当前用户已删除卡片。
            case 1: showDeletedCardsOfCurrentUser(); break;

            // 恢复最近已删除卡片列表中的某张卡片。
            case 2: 
                restoreCard(); 
                pauseScreen();
                break;

            // 查看当前用户已删除错题。
            case 3: showDeletedWrongsOfCurrentUser(); break;

            // 恢复最近已删除错题列表中的某道错题。
            case 4: 
                restoreWrong(); 
                pauseScreen();
                break;

            // 物理删除当前用户回收站记录。
            case 5:
                physicalDeleteInvalidRecords();
                pauseScreen();
                break;

            // 运行交互式一致性检查。
            case 6:
                runDataConsistencyCheck();
                break;

            // 返回主菜单。
            case 0: return;

            default:
                // 其他整数不是合法菜单项。
                cout << "菜单选项不存在。\n";
                pauseScreen();
        }
    }
}
