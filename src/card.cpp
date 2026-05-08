// 引入卡片模块头文件：公开函数声明和 Card 相关接口都在这里。
#include "card.h"

// 引入全局状态：cards 全局容器、currentUserId 等登录态数据来自这里。
#include "globals.h"

// 引入存储层接口：saveCards/getNextCardId 等文件读写能力来自这里。
#include "storage.h"

// 引入通用工具：trim、parseInt、readTextInput、日期包装、暂停/清屏等。
#include "utils.h"

// iostream 提供 cin/cout，用于控制台交互。
#include <iostream>

// set 用于收集不重复的学科/章节/标签分类。
#include <set>

// vector 用于展示序号映射、筛选结果和排序结果。
#include <vector>

// algorithm 提供 sort，用于排序查看卡片。
#include <algorithm>

// 只引入本文件实际使用的标准库名字，避免命名空间整体引入污染。
using std::cin;
using std::cout;
using std::getline;
using std::set;
using std::sort;
using std::string;
using std::vector;

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
- 所有列表只展示 currentUserId 且 active=true 的卡片，避免跨用户串数据或显示回收站记录。

[易错点]
- currentCardMap 只是最近一次列表快照；新增、删除、错题转卡片后需要清理或重建。
- readNonEmptyLine/readOptionalLine 禁止 |，是为了保护短字段的人工可读性；长文本字段例外。

[实验]
- 先查询得到一个列表，再新增或删除卡片，观察 resetCardDisplayCache() 为什么有必要。
- 在新增卡片时输入 .multi，再观察 cards.txt 如何由 storage.cpp 转义多行内容。
*/

// ========== 表现层映射 ==========
// 保存最近一次列表中每个展示序号对应的 cards 下标，避免要求用户输入全局 cardId。
static vector<int> currentCardMap;

void resetCardDisplayCache() {
  // 清空最近一次展示列表，避免其他模块新增/删除卡片后继续使用旧序号。
  currentCardMap.clear();
}

// ========== 卡片打印 ==========

void printCardBrief(int displayIdx, int realIdx) {
  // realIdx 是 cards 全局容器下标，不是 cardId，也不是展示序号。
  const Card &c = cards[realIdx];

  // 简表只输出列表选择需要的信息，详细字段交给 printCardDetail。
  cout << "  [" << displayIdx << "] " << c.title << " | " << c.subject
       << " | 难度:" << c.difficulty << " | 掌握:" << c.mastery
       << " | 复习:" << c.reviewCount << "次"
       << "\n";
}

void printCardDetail(int index) {
  // index 是 cards 全局容器下标；调用方必须确保它已经过 currentUserId/active 过滤。
  const Card &c = cards[index];

  // 详情页展示完整学习状态，方便用户决定是否修改、删除或复习。
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
  // 输出字段提示语。
  cout << prompt;

  // 读取整行，避免 cin >> 留下换行影响后续 getline。
  string line;
  getline(cin, line);

  // 短字段统一去掉首尾空白。
  line = trim(line);

  // 必填字段不允许为空。
  if (line.empty()) {
    cout << "输入不能为空。\n";
    return false;
  }

  // 短字段禁止 |，因为 cards.txt 用 | 分隔字段，短字段人工排查时也不应含分隔符。
  if (line.find('|') != string::npos) {
    cout << "输入不允许包含 | 字符。\n";
    return false;
  }

  // 校验通过后写入输出参数。
  out = line;

  // 返回 true 表示调用方可以继续流程。
  return true;
}

// 空字符串既可表示“可选字段为空”，也可表示编辑时“保留原值”，调用方按场景解释。
static string readOptionalLine(const string &prompt) {
  // 输出字段提示语。
  cout << prompt;

  // 读取整行输入。
  string line;
  getline(cin, line);

  // 可选短字段也去掉首尾空白。
  line = trim(line);

  // 可选短字段同样禁止 |；非法时返回空字符串，让调用方解释为“不修改/跳过”。
  if (line.find('|') != string::npos) {
    cout << "输入不允许包含 | 字符，已忽略本次输入。\n";
    return "";
  }

  // 返回用户输入；可以是空字符串。
  return line;
}

