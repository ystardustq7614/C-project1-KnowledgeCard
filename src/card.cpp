#include "card.h"
#include "globals.h"
#include "storage.h"
#include "utils.h"
#include <iostream>
#include <set>
#include <vector>
#include <algorithm>

using namespace std;

/*
[导读]
- 本文件负责知识卡片的交互式管理，是学习“菜单输入 -> 内存对象 -> 文件保存”的核心样例。

[对应流程图]
- 新增/修改/删除：控制台输入 -> Card 字段变化 -> saveCards()。
- 查询/分类/排序：cards 容器 -> currentCardMap 展示快照 -> 用户按展示序号查看详情。

[输入输出]
- 输入：当前登录用户、控制台字段输入、cards 全局容器。
- 输出：cards 容器变化、cards.txt、控制台列表。

[学习重点]
- currentCardMap 保存的是“展示序号 -> cards 下标”，不是 cardId。
- 长文本字段通过 readTextInput 允许 | 和多行，最终由 storage.cpp 统一转义。

[易错点]
- currentCardMap 只是最近一次列表快照；新增、删除、错题转卡片后需要清理或重建。

[实验]
- 先查询得到一个列表，再新增或删除卡片，观察 resetCardDisplayCache() 为什么有必要。
*/

// ========== 表现层映射 ==========
// 保存最近一次列表中每个展示序号对应的 cards 下标，避免要求用户输入全局 cardId。
static vector<int> currentCardMap;

void resetCardDisplayCache() {
  currentCardMap.clear();
}

// ========== 卡片打印 ==========

void printCardBrief(int displayIdx, int realIdx) {
  const Card &c = cards[realIdx];
  cout << "  [" << displayIdx << "] " << c.title << " | " << c.subject
       << " | 难度:" << c.difficulty << " | 掌握:" << c.mastery
       << " | 复习:" << c.reviewCount << "次"
       << "\n";
}

void printCardDetail(int index) {
  const Card &c = cards[index];
  cout << "------------------------------\n";
  // cout << "卡片编号：" << c.cardId << "\n"; // 隐藏内部 ID
  cout << "学    科：" << c.subject << "\n";
  cout << "章    节：" << c.chapter << "\n";
  cout << "标    题：" << c.title << "\n";
  cout << "正    面：" << c.front << "\n";
  cout << "背    面：" << c.back << "\n";
  cout << "标    签：" << c.tags << "\n";
  cout << "难    度：" << c.difficulty << "\n";
  cout << "掌 握 度：" << c.mastery << "\n";
  cout << "复习次数：" << c.reviewCount << "\n";
  cout << "连续答对：" << c.correctStreak << "\n";
  cout << "当前间隔：" << c.intervalDays << " 天\n";
  cout << "创建日期：" << c.createDate << "\n";
  cout << "最近复习："
       << (c.lastReviewDate.empty() ? "尚未复习" : c.lastReviewDate) << "\n";
  cout << "下次复习：" << c.nextReviewDate << "\n";
  cout << "------------------------------\n";
}

// ========== 输入辅助：读取非空字符串 ==========

