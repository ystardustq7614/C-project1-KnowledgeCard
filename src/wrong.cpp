#include "wrong.h"
#include "card.h"
#include "globals.h"
#include "storage.h"
#include "utils.h"
#include <iostream>
#include <vector>
#include <set>

using namespace std;

/*
模块职责：
- 负责错题的交互式管理，并把错题与知识卡片联动起来。

关键约束：
- currentWrongMap 保存展示序号到 wrongs 下标的快照，底层 wrongId 仍用于持久化和关联。
- 错题转卡片需要同时写 cards.txt 和 wrongs.txt，任何一侧变更都要保持 linkedCardId 可追踪。
*/

// ========== 表现层映射 ==========
static vector<int> currentWrongMap;

static bool hasVisibleLinkedCard(int cardId) {
    // 防重复转换只认可当前用户可见卡片；指向已删除/其他用户/不存在卡片的关联应允许重新生成。
    for (const Card& c : cards) {
        if (c.cardId == cardId && c.userId == currentUserId && c.active) {
            return true;
        }
    }
    return false;
}

// ========== 错题打印 ==========

void printWrongBrief(int displayIdx, int realIdx) {
    const WrongQuestion& w = wrongs[realIdx];
    cout << "  [" << displayIdx << "] "
         << (w.question.size() > 20 ? w.question.substr(0, 17) + "..." : w.question)
         << " | " << w.subject
         << " | 掌握:" << w.mastery
         << " | 复习:" << w.reviewCount << "次"
         << "\n";
}

void printWrongDetail(int index) {
    const WrongQuestion& w = wrongs[index];
    cout << "------------------------------\n";
    // cout << "错题编号：" << w.wrongId << "\n"; // 隐藏内部 ID
    cout << "学    科：" << w.subject << "\n";
    cout << "章    节：" << w.chapter << "\n";
    cout << "题    目：" << w.question << "\n";
    cout << "正确答案：" << w.correctAnswer << "\n";
    cout << "用户答案：" << w.wrongAnswer << "\n";
    cout << "错因分析：" << w.reason << "\n";
    cout << "错因类型：" << (w.errorType.empty() ? "未分类" : w.errorType) << "\n";
    cout << "关联卡片：" << (w.linkedCardId == -1 ? "无" : to_string(w.linkedCardId)) << "\n";
    cout << "掌 握 度：" << w.mastery << "\n";
    cout << "复习次数：" << w.reviewCount << "\n";
    cout << "连续答对：" << w.correctStreak << "\n";
    cout << "当前间隔：" << w.intervalDays << " 天\n";
    cout << "创建日期：" << w.createDate << "\n";
    cout << "最近复习：" << (w.lastReviewDate.empty() ? "尚未复习" : w.lastReviewDate) << "\n";
    cout << "下次复习：" << w.nextReviewDate << "\n";
    cout << "------------------------------\n";
}

// ========== 输入辅助 ==========

static bool readNonEmptyLine(const string& prompt, string& out) {
    cout << prompt;
    string line;
    getline(cin, line);
    line = trim(line);
    if (line.empty()) {
        cout << "输入不能为空。\n";
        return false;
    }
    if (line.find('|') != string::npos) {
        cout << "输入不允许包含 | 字符。\n";
        return false;
    }
    out = line;
    return true;
}

static string readOptionalLine(const string& prompt) {
    cout << prompt;
    string line;
    getline(cin, line);
    line = trim(line);
    if (line.find('|') != string::npos) {
        cout << "输入不允许包含 | 字符，已忽略本次输入。\n";
        return "";
    }
    return line;
}

static bool readRequiredTextField(const string& prompt, string& out) {
    // 错题正文、答案和错因分析允许 | 与多行，存储层统一做字段转义。
    string value = readTextInput(prompt);
    if (trim(value).empty()) {
        cout << "输入不能为空。\n";
        return false;
    }
    out = value;
    return true;
}

static string readOptionalTextField(const string& prompt) {
    return readTextInput(prompt);
}

// ========== 错因类型辅助 ==========

static const string VALID_ERROR_TYPES[] = {
    "概念不清", "记忆错误", "粗心", "审题失误", "计算错误", "方法不会"
};
static const int ERROR_TYPE_COUNT = 6;

bool isValidErrorType(const string& errorType) {
    // 固定枚举用于统计口径稳定；空字符串表示未分类，不属于有效类型。
    for (int i = 0; i < ERROR_TYPE_COUNT; ++i) {
        if (VALID_ERROR_TYPES[i] == errorType) return true;
    }
    return false;
}