static bool readRequiredTextField(const string& prompt, string& out) {
  // 长文本字段交给 readTextInput，允许 | 和多行；存储层负责转义，不在交互层重复限制。
  string value = readTextInput(prompt);

  // 必填长文本在 trim 后不能是空内容。
  if (trim(value).empty()) {
    cout << "输入不能为空。\n";
    return false;
  }

  // 长文本保留原始内容，包括行内空格和多行换行。
  out = value;

  // 返回 true 表示读取成功。
  return true;
}

static string readOptionalTextField(const string& prompt) {
  // 可选长文本直接复用 readTextInput；空字符串由调用方解释为“保留原值”。
  return readTextInput(prompt);
}

// 读取难度值（1-5），返回 true 表示成功
static bool readDifficulty(const string &prompt, int &out) {
  // 输出难度提示。
  cout << prompt;

  // 读取整行，和其他交互输入保持一致。
  string line;
  getline(cin, line);

  // 保存解析出的整数。
  int val;

  // 难度必须是完整整数，并且落在 1~5。
  if (!parseInt(line, val) || val < 1 || val > 5) {
    cout << "难度必须为 1~5 的整数。\n";
    return false;
  }

  // 校验通过后写入输出参数。
  out = val;

  // 返回 true 表示难度可用。
  return true;
}

// ========== 新增卡片 ==========

void addCard() {
  // 进入新增页面前清屏，保持菜单式界面干净。
  clearScreen();
  cout << "==============================\n";
  cout << "       新增知识卡片\n";
  cout << "==============================\n";

  // 创建一个新的 Card 对象，后续逐项填充字段。
  Card c;

  // 卡片归属当前登录用户；这是多用户数据隔离的关键字段。
  c.userId = currentUserId;

  // 学科是必填短字段，用于分类查看和推荐聚合。
  if (!readNonEmptyLine("学科：", c.subject)) {
    pauseScreen();
    return;
  }

  // 章节是必填短字段，用于更细粒度的分类、统计和薄弱点推荐。
  if (!readNonEmptyLine("章节：", c.chapter)) {
    pauseScreen();
    return;
  }

  // 标题是必填短字段，用于列表页快速识别卡片。
  if (!readNonEmptyLine("标题：", c.title)) {
    pauseScreen();
    return;
  }

  // 正面内容是必填长文本，允许 | 和多行；例如题干、概念问题。
  if (!readRequiredTextField("正面内容（问题，可输入 |；多行先输入 .multi）：", c.front)) {
    pauseScreen();
    return;
  }

  // 背面内容是必填长文本，允许 | 和多行；例如答案、解释、公式推导。
  if (!readRequiredTextField("背面内容（答案，可输入 |；多行先输入 .multi）：", c.back)) {
    pauseScreen();
    return;
  }

  // 标签是可选短字段，用户可以直接回车跳过。
  c.tags = readOptionalLine("标签（可选，直接回车跳过）：");

  // 难度是必填数值字段，范围固定为 1~5。
  if (!readDifficulty("难度（1~5）：", c.difficulty)) {
    pauseScreen();
    return;
  }

  // 新卡片当天即可复习，避免用户录入后看不到任何今日任务。
  // 分配全局唯一卡片 ID；它用于存储和跨模块关联，不直接暴露给普通用户操作。
  c.cardId = getNextCardId();

  // 新卡片默认掌握度为 50，表示中性起点。
  c.mastery = 50;

  // 新卡片还没有复习记录。
  c.reviewCount = 0;

  // 新卡片还没有连续答对记录。
  c.correctStreak = 0;

  // 初始复习间隔为 1 天。
  c.intervalDays = 1;

  // 创建日期使用系统本地今天。
  c.createDate = getTodayDate();

  // 空字符串表示尚未复习。
  c.lastReviewDate = "";

  // 创建当天即可复习。
  c.nextReviewDate = c.createDate; // 创建当天即可复习

  // active=true 表示正常可见；删除时会改成 false。
  c.active = true;

  // 写入全局 cards 容器。
  cards.push_back(c);

  // 立即保存到 cards.txt，保证新增操作落盘。
  saveCards();

  // 提示创建成功；这里显示内部编号主要用于确认和调试。
  cout << "\n卡片创建成功！编号：" << c.cardId << "\n";
  pauseScreen();
}