// 单行短字段仍禁止 |，保持学科/章节/标题等分类字段易于人工查看和筛选。
static bool readNonEmptyLine(const string &prompt, string &out) {
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

// 空字符串既可表示“可选字段为空”，也可表示编辑时“保留原值”，调用方按场景解释。
static string readOptionalLine(const string &prompt) {
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
  // 长文本字段交给 readTextInput，允许 | 和多行；存储层负责转义，不在交互层重复限制。
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

// 读取难度值（1-5），返回 true 表示成功
static bool readDifficulty(const string &prompt, int &out) {
  cout << prompt;
  string line;
  getline(cin, line);
  int val;
  if (!parseInt(line, val) || val < 1 || val > 5) {
    cout << "难度必须为 1~5 的整数。\n";
    return false;
  }
  out = val;
  return true;
}

// ========== 新增卡片 ==========

void addCard() {
  clearScreen();
  cout << "==============================\n";
  cout << "       新增知识卡片\n";
  cout << "==============================\n";

  Card c;
  c.userId = currentUserId;

  if (!readNonEmptyLine("学科：", c.subject)) {
    pauseScreen();
    return;
  }
  if (!readNonEmptyLine("章节：", c.chapter)) {
    pauseScreen();
    return;
  }
  if (!readNonEmptyLine("标题：", c.title)) {
    pauseScreen();
    return;
  }
  if (!readRequiredTextField("正面内容（问题，可输入 |；多行先输入 .multi）：", c.front)) {
    pauseScreen();
    return;
  }
  if (!readRequiredTextField("背面内容（答案，可输入 |；多行先输入 .multi）：", c.back)) {
    pauseScreen();
    return;
  }

  c.tags = readOptionalLine("标签（可选，直接回车跳过）：");

  if (!readDifficulty("难度（1~5）：", c.difficulty)) {
    pauseScreen();
    return;
  }

  // 新卡片当天即可复习，避免用户录入后看不到任何今日任务。
  c.cardId = getNextCardId();
  c.mastery = 50;
  c.reviewCount = 0;
  c.correctStreak = 0;
  c.intervalDays = 1;
  c.createDate = getTodayDate();
  c.lastReviewDate = "";
  c.nextReviewDate = c.createDate; // 创建当天即可复习
  c.active = true;

  cards.push_back(c);
  saveCards();

  cout << "\n卡片创建成功！编号：" << c.cardId << "\n";
  pauseScreen();
}

// ========== 修改卡片 ==========

void editCard() {
  clearScreen();
  cout << "==============================\n";
  cout << "       修改知识卡片\n";
  cout << "==============================\n";

  if (currentCardMap.empty()) {
    // 没有最近列表时自动加载全部有效卡片，保证编辑入口可独立使用。
    cout << "当前没有展示列表，正在自动加载全部卡片...\n\n";
    for (size_t i = 0; i < cards.size(); ++i) {
      if (cards[i].userId == currentUserId && cards[i].active) {
        currentCardMap.push_back(i);
      }
    }
    for (size_t i = 0; i < currentCardMap.size(); ++i) {
      printCardBrief(i + 1, currentCardMap[i]);
    }
    cout << "\n";
  } else {
    cout << "（当前使用的是最近一次查询/查看的列表，共 " << currentCardMap.size() << " 条记录）\n\n";
  }

  if (currentCardMap.empty()) {
    cout << "未找到任何可用卡片。\n";
    pauseScreen();
    return;
  }

  cout << "请输入要修改的卡片序号（1~" << currentCardMap.size() << "，直接回车取消）：";
  string line;
  getline(cin, line);
  line = trim(line);
  if (line.empty()) return;

  int displayIdx;
  if (!parseInt(line, displayIdx) || displayIdx < 1 || displayIdx > static_cast<int>(currentCardMap.size())) {
    cout << "序号无效。\n";
    pauseScreen();
    return;
  }

  int found = currentCardMap[displayIdx - 1];

  cout << "\n当前卡片信息：\n";
  printCardDetail(found);

  cout << "\n修改提示：直接回车保留原值。\n\n";

  Card &c = cards[found];
  string input;

  input = readOptionalLine("学科 [" + c.subject + "]：");
  if (!input.empty())
    c.subject = input;

  input = readOptionalLine("章节 [" + c.chapter + "]：");
  if (!input.empty())
    c.chapter = input;

  input = readOptionalLine("标题 [" + c.title + "]：");
  if (!input.empty())
    c.title = input;

  input = readOptionalTextField("正面内容 [" + c.front + "]（可输入 |；多行先输入 .multi）：");
  if (!input.empty())
    c.front = input;

  input = readOptionalTextField("背面内容 [" + c.back + "]（可输入 |；多行先输入 .multi）：");
  if (!input.empty())
    c.back = input;

  input = readOptionalLine("标签 [" + c.tags + "]：");
  if (!input.empty())
    c.tags = input;

  // 难度必须保留 1~5 的业务范围；非法输入不打断整次编辑，直接保留原值。
  cout << "难度 [" << c.difficulty << "]（直接回车保留）：";
  getline(cin, input);
  input = trim(input);
  if (!input.empty()) {
    int val;
    if (parseInt(input, val) && val >= 1 && val <= 5) {
      c.difficulty = val;
    } else {
      cout << "难度输入无效，保留原值。\n";
    }
  }

  saveCards();
  cout << "\n卡片修改成功！\n";
  pauseScreen();
}

// ========== 删除卡片（逻辑删除） ==========

void deleteCard() {
  clearScreen();
  cout << "==============================\n";
  cout << "       删除知识卡片\n";
  cout << "==============================\n";

  if (currentCardMap.empty()) {
    cout << "当前没有展示列表，正在自动加载全部卡片...\n\n";
    for (size_t i = 0; i < cards.size(); ++i) {
      if (cards[i].userId == currentUserId && cards[i].active) {
        currentCardMap.push_back(i);
      }
    }
    for (size_t i = 0; i < currentCardMap.size(); ++i) {
      printCardBrief(i + 1, currentCardMap[i]);
    }
    cout << "\n";
  } else {
    cout << "（当前使用的是最近一次查询/查看的列表，共 " << currentCardMap.size() << " 条记录）\n\n";
  }

  if (currentCardMap.empty()) {
    cout << "未找到任何可用卡片。\n";
    pauseScreen();
    return;
  }

  cout << "请输入要删除的卡片序号（1~" << currentCardMap.size() << "，直接回车取消）：";
  string line;
  getline(cin, line);
  line = trim(line);
  if (line.empty()) return;

  int displayIdx;
  if (!parseInt(line, displayIdx) || displayIdx < 1 || displayIdx > static_cast<int>(currentCardMap.size())) {
    cout << "序号无效。\n";
    pauseScreen();
    return;
  }

  int found = currentCardMap[displayIdx - 1];

  cout << "\n即将删除以下卡片：\n";
  printCardDetail(found);

  cout << "确认删除？(y/n)：";
  getline(cin, line);
  line = trim(line);
  if (line != "y" && line != "Y") {
    cout << "已取消删除。\n";
    pauseScreen();
    return;
  }

  // 逻辑删除保留复习历史和可恢复能力；物理删除只在维护模块的回收站流程中执行。
  cards[found].active = false;
  saveCards();
  cout << "卡片已删除。\n";
  pauseScreen();
}

// ========== 查看列表详情 ==========

void queryCardById() {
  clearScreen();
  cout << "==============================\n";
  cout << "       查看卡片详情\n";
  cout << "==============================\n";

  if (currentCardMap.empty()) {
    cout << "当前没有展示列表，正在自动加载全部卡片...\n\n";
    for (size_t i = 0; i < cards.size(); ++i) {
      if (cards[i].userId == currentUserId && cards[i].active) {
        currentCardMap.push_back(i);
      }
    }
    for (size_t i = 0; i < currentCardMap.size(); ++i) {
      printCardBrief(i + 1, currentCardMap[i]);
    }
    cout << "\n";
  } else {
    cout << "（当前使用的是最近一次查询/查看的列表，共 " << currentCardMap.size() << " 条记录）\n\n";
  }

  if (currentCardMap.empty()) {
    cout << "未找到任何可用卡片。\n";
    pauseScreen();
    return;
  }

  cout << "请输入要查看的卡片序号（1~" << currentCardMap.size() << "，直接回车取消）：";
  string line;
  getline(cin, line);
  line = trim(line);
  if (line.empty()) return;

  int displayIdx;
  if (!parseInt(line, displayIdx) || displayIdx < 1 || displayIdx > static_cast<int>(currentCardMap.size())) {
    cout << "序号无效。\n";
    pauseScreen();
    return;
  }

  printCardDetail(currentCardMap[displayIdx - 1]);
  pauseScreen();
}

// ========== 按关键字查询 ==========

void queryCardByKeyword() {
  clearScreen();
  cout << "==============================\n";
  cout << "   按关键字查询知识卡片\n";
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

  // 关键字搜索覆盖用户最可能记得的内容字段；不搜索内部 ID，避免暴露存储实现。
  currentCardMap.clear();
  for (size_t i = 0; i < cards.size(); ++i) {
    const Card &c = cards[i];
    if (c.userId != currentUserId || !c.active)
      continue;
    if (containsKeyword(c.title, keyword) ||
        containsKeyword(c.front, keyword) || containsKeyword(c.back, keyword) ||
        containsKeyword(c.tags, keyword)) {
      currentCardMap.push_back(static_cast<int>(i));
    }
  }

  if (currentCardMap.empty()) {
    cout << "未找到匹配的卡片。\n";
    pauseScreen();
    return;
  }

  cout << "\n共找到 " << currentCardMap.size() << " 条结果：\n\n";
  for (size_t i = 0; i < currentCardMap.size(); ++i) {
    printCardBrief(i + 1, currentCardMap[i]);
  }

  // 可选：查看某条详情
  cout << "\n输入序号查看详情，或直接回车返回：";
  string line;
  getline(cin, line);
  line = trim(line);
  if (!line.empty()) {
    int idx;
    if (parseInt(line, idx) && idx >= 1 && idx <= static_cast<int>(currentCardMap.size())) {
      printCardDetail(currentCardMap[idx - 1]);
    } else {
      cout << "序号无效。\n";
    }
  }
  pauseScreen();
}

// ========== 多条件组合查询 ==========

void queryCardMultiCondition() {
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

  currentCardMap.clear();
  for (size_t i = 0; i < cards.size(); ++i) {
    if (cards[i].userId != currentUserId || !cards[i].active) continue;
    
    bool match = true;
    if (!subject.empty() && cards[i].subject != subject) match = false;
    if (!chapter.empty() && cards[i].chapter != chapter) match = false;
    if (!keyword.empty()) {
        if (cards[i].title.find(keyword) == string::npos && 
            cards[i].front.find(keyword) == string::npos && 
            cards[i].back.find(keyword) == string::npos &&
            cards[i].tags.find(keyword) == string::npos) {
            match = false;
        }
    }
    
    if (match) {
        currentCardMap.push_back(i);
    }
  }

  if (currentCardMap.empty()) {
      cout << "\n未找到符合所有条件的卡片。\n";
  } else {
      cout << "\n找到 " << currentCardMap.size() << " 张符合条件的卡片：\n\n";
      for (size_t i = 0; i < currentCardMap.size(); ++i) {
          printCardBrief(i + 1, currentCardMap[i]);
      }
      
      cout << "\n输入序号查看详情，或直接回车返回：";
      string line;
      getline(cin, line);
      line = trim(line);
      if (!line.empty()) {
        int idx;
        if (parseInt(line, idx) && idx >= 1 && idx <= static_cast<int>(currentCardMap.size())) {
          printCardDetail(currentCardMap[idx - 1]);
        } else {
          cout << "序号无效。\n";
        }
      }
  }
  pauseScreen();
}

// ========== 分类查看 ==========

void viewCardsByCategory() {
  clearScreen();
  cout << "==============================\n";
  cout << "     按分类查看知识卡片\n";
  cout << "==============================\n";
  cout << "1. 按学科查看\n";
  cout << "2. 按章节查看\n";
  cout << "3. 按标签查看\n";
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

  if (choice == 0)
    return;
  if (choice < 1 || choice > 3) {
    cout << "菜单选项不存在。\n";
    pauseScreen();
    return;
  }

  // 收集当前用户有效卡片的分类值
  set<string> categories;
  for (size_t i = 0; i < cards.size(); ++i) {
    const Card &c = cards[i];
    if (c.userId != currentUserId || !c.active)
      continue;
    switch (choice) {
    case 1:
      categories.insert(c.subject);
      break;
    case 2:
      categories.insert(c.chapter);
      break;
    case 3:
      categories.insert(c.tags);
      break;
    }
  }

  if (categories.empty()) {
    cout << "当前没有卡片数据。\n";
    pauseScreen();
    return;
  }

  string label = (choice == 1) ? "学科" : (choice == 2) ? "章节" : "标签";
  cout << "\n当前 " << label << " 列表：\n";
  int idx = 1;
  vector<string> catList(categories.begin(), categories.end());
  for (const string &cat : catList) {
    cout << "  " << idx++ << ". " << cat << "\n";
  }

  cout << "\n输入序号查看该分类下的卡片，或 0 返回：";
  getline(cin, line);
  int catChoice;
  if (!parseInt(line, catChoice) || catChoice < 0 ||
      catChoice > static_cast<int>(catList.size())) {
    cout << "输入无效。\n";
    pauseScreen();
    return;
  }
  if (catChoice == 0)
    return;

  string selected = catList[catChoice - 1];
  cout << "\n" << label << "：" << selected << "\n\n";

  currentCardMap.clear();
  for (size_t i = 0; i < cards.size(); ++i) {
    const Card &c = cards[i];
    if (c.userId != currentUserId || !c.active)
      continue;
    bool match = false;
    switch (choice) {
    case 1: match = (c.subject == selected); break;
    case 2: match = (c.chapter == selected); break;
    case 3: match = (c.tags == selected); break;
    }
    if (match) {
      currentCardMap.push_back(static_cast<int>(i));
    }
  }
  
  for (size_t i = 0; i < currentCardMap.size(); ++i) {
    printCardBrief(i + 1, currentCardMap[i]);
  }
  
  cout << "\n共 " << currentCardMap.size() << " 条卡片。\n";
  
  cout << "\n输入序号查看详情，或直接回车返回：";
  getline(cin, line);
  line = trim(line);
  if (!line.empty()) {
    int idx;
    if (parseInt(line, idx) && idx >= 1 && idx <= static_cast<int>(currentCardMap.size())) {
      printCardDetail(currentCardMap[idx - 1]);
    } else {
      cout << "序号无效。\n";
    }
  }
  pauseScreen();
}

// ========== 查看全部卡片 ==========

void viewAllCards() {
  clearScreen();
  cout << "==============================\n";
  cout << "     全部知识卡片\n";
  cout << "==============================\n";

  currentCardMap.clear();
  for (size_t i = 0; i < cards.size(); ++i) {
    if (cards[i].userId == currentUserId && cards[i].active) {
      currentCardMap.push_back(static_cast<int>(i));
    }
  }

  if (currentCardMap.empty()) {
    cout << "当前没有卡片。\n";
  } else {
    for (size_t i = 0; i < currentCardMap.size(); ++i) {
      printCardBrief(i + 1, currentCardMap[i]);
    }
    cout << "\n共 " << currentCardMap.size() << " 条卡片。\n";

    // 可选：查看某条详情
    cout << "\n输入序号查看详情，或直接回车返回：";
    string line;
    if (!getline(cin, line)) return;
    line = trim(line);
    if (!line.empty()) {
      int idx;
      if (parseInt(line, idx) && idx >= 1 && idx <= static_cast<int>(currentCardMap.size())) {
        printCardDetail(currentCardMap[idx - 1]);
      } else {
        cout << "序号无效。\n";
      }
    }
  }
  pauseScreen();
}

// ========== 筛选与排序逻辑 ==========

vector<int> filterCardsBySubject(const string& subject) {
    vector<int> res;
    for (size_t i = 0; i < cards.size(); ++i) {
        if (cards[i].userId == currentUserId && cards[i].active && cards[i].subject == subject) {
            res.push_back(i);
        }
    }
    return res;
}

vector<int> filterCardsByChapter(const string& chapter) {
    vector<int> res;
    for (size_t i = 0; i < cards.size(); ++i) {
        if (cards[i].userId == currentUserId && cards[i].active && cards[i].chapter == chapter) {
            res.push_back(i);
        }
    }
    return res;
}

vector<int> filterCardsByTag(const string& tag) {
    vector<int> res;
    for (size_t i = 0; i < cards.size(); ++i) {
        if (cards[i].userId == currentUserId && cards[i].active && cards[i].tags == tag) {
            res.push_back(i);
        }
    }
    return res;
}

void sortCardsByCreateDate(vector<int>& indexes, bool ascending) {
    sort(indexes.begin(), indexes.end(), [ascending](int a, int b) {
        if (ascending) return compareDate(cards[a].createDate, cards[b].createDate) < 0;
        return compareDate(cards[a].createDate, cards[b].createDate) > 0;
    });
}

void sortCardsByNextReviewDate(vector<int>& indexes, bool ascending) {
    sort(indexes.begin(), indexes.end(), [ascending](int a, int b) {
        if (ascending) return compareDate(cards[a].nextReviewDate, cards[b].nextReviewDate) < 0;
        return compareDate(cards[a].nextReviewDate, cards[b].nextReviewDate) > 0;
    });
}

void sortCardsByMastery(vector<int>& indexes, bool ascending) {
    sort(indexes.begin(), indexes.end(), [ascending](int a, int b) {
        if (ascending) return cards[a].mastery < cards[b].mastery;
        return cards[a].mastery > cards[b].mastery;
    });
}

void viewCardsBySorting() {
    clearScreen();
    cout << "==============================\n";
    cout << "     排序查看知识卡片\n";
    cout << "==============================\n";
    cout << "1. 按创建时间升序\n";
    cout << "2. 按创建时间降序\n";
    cout << "3. 按下次复习时间升序\n";
    cout << "4. 按下次复习时间降序\n";
    cout << "5. 按掌握度升序\n";
    cout << "6. 按掌握度降序\n";
    cout << "0. 返回\n";
    cout << "请选择：";

    string line;
    if (!getline(cin, line)) return;
    int choice;
    if (!parseInt(line, choice)) {
        cout << "输入无效。\n";
        pauseScreen();
        return;
    }

    if (choice == 0) return;
    if (choice < 1 || choice > 6) {
        cout << "菜单选项不存在。\n";
        pauseScreen();
        return;
    }

    vector<int> indexes;
    for (size_t i = 0; i < cards.size(); ++i) {
        if (cards[i].userId == currentUserId && cards[i].active) {
            indexes.push_back(i);
        }
    }

    if (indexes.empty()) {
        cout << "当前没有卡片数据。\n";
        pauseScreen();
        return;
    }

    switch (choice) {
        case 1: sortCardsByCreateDate(indexes, true); break;
        case 2: sortCardsByCreateDate(indexes, false); break;
        case 3: sortCardsByNextReviewDate(indexes, true); break;
        case 4: sortCardsByNextReviewDate(indexes, false); break;
        case 5: sortCardsByMastery(indexes, true); break;
        case 6: sortCardsByMastery(indexes, false); break;
    }

    cout << "\n排序结果：\n\n";
    // 排序结果继续写入展示映射，后续“查看最近列表详情/修改/删除”沿用同一序号。
    currentCardMap = indexes;
    for (size_t i = 0; i < currentCardMap.size(); ++i) {
        printCardBrief(i + 1, currentCardMap[i]);
    }
    
    // 可选：查看某条详情
    cout << "\n输入序号查看详情，或直接回车返回：";
    getline(cin, line);
    line = trim(line);
    if (!line.empty()) {
      int idx;
      if (parseInt(line, idx) && idx >= 1 && idx <= static_cast<int>(currentCardMap.size())) {
        printCardDetail(currentCardMap[idx - 1]);
      } else {
        cout << "序号无效。\n";
      }
    }
    pauseScreen();
}

// ========== 卡片管理子菜单 ==========

void showCardMenu() {
  while (true) {
    clearScreen();
    cout << "==============================\n";
    cout << "     知识卡片管理\n";
    cout << "==============================\n";
    cout << "1. 新增卡片\n";
    cout << "2. 修改卡片\n";
    cout << "3. 删除卡片\n";
    cout << "4. 查看最近列表详情\n";
    cout << "5. 按关键字查询\n";
    cout << "6. 分类查看\n";
    cout << "7. 排序查看卡片\n";
    cout << "8. 多条件组合查询\n";
    cout << "9. 查看全部卡片\n";
    cout << "0. 返回主菜单\n";
    cout << "请选择：";

    string line;
    getline(cin, line);
    int choice;
    if (!parseInt(line, choice)) {
      cout << "输入无效，请重新输入。\n";
      pauseScreen();
      continue;
    }

    switch (choice) {
    case 1:
      addCard();
      break;
    case 2:
      editCard();
      break;
    case 3:
      deleteCard();
      break;
    case 4:
      queryCardById();
      break;
    case 5:
      queryCardByKeyword();
      break;
    case 6:
      viewCardsByCategory();
      break;
    case 7:
      viewCardsBySorting();
      break;
    case 8:
      queryCardMultiCondition();
      break;
    case 9:
      viewAllCards();
      break;
    case 0:
      return;
    default:
      cout << "菜单选项不存在。\n";
      pauseScreen();
    }
  }
}