string inputErrorType() {
    cout << "\n错因类型（可选，直接回车跳过）：\n";
    for (int i = 0; i < ERROR_TYPE_COUNT; ++i) {
        cout << "  " << (i + 1) << ". " << VALID_ERROR_TYPES[i] << "\n";
    }
    cout << "请输入序号：";
    string line;
    getline(cin, line);
    line = trim(line);
    if (line.empty()) return "";

    int idx;
    if (!parseInt(line, idx) || idx < 1 || idx > ERROR_TYPE_COUNT) {
        cout << "输入无效，已跳过错因类型。\n";
        return "";
    }
    return VALID_ERROR_TYPES[idx - 1];
}

// ========== 新增错题 ==========

void addWrong() {
    clearScreen();
    cout << "==============================\n";
    cout << "       记录新错题\n";
    cout << "==============================\n";

    WrongQuestion w;
    w.userId = currentUserId;

    if (!readNonEmptyLine("学科：", w.subject)) { pauseScreen(); return; }
    if (!readNonEmptyLine("章节：", w.chapter)) { pauseScreen(); return; }
    if (!readRequiredTextField("题目内容（可输入 |；多行先输入 .multi）：", w.question)) { pauseScreen(); return; }
    if (!readRequiredTextField("正确答案（可输入 |；多行先输入 .multi）：", w.correctAnswer)) { pauseScreen(); return; }
    if (!readRequiredTextField("用户错误答案（可输入 |；多行先输入 .multi）：", w.wrongAnswer)) { pauseScreen(); return; }
    
    w.reason = readOptionalTextField("错因分析（可选，可输入 |；多行先输入 .multi）：");

    // 错因类型是统计维度，不强制填写，避免阻塞快速记录错题。
    w.errorType = inputErrorType();

    // 错题初始掌握度低于卡片，确保刚记录的错误优先进入复习队列。
    w.wrongId       = getNextWrongId();
    w.linkedCardId  = -1;
    w.mastery       = 30;
    w.reviewCount   = 0;
    w.correctStreak = 0;
    w.intervalDays  = 1;
    w.createDate    = getTodayDate();
    w.lastReviewDate = "";
    w.nextReviewDate = w.createDate;
    w.active        = true;

    wrongs.push_back(w);
    saveWrongs();

    cout << "\n错题记录成功！编号：" << w.wrongId << "\n";
    pauseScreen();
}

// ========== 修改错题 ==========

void editWrong() {
    clearScreen();
    cout << "==============================\n";
    cout << "       修改错题信息\n";
    cout << "==============================\n";

    if (currentWrongMap.empty()) {
        cout << "当前没有展示列表，正在自动加载全部错题...\n\n";
        for (size_t i = 0; i < wrongs.size(); ++i) {
            if (wrongs[i].userId == currentUserId && wrongs[i].active) {
                currentWrongMap.push_back(i);
            }
        }
        for (size_t i = 0; i < currentWrongMap.size(); ++i) {
            printWrongBrief(i + 1, currentWrongMap[i]);
        }
        cout << "\n";
    } else {
        cout << "（当前使用的是最近一次查询/查看的列表，共 " << currentWrongMap.size() << " 条记录）\n\n";
    }

    if (currentWrongMap.empty()) {
        cout << "未找到任何可用错题。\n";
        pauseScreen();
        return;
    }

    cout << "请输入要修改的错题序号（1~" << currentWrongMap.size() << "，直接回车取消）：";
    string line;
    getline(cin, line);
    line = trim(line);
    if (line.empty()) return;

    int displayIdx;
    if (!parseInt(line, displayIdx) || displayIdx < 1 || displayIdx > static_cast<int>(currentWrongMap.size())) {
        cout << "序号无效。\n";
        pauseScreen();
        return;
    }

    int found = currentWrongMap[displayIdx - 1];

    cout << "\n当前错题信息：\n";
    printWrongDetail(found);

    cout << "\n修改提示：直接回车保留原值。\n\n";

    WrongQuestion& w = wrongs[found];
    string input;

    input = readOptionalLine("学科 [" + w.subject + "]：");
    if (!input.empty()) w.subject = input;

    input = readOptionalLine("章节 [" + w.chapter + "]：");
    if (!input.empty()) w.chapter = input;

    input = readOptionalTextField("题目 [" + w.question + "]（可输入 |；多行先输入 .multi）：");
    if (!input.empty()) w.question = input;

    input = readOptionalTextField("正确答案 [" + w.correctAnswer + "]（可输入 |；多行先输入 .multi）：");
    if (!input.empty()) w.correctAnswer = input;

    input = readOptionalTextField("用户答案 [" + w.wrongAnswer + "]（可输入 |；多行先输入 .multi）：");
    if (!input.empty()) w.wrongAnswer = input;

    input = readOptionalTextField("错因分析 [" + w.reason + "]（可输入 |；多行先输入 .multi）：");
    if (!input.empty()) w.reason = input;

    // 允许清空错因类型，保证用户在无法准确分类时不会留下错误分类。
    cout << "当前错因类型：" << (w.errorType.empty() ? "未分类" : w.errorType) << "\n";
    cout << "是否修改错因类型？(y/n)：";
    getline(cin, input);
    input = trim(input);
    if (input == "y" || input == "Y") {
        string newType = inputErrorType();
        w.errorType = newType;  // 允许清空（用户直接回车）
    }

    saveWrongs();
    cout << "\n错题信息修改成功！\n";
    pauseScreen();
}