// ========== 修改卡片 ==========

void editCard() {
  // 进入修改页面前清屏。
  clearScreen();
  cout << "==============================\n";
  cout << "       修改知识卡片\n";
  cout << "==============================\n";

  if (currentCardMap.empty()) {
    // 没有最近列表时自动加载全部有效卡片，保证编辑入口可独立使用。
    cout << "当前没有展示列表，正在自动加载全部卡片...\n\n";

    // 只加载当前用户且 active=true 的卡片。
    for (size_t i = 0; i < cards.size(); ++i) {
      if (cards[i].userId == currentUserId && cards[i].active) {
        currentCardMap.push_back(i);
      }
    }

    // 把自动加载的列表展示给用户，序号从 1 开始。
    for (size_t i = 0; i < currentCardMap.size(); ++i) {
      printCardBrief(i + 1, currentCardMap[i]);
    }
    cout << "\n";
  } else {
    // 如果已经有最近列表，沿用它，用户可以先查询/排序再修改指定结果。
    cout << "（当前使用的是最近一次查询/查看的列表，共 " << currentCardMap.size() << " 条记录）\n\n";
  }

  // 没有任何可编辑卡片时结束。
  if (currentCardMap.empty()) {
    cout << "未找到任何可用卡片。\n";
    pauseScreen();
    return;
  }

  // 用户输入的是展示序号，不是 cardId。
  cout << "请输入要修改的卡片序号（1~" << currentCardMap.size() << "，直接回车取消）：";
  string line;
  getline(cin, line);
  line = trim(line);

  // 直接回车表示取消修改。
  if (line.empty()) return;

  // 解析并校验展示序号范围。
  int displayIdx;
  if (!parseInt(line, displayIdx) || displayIdx < 1 || displayIdx > static_cast<int>(currentCardMap.size())) {
    cout << "序号无效。\n";
    pauseScreen();
    return;
  }

  // 把展示序号转换成 cards 全局容器下标。
  int found = currentCardMap[displayIdx - 1];

  // 先展示当前详情，避免用户改错对象。
  cout << "\n当前卡片信息：\n";
  printCardDetail(found);

  // 编辑模式下，空输入代表保留原值。
  cout << "\n修改提示：直接回车保留原值。\n\n";

  // 引用要修改的卡片对象，后续直接改全局容器中的数据。
  Card &c = cards[found];

  // 复用 input 接收每个字段的新值。
  string input;

  // 修改学科；空输入保留原值。
  input = readOptionalLine("学科 [" + c.subject + "]：");
  if (!input.empty())
    c.subject = input;

  // 修改章节；空输入保留原值。
  input = readOptionalLine("章节 [" + c.chapter + "]：");
  if (!input.empty())
    c.chapter = input;

  // 修改标题；空输入保留原值。
  input = readOptionalLine("标题 [" + c.title + "]：");
  if (!input.empty())
    c.title = input;

  // 修改正面长文本；空输入保留原值。
  input = readOptionalTextField("正面内容 [" + c.front + "]（可输入 |；多行先输入 .multi）：");
  if (!input.empty())
    c.front = input;

  // 修改背面长文本；空输入保留原值。
  input = readOptionalTextField("背面内容 [" + c.back + "]（可输入 |；多行先输入 .multi）：");
  if (!input.empty())
    c.back = input;

  // 修改标签；空输入保留原值。
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
      // 合法难度才写回。
      c.difficulty = val;
    } else {
      cout << "难度输入无效，保留原值。\n";
    }
  }

  // 修改完成后立即保存 cards.txt。
  saveCards();
  cout << "\n卡片修改成功！\n";
  pauseScreen();
}

// ========== 删除卡片（逻辑删除） ==========

void deleteCard() {
  // 进入删除页面前清屏。
  clearScreen();
  cout << "==============================\n";
  cout << "       删除知识卡片\n";
  cout << "==============================\n";

  if (currentCardMap.empty()) {
    cout << "当前没有展示列表，正在自动加载全部卡片...\n\n";

    // 没有最近列表时，自动构造当前用户全部有效卡片列表。
    for (size_t i = 0; i < cards.size(); ++i) {
      if (cards[i].userId == currentUserId && cards[i].active) {
        currentCardMap.push_back(i);
      }
    }

    // 展示可删除对象。
    for (size_t i = 0; i < currentCardMap.size(); ++i) {
      printCardBrief(i + 1, currentCardMap[i]);
    }
    cout << "\n";
  } else {
    // 沿用最近列表，方便用户先查询再删除。
    cout << "（当前使用的是最近一次查询/查看的列表，共 " << currentCardMap.size() << " 条记录）\n\n";
  }

  // 没有可删除卡片时结束。
  if (currentCardMap.empty()) {
    cout << "未找到任何可用卡片。\n";
    pauseScreen();
    return;
  }

  // 用户输入展示序号。
  cout << "请输入要删除的卡片序号（1~" << currentCardMap.size() << "，直接回车取消）：";
  string line;
  getline(cin, line);
  line = trim(line);

  // 直接回车表示取消。
  if (line.empty()) return;

  // 校验展示序号。
  int displayIdx;
  if (!parseInt(line, displayIdx) || displayIdx < 1 || displayIdx > static_cast<int>(currentCardMap.size())) {
    cout << "序号无效。\n";
    pauseScreen();
    return;
  }

  // 转换成 cards 容器下标。
  int found = currentCardMap[displayIdx - 1];

  // 删除前展示完整详情，让用户二次确认。
  cout << "\n即将删除以下卡片：\n";
  printCardDetail(found);

  // 明确要求 y/Y 确认，降低误删概率。
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

  // 保存 active=false 的状态。
  saveCards();
  cout << "卡片已删除。\n";
  pauseScreen();
}

// ========== 查看列表详情 ==========

void queryCardById() {
  // 这个函数名保留历史叫法；实际用户输入的是展示序号，不是 cardId。
  clearScreen();
  cout << "==============================\n";
  cout << "       查看卡片详情\n";
  cout << "==============================\n";

  if (currentCardMap.empty()) {
    cout << "当前没有展示列表，正在自动加载全部卡片...\n\n";

    // 自动加载当前用户全部有效卡片，保证详情入口可独立使用。
    for (size_t i = 0; i < cards.size(); ++i) {
      if (cards[i].userId == currentUserId && cards[i].active) {
        currentCardMap.push_back(i);
      }
    }

    // 展示可查看列表。
    for (size_t i = 0; i < currentCardMap.size(); ++i) {
      printCardBrief(i + 1, currentCardMap[i]);
    }
    cout << "\n";
  } else {
    // 沿用最近一次查询/排序/查看列表。
    cout << "（当前使用的是最近一次查询/查看的列表，共 " << currentCardMap.size() << " 条记录）\n\n";
  }

  // 没有可查看卡片时结束。
  if (currentCardMap.empty()) {
    cout << "未找到任何可用卡片。\n";
    pauseScreen();
    return;
  }

  // 用户输入展示序号。
  cout << "请输入要查看的卡片序号（1~" << currentCardMap.size() << "，直接回车取消）：";
  string line;
  getline(cin, line);
  line = trim(line);

  // 直接回车表示返回。
  if (line.empty()) return;

  // 校验展示序号。
  int displayIdx;
  if (!parseInt(line, displayIdx) || displayIdx < 1 || displayIdx > static_cast<int>(currentCardMap.size())) {
    cout << "序号无效。\n";
    pauseScreen();
    return;
  }

  // 展示对应卡片详情。
  printCardDetail(currentCardMap[displayIdx - 1]);
  pauseScreen();
}