// ========== 删除错题 ==========

void deleteWrong() {
    clearScreen();
    cout << "==============================\n";
    cout << "       删除错题记录\n";
    cout << "==============================\n";

    if (currentWrongMap.empty()) {
        cout << "当前没有展示列表，正在自动加载全部错题...\n\n";
        for (size_t i = 0; i < wrongs.size(); ++i) {
            if (wrongs[i].userId == currentUserId && wrongs[i].active) {
                currentWrongMap.push_back(i);
            }
        }
        for (size_t i = 0; i < currentWrongMap.size(); ++i) {
            printWrongBrief(i + 1, currentWrongMap[i]);
        }
        cout << "\n";
    } else {
        cout << "（当前使用的是最近一次查询/查看的列表，共 " << currentWrongMap.size() << " 条记录）\n\n";
    }

    if (currentWrongMap.empty()) {
        cout << "未找到任何可用错题。\n";
        pauseScreen();
        return;
    }

    cout << "请输入要删除的错题序号（1~" << currentWrongMap.size() << "，直接回车取消）：";
    string line;
    getline(cin, line);
    line = trim(line);
    if (line.empty()) return;

    int displayIdx;
    if (!parseInt(line, displayIdx) || displayIdx < 1 || displayIdx > static_cast<int>(currentWrongMap.size())) {
        cout << "序号无效。\n";
        pauseScreen();
        return;
    }

    int found = currentWrongMap[displayIdx - 1];

    cout << "\n即将删除以下错题：\n";
    printWrongDetail(found);

    cout << "确认删除？(y/n)：";
    getline(cin, line);
    line = trim(line);
    if (line != "y" && line != "Y") {
        cout << "已取消删除。\n";
        pauseScreen();
        return;
    }

    // 逻辑删除保留错题与复习日志，便于回收站恢复和历史统计审计。
    wrongs[found].active = false;
    saveWrongs();
    cout << "错题已删除。\n";
    pauseScreen();
}

// ========== 查看列表详情 ==========

void queryWrongById() {
    clearScreen();
    cout << "==============================\n";
    cout << "       查看错题详情\n";
    cout << "==============================\n";

    if (currentWrongMap.empty()) {
        cout << "当前没有展示列表，正在自动加载全部错题...\n\n";
        for (size_t i = 0; i < wrongs.size(); ++i) {
            if (wrongs[i].userId == currentUserId && wrongs[i].active) {
                currentWrongMap.push_back(i);
            }
        }
        for (size_t i = 0; i < currentWrongMap.size(); ++i) {
            printWrongBrief(i + 1, currentWrongMap[i]);
        }
        cout << "\n";
    } else {
        cout << "（当前使用的是最近一次查询/查看的列表，共 " << currentWrongMap.size() << " 条记录）\n\n";
    }

    if (currentWrongMap.empty()) {
        cout << "未找到任何可用错题。\n";
        pauseScreen();
        return;
    }

    cout << "请输入要查看的错题序号（1~" << currentWrongMap.size() << "，直接回车取消）：";
    string line;
    getline(cin, line);
    line = trim(line);
    if (line.empty()) return;

    int displayIdx;
    if (!parseInt(line, displayIdx) || displayIdx < 1 || displayIdx > static_cast<int>(currentWrongMap.size())) {
        cout << "序号无效。\n";
        pauseScreen();
        return;
    }

    printWrongDetail(currentWrongMap[displayIdx - 1]);
    pauseScreen();
}