// ========== 按关键字查询 ==========

void queryCardByKeyword() {
  // 进入关键字查询页面。
  clearScreen();
  cout << "==============================\n";
  cout << "   按关键字查询知识卡片\n";
  cout << "==============================\n";

  // 读取用户输入的关键字。
  cout << "请输入关键字：";
  string keyword;
  getline(cin, keyword);
  keyword = trim(keyword);

  // 空关键字没有查询意义，直接返回。
  if (keyword.empty()) {
    cout << "关键字不能为空。\n";
    pauseScreen();
    return;
  }

  // 关键字搜索覆盖用户最可能记得的内容字段；不搜索内部 ID，避免暴露存储实现。
  // 每次查询都会重建展示映射，后续“查看详情/修改/删除”可沿用这份结果。
  currentCardMap.clear();

  // 扫描所有卡片。
  for (size_t i = 0; i < cards.size(); ++i) {
    const Card &c = cards[i];

    // 只搜索当前用户的有效卡片。
    if (c.userId != currentUserId || !c.active)
      continue;

    // 标题、正面、背面、标签任一字段命中即可进入结果。
    if (containsKeyword(c.title, keyword) ||
        containsKeyword(c.front, keyword) || containsKeyword(c.back, keyword) ||
        containsKeyword(c.tags, keyword)) {
      // 保存 cards 下标，而不是 cardId。
      currentCardMap.push_back(static_cast<int>(i));
    }
  }

  // 没有结果时提示并返回。
  if (currentCardMap.empty()) {
    cout << "未找到匹配的卡片。\n";
    pauseScreen();
    return;
  }

  // 展示查询结果列表。
  cout << "\n共找到 " << currentCardMap.size() << " 条结果：\n\n";
  for (size_t i = 0; i < currentCardMap.size(); ++i) {
    printCardBrief(i + 1, currentCardMap[i]);
  }

  // 可选：查看某条详情
  cout << "\n输入序号查看详情，或直接回车返回：";
  string line;
  getline(cin, line);
  line = trim(line);

  // 用户输入序号才查看详情；直接回车返回。
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
  // 进入多条件查询页面。
  clearScreen();
  cout << "==============================\n";
  cout << "       多条件组合查询\n";
  cout << "==============================\n";
  cout << "提示：任何条件直接回车即表示不限。\n\n";

  // 学科精确匹配条件；空字符串表示不限学科。
  cout << "请输入学科（回车跳过）：";
  string subject;
  getline(cin, subject);
  subject = trim(subject);

  // 章节精确匹配条件；空字符串表示不限章节。
  cout << "请输入章节（回车跳过）：";
  string chapter;
  getline(cin, chapter);
  chapter = trim(chapter);

  // 关键字子串匹配条件；空字符串表示不限关键字。
  cout << "请输入关键字（回车跳过）：";
  string keyword;
  getline(cin, keyword);
  keyword = trim(keyword);

  // 重建展示映射。
  currentCardMap.clear();

  // 遍历所有卡片，逐条判断是否满足全部条件。
  for (size_t i = 0; i < cards.size(); ++i) {
    // 只查询当前用户的有效卡片。
    if (cards[i].userId != currentUserId || !cards[i].active) continue;
    
    // match 初始为 true，任何条件不满足就置为 false。
    bool match = true;

    // 学科条件是精确匹配。
    if (!subject.empty() && cards[i].subject != subject) match = false;

    // 章节条件是精确匹配。
    if (!chapter.empty() && cards[i].chapter != chapter) match = false;

    // 关键字条件覆盖标题、正面、背面和标签。
    if (!keyword.empty()) {
        if (cards[i].title.find(keyword) == string::npos && 
            cards[i].front.find(keyword) == string::npos && 
            cards[i].back.find(keyword) == string::npos &&
            cards[i].tags.find(keyword) == string::npos) {
            match = false;
        }
    }
    
    // 所有条件都满足时，加入结果映射。
    if (match) {
        currentCardMap.push_back(i);
    }
  }

  // 输出查询结果。
  if (currentCardMap.empty()) {
      cout << "\n未找到符合所有条件的卡片。\n";
  } else {
      cout << "\n找到 " << currentCardMap.size() << " 张符合条件的卡片：\n\n";

      // 展示结果列表。
      for (size_t i = 0; i < currentCardMap.size(); ++i) {
          printCardBrief(i + 1, currentCardMap[i]);
      }
      
      // 允许用户顺手查看其中一条详情。
      cout << "\n输入序号查看详情，或直接回车返回：";
      string line;
      getline(cin, line);
      line = trim(line);

      // 直接回车表示不查看详情。
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
  // 进入分类查看页面。
  clearScreen();
  cout << "==============================\n";
  cout << "     按分类查看知识卡片\n";
  cout << "==============================\n";
  cout << "1. 按学科查看\n";
  cout << "2. 按章节查看\n";
  cout << "3. 按标签查看\n";
  cout << "0. 返回\n";
  cout << "请选择：";

  // 读取分类方式。
  string line;
  getline(cin, line);
  int choice;

  // 分类方式必须是整数。
  if (!parseInt(line, choice)) {
    cout << "输入无效。\n";
    pauseScreen();
    return;
  }

  // 0 表示返回上级菜单。
  if (choice == 0)
    return;

  // 只支持 1~3 三种分类方式。
  if (choice < 1 || choice > 3) {
    cout << "菜单选项不存在。\n";
    pauseScreen();
    return;
  }

  // 收集当前用户有效卡片的分类值
  set<string> categories;

  // 扫描当前用户所有有效卡片。
  for (size_t i = 0; i < cards.size(); ++i) {
    const Card &c = cards[i];

    // 跳过其他用户和逻辑删除的卡片。
    if (c.userId != currentUserId || !c.active)
      continue;

    // 根据用户选择收集不同分类字段。
    switch (choice) {
    case 1:
      // 按学科分类。
      categories.insert(c.subject);
      break;
    case 2:
      // 按章节分类。
      categories.insert(c.chapter);
      break;
    case 3:
      // 按标签分类；空标签也会作为一个分类值展示。
      categories.insert(c.tags);
      break;
    }
  }

  // 没有分类值说明当前没有可展示卡片。
  if (categories.empty()) {
    cout << "当前没有卡片数据。\n";
    pauseScreen();
    return;
  }

  // 根据 choice 生成展示标签。
  string label = (choice == 1) ? "学科" : (choice == 2) ? "章节" : "标签";

  // 输出分类列表。
  cout << "\n当前 " << label << " 列表：\n";
  int idx = 1;

  // set 转 vector，便于按用户输入序号反查分类值。
  vector<string> catList(categories.begin(), categories.end());

  // 展示分类序号。
  for (const string &cat : catList) {
    cout << "  " << idx++ << ". " << cat << "\n";
  }

  // 用户选择某个分类。
  cout << "\n输入序号查看该分类下的卡片，或 0 返回：";
  getline(cin, line);
  int catChoice;

  // 校验分类序号。
  if (!parseInt(line, catChoice) || catChoice < 0 ||
      catChoice > static_cast<int>(catList.size())) {
    cout << "输入无效。\n";
    pauseScreen();
    return;
  }

  // 0 表示返回。
  if (catChoice == 0)
    return;

  // 根据序号取出用户选中的分类值。
  string selected = catList[catChoice - 1];
  cout << "\n" << label << "：" << selected << "\n\n";

  // 重建当前分类下的展示映射。
  currentCardMap.clear();

  // 再次扫描卡片，找出属于该分类的记录。
  for (size_t i = 0; i < cards.size(); ++i) {
    const Card &c = cards[i];

    // 只处理当前用户有效卡片。
    if (c.userId != currentUserId || !c.active)
      continue;

    // 按用户选择的分类字段判断是否命中。
    bool match = false;
    switch (choice) {
    case 1: match = (c.subject == selected); break;
    case 2: match = (c.chapter == selected); break;
    case 3: match = (c.tags == selected); break;
    }

    // 命中则加入展示映射。
    if (match) {
      currentCardMap.push_back(static_cast<int>(i));
    }
  }
  
  // 输出分类下的卡片列表。
  for (size_t i = 0; i < currentCardMap.size(); ++i) {
    printCardBrief(i + 1, currentCardMap[i]);
  }
  
  // 输出总数。
  cout << "\n共 " << currentCardMap.size() << " 条卡片。\n";
  
  // 允许用户查看其中一条详情。
  cout << "\n输入序号查看详情，或直接回车返回：";
  getline(cin, line);
  line = trim(line);

  // 直接回车表示返回。
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
  // 进入全部卡片页面。
  clearScreen();
  cout << "==============================\n";
  cout << "     全部知识卡片\n";
  cout << "==============================\n";

  // 重建展示映射。
  currentCardMap.clear();

  // 收集当前用户所有有效卡片。
  for (size_t i = 0; i < cards.size(); ++i) {
    if (cards[i].userId == currentUserId && cards[i].active) {
      currentCardMap.push_back(static_cast<int>(i));
    }
  }

  // 没有卡片时只提示。
  if (currentCardMap.empty()) {
    cout << "当前没有卡片。\n";
  } else {
    // 输出全部卡片简表。
    for (size_t i = 0; i < currentCardMap.size(); ++i) {
      printCardBrief(i + 1, currentCardMap[i]);
    }

    // 输出总数。
    cout << "\n共 " << currentCardMap.size() << " 条卡片。\n";

    // 可选：查看某条详情
    cout << "\n输入序号查看详情，或直接回车返回：";
    string line;

    // 输入流关闭时直接返回，避免继续阻塞。
    if (!getline(cin, line)) return;
    line = trim(line);

    // 直接回车表示不查看详情。
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
    // 返回 cards 全局容器下标列表，不是 cardId。
    vector<int> res;

    // 扫描所有卡片。
    for (size_t i = 0; i < cards.size(); ++i) {
        // 只返回当前用户、未删除、学科精确匹配的卡片。
        if (cards[i].userId == currentUserId && cards[i].active && cards[i].subject == subject) {
            res.push_back(i);
        }
    }

    // 调用方可以继续排序或映射成展示序号。
    return res;
}

vector<int> filterCardsByChapter(const string& chapter) {
    // 返回 cards 全局容器下标列表，不是 cardId。
    vector<int> res;

    // 扫描所有卡片。
    for (size_t i = 0; i < cards.size(); ++i) {
        // 只返回当前用户、未删除、章节精确匹配的卡片。
        if (cards[i].userId == currentUserId && cards[i].active && cards[i].chapter == chapter) {
            res.push_back(i);
        }
    }

    // 返回筛选结果。
    return res;
}

vector<int> filterCardsByTag(const string& tag) {
    // 返回 cards 全局容器下标列表，不是 cardId。
    vector<int> res;

    // 扫描所有卡片。
    for (size_t i = 0; i < cards.size(); ++i) {
        // 只返回当前用户、未删除、标签精确匹配的卡片。
        if (cards[i].userId == currentUserId && cards[i].active && cards[i].tags == tag) {
            res.push_back(i);
        }
    }

    // 返回筛选结果。
    return res;
}

void sortCardsByCreateDate(vector<int>& indexes, bool ascending) {
    // indexes 中保存的是 cards 下标；排序时通过下标访问对应卡片。
    sort(indexes.begin(), indexes.end(), [ascending](int a, int b) {
        // ascending=true 表示创建日期早的排前面。
        if (ascending) return compareDate(cards[a].createDate, cards[b].createDate) < 0;

        // ascending=false 表示创建日期晚的排前面。
        return compareDate(cards[a].createDate, cards[b].createDate) > 0;
    });
}

void sortCardsByNextReviewDate(vector<int>& indexes, bool ascending) {
    // 按下一次复习日期排序，用于快速找到最早/最晚到期的卡片。
    sort(indexes.begin(), indexes.end(), [ascending](int a, int b) {
        // ascending=true 表示下次复习日期早的排前面。
        if (ascending) return compareDate(cards[a].nextReviewDate, cards[b].nextReviewDate) < 0;

        // ascending=false 表示下次复习日期晚的排前面。
        return compareDate(cards[a].nextReviewDate, cards[b].nextReviewDate) > 0;
    });
}

void sortCardsByMastery(vector<int>& indexes, bool ascending) {
    // 按掌握度排序，用于找薄弱卡片或高掌握卡片。
    sort(indexes.begin(), indexes.end(), [ascending](int a, int b) {
        // ascending=true 表示掌握度低的排前面。
        if (ascending) return cards[a].mastery < cards[b].mastery;

        // ascending=false 表示掌握度高的排前面。
        return cards[a].mastery > cards[b].mastery;
    });
}

void viewCardsBySorting() {
    // 进入排序查看页面。
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

    // 读取排序菜单选择。
    string line;
    if (!getline(cin, line)) return;
    int choice;

    // 菜单选择必须是整数。
    if (!parseInt(line, choice)) {
        cout << "输入无效。\n";
        pauseScreen();
        return;
    }

    // 0 表示返回。
    if (choice == 0) return;

    // 只支持 1~6 六种排序方式。
    if (choice < 1 || choice > 6) {
        cout << "菜单选项不存在。\n";
        pauseScreen();
        return;
    }

    // 收集当前用户所有有效卡片下标。
    vector<int> indexes;
    for (size_t i = 0; i < cards.size(); ++i) {
        if (cards[i].userId == currentUserId && cards[i].active) {
            indexes.push_back(i);
        }
    }

    // 没有卡片时结束。
    if (indexes.empty()) {
        cout << "当前没有卡片数据。\n";
        pauseScreen();
        return;
    }

    // 根据菜单选择调用对应排序函数。
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

    // 输出排序后的简表。
    for (size_t i = 0; i < currentCardMap.size(); ++i) {
        printCardBrief(i + 1, currentCardMap[i]);
    }
    
    // 可选：查看某条详情
    cout << "\n输入序号查看详情，或直接回车返回：";
    getline(cin, line);
    line = trim(line);

    // 直接回车表示返回。
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
  // 卡片管理子菜单循环，直到用户选择 0 返回主菜单。
  while (true) {
    // 每轮菜单前清屏。
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

    // 读取菜单输入。
    string line;
    getline(cin, line);
    int choice;

    // 菜单输入必须是整数。
    if (!parseInt(line, choice)) {
      cout << "输入无效，请重新输入。\n";
      pauseScreen();
      continue;
    }

    // 根据菜单项分发到具体业务函数。
    switch (choice) {
    case 1:
      // 新增知识卡片。
      addCard();
      break;
    case 2:
      // 修改最近列表或自动加载列表中的卡片。
      editCard();
      break;
    case 3:
      // 逻辑删除卡片。
      deleteCard();
      break;
    case 4:
      // 查看最近展示列表中的某条详情。
      queryCardById();
      break;
    case 5:
      // 关键字查询。
      queryCardByKeyword();
      break;
    case 6:
      // 按学科/章节/标签分类查看。
      viewCardsByCategory();
      break;
    case 7:
      // 排序查看。
      viewCardsBySorting();
      break;
    case 8:
      // 多条件组合查询。
      queryCardMultiCondition();
      break;
    case 9:
      // 查看当前用户全部有效卡片。
      viewAllCards();
      break;
    case 0:
      // 返回主菜单。
      return;
    default:
      // 其他整数不是合法菜单项。
      cout << "菜单选项不存在。\n";
      pauseScreen();
    }
  }
}