void queryWrongByKeyword() {
    clearScreen();
    cout << "==============================\n";
    cout << "     按关键字查询错题\n";
    cout << "==============================\n";

    cout << "请输入关键字：";
    string keyword;
    getline(cin, keyword);
    keyword = trim(keyword);
    if (keyword.empty()) {
        cout << "关键字不能为空。\n";
        pauseScreen();
        return;
    }

    currentWrongMap.clear();
    for (size_t i = 0; i < wrongs.size(); ++i) {
        const WrongQuestion& w = wrongs[i];
        if (w.userId != currentUserId || !w.active) continue;
        if (containsKeyword(w.question, keyword) ||
            containsKeyword(w.correctAnswer, keyword) ||
            containsKeyword(w.wrongAnswer, keyword) ||
            containsKeyword(w.reason, keyword)) {
            currentWrongMap.push_back(static_cast<int>(i));
        }
    }

    if (currentWrongMap.empty()) {
        cout << "未找到匹配的错题。\n";
        pauseScreen();
        return;
    }

    cout << "\n共找到 " << currentWrongMap.size() << " 条结果：\n\n";
    for (size_t i = 0; i < currentWrongMap.size(); ++i) {
        printWrongBrief(i + 1, currentWrongMap[i]);
    }

    cout << "\n输入序号查看详情，或直接回车返回：";
    string line;
    getline(cin, line);
    line = trim(line);
    if (!line.empty()) {
        int idx;
        if (parseInt(line, idx) && idx >= 1 && idx <= static_cast<int>(currentWrongMap.size())) {
            printWrongDetail(currentWrongMap[idx - 1]);
        } else {
            cout << "序号无效。\n";
        }
    }
    pauseScreen();
}

// ========== 错题筛选逻辑 ==========

void queryWrongMultiCondition() {
  clearScreen();
  cout << "==============================\n";
  cout << "       多条件组合查询\n";
  cout << "==============================\n";
  cout << "提示：任何条件直接回车即表示不限。\n\n";

  cout << "请输入学科（回车跳过）：";
  string subject;
  getline(cin, subject);
  subject = trim(subject);

  cout << "请输入章节（回车跳过）：";
  string chapter;
  getline(cin, chapter);
  chapter = trim(chapter);

  cout << "请输入关键字（回车跳过）：";
  string keyword;
  getline(cin, keyword);
  keyword = trim(keyword);

  currentWrongMap.clear();
  for (size_t i = 0; i < wrongs.size(); ++i) {
    if (wrongs[i].userId != currentUserId || !wrongs[i].active) continue;
    
    bool match = true;
    if (!subject.empty() && wrongs[i].subject != subject) match = false;
    if (!chapter.empty() && wrongs[i].chapter != chapter) match = false;
    if (!keyword.empty()) {
        if (wrongs[i].question.find(keyword) == string::npos && 
            wrongs[i].correctAnswer.find(keyword) == string::npos && 
            wrongs[i].reason.find(keyword) == string::npos) {
            match = false;
        }
    }
    
    if (match) {
        currentWrongMap.push_back(i);
    }
  }

  if (currentWrongMap.empty()) {
      cout << "\n未找到符合所有条件的错题。\n";
  } else {
      cout << "\n找到 " << currentWrongMap.size() << " 道符合条件的错题：\n\n";
      for (size_t i = 0; i < currentWrongMap.size(); ++i) {
          printWrongBrief(i + 1, currentWrongMap[i]);
      }
      
      cout << "\n输入序号查看详情，或直接回车返回：";
      string line;
      getline(cin, line);
      line = trim(line);
      if (!line.empty()) {
        int idx;
        if (parseInt(line, idx) && idx >= 1 && idx <= static_cast<int>(currentWrongMap.size())) {
          printWrongDetail(currentWrongMap[idx - 1]);
        } else {
          cout << "序号无效。\n";
        }
      }
  }
  pauseScreen();
}

vector<int> filterWrongsBySubject(const string& subject) {
    vector<int> res;
    for (size_t i = 0; i < wrongs.size(); ++i) {
        if (wrongs[i].userId == currentUserId && wrongs[i].active && wrongs[i].subject == subject) {
            res.push_back(i);
        }
    }
    return res;
}

vector<int> filterWrongsByChapter(const string& chapter) {
    vector<int> res;
    for (size_t i = 0; i < wrongs.size(); ++i) {
        if (wrongs[i].userId == currentUserId && wrongs[i].active && wrongs[i].chapter == chapter) {
            res.push_back(i);
        }
    }
    return res;
}

// ========== 分类查看 ==========

void viewWrongsByCategory() {
    clearScreen();
    cout << "==============================\n";
    cout << "     按分类查看错题\n";
    cout << "==============================\n";
    cout << "1. 按学科查看\n";
    cout << "2. 按章节查看\n";
    cout << "0. 返回\n";
    cout << "请选择：";

    string line;
    getline(cin, line);
    int choice;
    if (!parseInt(line, choice)) {
        cout << "输入无效。\n";
        pauseScreen();
        return;
    }

    if (choice == 0) return;
    if (choice < 1 || choice > 2) {
        cout << "菜单选项不存在。\n";
        pauseScreen();
        return;
    }

    set<string> categories;
    for (size_t i = 0; i < wrongs.size(); ++i) {
        const WrongQuestion& w = wrongs[i];
        if (w.userId != currentUserId || !w.active) continue;
        if (choice == 1) categories.insert(w.subject);
        else categories.insert(w.chapter);
    }

    if (categories.empty()) {
        cout << "当前没有错题数据。\n";
        pauseScreen();
        return;
    }

    string label = (choice == 1) ? "学科" : "章节";
    cout << "\n当前 " << label << " 列表：\n";
    int idx = 1;
    vector<string> catList(categories.begin(), categories.end());
    for (const string& cat : catList) {
        cout << "  " << idx++ << ". " << cat << "\n";
    }

    cout << "\n输入序号查看该分类下的错题，或 0 返回：";
    getline(cin, line);
    int catChoice;
    if (!parseInt(line, catChoice) || catChoice < 0 || catChoice > static_cast<int>(catList.size())) {
        cout << "输入无效。\n";
        pauseScreen();
        return;
    }
    if (catChoice == 0) return;

    string selected = catList[catChoice - 1];
    cout << "\n" << label << "：" << selected << "\n\n";

    currentWrongMap.clear();
    for (size_t i = 0; i < wrongs.size(); ++i) {
        const WrongQuestion& w = wrongs[i];
        if (w.userId != currentUserId || !w.active) continue;
        bool match = (choice == 1) ? (w.subject == selected) : (w.chapter == selected);
        if (match) {
            currentWrongMap.push_back(static_cast<int>(i));
        }
    }
    
    for (size_t i = 0; i < currentWrongMap.size(); ++i) {
        printWrongBrief(i + 1, currentWrongMap[i]);
    }
    
    cout << "\n共 " << currentWrongMap.size() << " 条错题。\n";
    
    cout << "\n输入序号查看详情，或直接回车返回：";
    getline(cin, line);
    line = trim(line);
    if (!line.empty()) {
        int idx;
        if (parseInt(line, idx) && idx >= 1 && idx <= static_cast<int>(currentWrongMap.size())) {
            printWrongDetail(currentWrongMap[idx - 1]);
        } else {
            cout << "序号无效。\n";
        }
    }
    pauseScreen();
}

// ========== 查看全部错题 ==========

void viewAllWrongs() {
    clearScreen();
    cout << "==============================\n";
    cout << "     全部错题记录\n";
    cout << "==============================\n";

    currentWrongMap.clear();
    for (size_t i = 0; i < wrongs.size(); ++i) {
        if (wrongs[i].userId == currentUserId && wrongs[i].active) {
            currentWrongMap.push_back(static_cast<int>(i));
        }
    }

    if (currentWrongMap.empty()) {
        cout << "当前没有错题。\n";
    } else {
        for (size_t i = 0; i < currentWrongMap.size(); ++i) {
            printWrongBrief(i + 1, currentWrongMap[i]);
        }
        cout << "\n共 " << currentWrongMap.size() << " 条错题。\n";

        cout << "\n输入序号查看详情，或直接回车返回：";
        string line;
        if (!getline(cin, line)) return;
        line = trim(line);
        if (!line.empty()) {
            int idx;
            if (parseInt(line, idx) && idx >= 1 && idx <= static_cast<int>(currentWrongMap.size())) {
                printWrongDetail(currentWrongMap[idx - 1]);
            } else {
                cout << "序号无效。\n";
            }
        }
    }
    pauseScreen();
}

// ========== 错题转卡片 ==========

void convertWrongToCard() {
    clearScreen();
    cout << "==============================\n";
    cout << "     错题转知识卡片\n";
    cout << "==============================\n";

    if (currentWrongMap.empty()) {
        cout << "当前没有展示列表，正在自动加载全部错题...\n\n";
        for (size_t i = 0; i < wrongs.size(); ++i) {
            if (wrongs[i].userId == currentUserId && wrongs[i].active) {
                currentWrongMap.push_back(i);
            }
        }
        for (size_t i = 0; i < currentWrongMap.size(); ++i) {
            printWrongBrief(i + 1, currentWrongMap[i]);
        }
        cout << "\n";
    } else {
        cout << "（当前使用的是最近一次查询/查看的列表，共 " << currentWrongMap.size() << " 条记录）\n\n";
    }

    if (currentWrongMap.empty()) {
        cout << "未找到任何可用错题。\n";
        pauseScreen();
        return;
    }

    cout << "请输入要转换的错题序号（1~" << currentWrongMap.size() << "，直接回车取消）：";
    string line;
    getline(cin, line);
    line = trim(line);
    if (line.empty()) return;

    int displayIdx;
    if (!parseInt(line, displayIdx) || displayIdx < 1 || displayIdx > static_cast<int>(currentWrongMap.size())) {
        cout << "序号无效。\n";
        pauseScreen();
        return;
    }

    int found = currentWrongMap[displayIdx - 1];
    WrongQuestion& w = wrongs[found];

    if (w.linkedCardId != -1 && hasVisibleLinkedCard(w.linkedCardId)) {
        cout << "该错题已关联卡片（卡片编号：" << w.linkedCardId << "），无法重复转换。\n";
        pauseScreen();
        return;
    }

    if (w.linkedCardId != -1) {
        // 临时兼容原因：旧数据可能保留了失效 linkedCardId；重新生成可见卡片后覆盖关联。
        // 移除条件：所有旧数据都经过 maintenance --fix 或版本迁移后，可改为提前清理。
        cout << "检测到该错题关联的卡片不存在或不可见，将重新生成卡片并修复关联。\n";
    }

    Card c;
    c.cardId = getNextCardId();
    c.userId = currentUserId;
    c.subject = w.subject;
    c.chapter = w.chapter;
    c.title = "[错题转化] " + w.subject + " - " + w.chapter;
    c.front = w.question;
    c.back = w.correctAnswer;
    if (!w.reason.empty()) {
        c.back += "；错因分析：" + w.reason;
    }
    c.tags = "错题转化";
    // 错题转化卡片默认高难度，让它在复习和排序中保持足够显著。
    c.difficulty = 5;
    c.mastery = 50;
    c.reviewCount = 0;
    c.correctStreak = 0;
    c.intervalDays = 1;
    c.createDate = getTodayDate();
    c.lastReviewDate = "";
    c.nextReviewDate = c.createDate;
    c.active = true;

    cards.push_back(c);
    saveCards();
    resetCardDisplayCache();

    // 先保存新卡片，再写回 linkedCardId；即使后续错题保存失败，也不会留下悬空卡片引用。
    w.linkedCardId = c.cardId;
    saveWrongs();

    cout << "\n转换成功！已生成新卡片（编号：" << c.cardId << "）。\n";
    pauseScreen();
}

// ========== 错题管理子菜单 ==========

void showWrongMenu() {
    while (true) {
        clearScreen();
        cout << "==============================\n";
        cout << "       错题管理\n";
        cout << "==============================\n";
        cout << "1. 记录错题\n";
        cout << "2. 修改错题\n";
        cout << "3. 删除错题\n";
        cout << "4. 查看最近列表详情\n";
        cout << "5. 按关键字查询\n";
        cout << "6. 分类查看\n";
        cout << "7. 多条件组合查询\n";
        cout << "8. 错题转知识卡片\n";
        cout << "9. 查看全部错题\n";
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
            case 1: addWrong();              break;
            case 2: editWrong();             break;
            case 3: deleteWrong();           break;
            case 4: queryWrongById();        break;
            case 5: queryWrongByKeyword();   break;
            case 6: viewWrongsByCategory();  break;
            case 7: queryWrongMultiCondition(); break;
            case 8: convertWrongToCard();    break;
            case 9: viewAllWrongs();         break;
            case 0: return;
            default:
                cout << "菜单选项不存在。\n";
                pauseScreen();
        }
    }
}
