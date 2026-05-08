// 引入错题模块头文件：公开函数声明和 WrongQuestion 相关接口都在这里。
#include "wrong.h"

// 引入卡片模块头文件：错题转卡片后需要 resetCardDisplayCache，并会生成 Card。
#include "card.h"

// 引入全局状态：wrongs/cards 全局容器、currentUserId 等登录态数据来自这里。
#include "globals.h"

// 引入存储层接口：saveWrongs/saveCards/getNextWrongId/getNextCardId 等来自这里。
#include "storage.h"

// 引入通用工具：trim、parseInt、readTextInput、日期包装、暂停/清屏等。
#include "utils.h"

// iostream 提供 cin/cout，用于控制台交互。
#include <iostream>

// vector 用于展示序号映射和筛选结果。
#include <vector>

// set 用于收集不重复的学科/章节分类。
#include <set>

// 只引入本文件实际使用的标准库名字，避免命名空间整体引入污染。
using std::cin;
using std::cout;
using std::getline;
using std::set;
using std::string;
using std::to_string;
using std::vector;

/*
[导读]
- 本文件负责错题的交互式管理，也是“错题 -> 知识卡片”跨模块联动的主要入口。

[对应流程图]
- 错题 CRUD：控制台输入 -> WrongQuestion 字段变化 -> saveWrongs()。
- 错题转卡片：WrongQuestion -> 新 Card -> linkedCardId 回写 -> saveCards()/saveWrongs()。

[输入输出]
- 输入：当前登录用户、控制台字段输入、wrongs/cards 全局容器。
- 输出：wrongs.txt、必要时 cards.txt、控制台列表。

[学习重点]
- currentWrongMap 保存的是“展示序号 -> wrongs 下标”，底层 wrongId 仍用于文件和关联。
- linkedCardId 是跨文件关联字段，维护模块会检查它是否仍指向当前用户可见卡片。
- 错题正文、答案、错因分析属于长文本字段，允许 | 和多行，存储层负责转义。

[易错点]
- 错题转卡片必须同时保存 cards.txt 和 wrongs.txt，否则 linkedCardId 会指向不存在或未落盘的卡片。
- currentWrongMap 只是最近一次列表快照；查询、分类、查看全部会重建它。
- 错因类型是固定枚举，用于统计口径稳定，不能随便改文案而不同步统计展示。

[实验]
- 转换一条错题后手动把目标卡片 active 改成 0，再运行 --check-data 观察关联修复提示。
- 新增一条错题时输入 .multi，再观察 wrongs.txt 如何由 storage.cpp 转义多行内容。
*/

// ========== 表现层映射 ==========
// 保存最近一次列表中每个展示序号对应的 wrongs 下标，避免要求用户输入全局 wrongId。
static vector<int> currentWrongMap;

static bool hasVisibleLinkedCard(int cardId) {
    // 防重复转换只认可当前用户可见卡片；指向已删除/其他用户/不存在卡片的关联应允许重新生成。
    // 遍历全局 cards 容器，寻找 linkedCardId 指向的卡片。
    for (const Card& c : cards) {
        // cardId 必须匹配，并且必须属于当前用户、仍处于 active=true。
        if (c.cardId == cardId && c.userId == currentUserId && c.active) {
            return true;
        }
    }

    // 没找到可见目标卡片，说明这个关联不可用于防重复转换。
    return false;
}

// ========== 错题打印 ==========

void printWrongBrief(int displayIdx, int realIdx) {
    // realIdx 是 wrongs 全局容器下标，不是 wrongId，也不是展示序号。
    const WrongQuestion& w = wrongs[realIdx];

    // 简表只展示题目前缀、学科、掌握度和复习次数，方便列表扫描。
    cout << "  [" << displayIdx << "] "
         << (w.question.size() > 20 ? w.question.substr(0, 17) + "..." : w.question)
         << " | " << w.subject
         << " | 掌握:" << w.mastery
         << " | 复习:" << w.reviewCount << "次"
         << "\n";
}

void printWrongDetail(int index) {
    // index 是 wrongs 全局容器下标；调用方必须确保它已经过 currentUserId/active 过滤。
    const WrongQuestion& w = wrongs[index];

    // 详情页展示错题内容、答案、错因、关联卡片和复习状态。
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
    // 输出字段提示语。
    cout << prompt;

    // 读取整行，避免 cin >> 留下换行影响后续 getline。
    string line;
    getline(cin, line);

    // 短字段统一去掉首尾空白。
    line = trim(line);

    // 必填短字段不允许为空。
    if (line.empty()) {
        cout << "输入不能为空。\n";
        return false;
    }

    // 短字段禁止 |，因为 wrongs.txt 用 | 分隔字段，短字段人工排查时也不应含分隔符。
    if (line.find('|') != string::npos) {
        cout << "输入不允许包含 | 字符。\n";
        return false;
    }

    // 校验通过后写入输出参数。
    out = line;

    // 返回 true 表示调用方可以继续流程。
    return true;
}

static string readOptionalLine(const string& prompt) {
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
    // 错题正文、答案和错因分析允许 | 与多行，存储层统一做字段转义。
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
    // 可选长文本直接复用 readTextInput；空字符串由调用方解释为“保留原值/跳过”。
    return readTextInput(prompt);
}

// ========== 错因类型辅助 ==========

static const string VALID_ERROR_TYPES[] = {
    "概念不清", "记忆错误", "粗心", "审题失误", "计算错误", "方法不会"
};

// 固定错因类型数量；新增/删除类型时要同步这里和统计展示逻辑。
static const int ERROR_TYPE_COUNT = 6;

bool isValidErrorType(const string& errorType) {
    // 固定枚举用于统计口径稳定；空字符串表示未分类，不属于有效类型。
    // 遍历所有合法错因类型。
    for (int i = 0; i < ERROR_TYPE_COUNT; ++i) {
        // 完全匹配才算合法。
        if (VALID_ERROR_TYPES[i] == errorType) return true;
    }

    // 没命中枚举表，说明不是当前支持的错因类型。
    return false;
}

string inputErrorType() {
    // 错因类型是可选项，直接回车表示未分类。
    cout << "\n错因类型（可选，直接回车跳过）：\n";

    // 输出固定枚举菜单。
    for (int i = 0; i < ERROR_TYPE_COUNT; ++i) {
        cout << "  " << (i + 1) << ". " << VALID_ERROR_TYPES[i] << "\n";
    }

    // 读取用户选择的序号。
    cout << "请输入序号：";
    string line;
    getline(cin, line);
    line = trim(line);

    // 空输入表示主动跳过分类。
    if (line.empty()) return "";

    // 解析并校验序号范围。
    int idx;
    if (!parseInt(line, idx) || idx < 1 || idx > ERROR_TYPE_COUNT) {
        cout << "输入无效，已跳过错因类型。\n";
        return "";
    }

    // 菜单序号从 1 开始，数组下标从 0 开始。
    return VALID_ERROR_TYPES[idx - 1];
}

// ========== 新增错题 ==========

void addWrong() {
    // 进入新增错题页面前清屏。
    clearScreen();
    cout << "==============================\n";
    cout << "       记录新错题\n";
    cout << "==============================\n";

    // 创建一个新的 WrongQuestion 对象，后续逐项填充字段。
    WrongQuestion w;

    // 错题归属当前登录用户；这是多用户数据隔离的关键字段。
    w.userId = currentUserId;

    // 学科是必填短字段，用于分类查看和统计。
    if (!readNonEmptyLine("学科：", w.subject)) { pauseScreen(); return; }

    // 章节是必填短字段，用于更细粒度分类和薄弱点分析。
    if (!readNonEmptyLine("章节：", w.chapter)) { pauseScreen(); return; }

    // 题目内容是必填长文本，允许 | 和多行。
    if (!readRequiredTextField("题目内容（可输入 |；多行先输入 .multi）：", w.question)) { pauseScreen(); return; }

    // 正确答案是必填长文本，用于复习和转卡片背面。
    if (!readRequiredTextField("正确答案（可输入 |；多行先输入 .multi）：", w.correctAnswer)) { pauseScreen(); return; }

    // 用户错误答案是必填长文本，用于回顾错误来源。
    if (!readRequiredTextField("用户错误答案（可输入 |；多行先输入 .multi）：", w.wrongAnswer)) { pauseScreen(); return; }
    
    // 错因分析是可选长文本，可以为空。
    w.reason = readOptionalTextField("错因分析（可选，可输入 |；多行先输入 .multi）：");

    // 错因类型是统计维度，不强制填写，避免阻塞快速记录错题。
    w.errorType = inputErrorType();

    // 错题初始掌握度低于卡片，确保刚记录的错误优先进入复习队列。
    // 分配全局唯一错题 ID；它用于存储和跨模块关联，不直接暴露给普通用户操作。
    w.wrongId       = getNextWrongId();

    // -1 表示尚未关联到知识卡片。
    w.linkedCardId  = -1;

    // 新错题默认掌握度为 30，比新卡片更低。
    w.mastery       = 30;

    // 新错题还没有复习记录。
    w.reviewCount   = 0;

    // 新错题还没有连续答对记录。
    w.correctStreak = 0;

    // 初始复习间隔为 1 天。
    w.intervalDays  = 1;

    // 创建日期使用系统本地今天。
    w.createDate    = getTodayDate();

    // 空字符串表示尚未复习。
    w.lastReviewDate = "";

    // 创建当天即可复习。
    w.nextReviewDate = w.createDate;

    // active=true 表示正常可见；删除时会改成 false。
    w.active        = true;

    // 写入全局 wrongs 容器。
    wrongs.push_back(w);

    // 立即保存到 wrongs.txt，保证新增操作落盘。
    saveWrongs();

    // 提示创建成功；这里显示内部编号主要用于确认和调试。
    cout << "\n错题记录成功！编号：" << w.wrongId << "\n";
    pauseScreen();
}

// ========== 修改错题 ==========

void editWrong() {
    // 进入修改错题页面前清屏。
    clearScreen();
    cout << "==============================\n";
    cout << "       修改错题信息\n";
    cout << "==============================\n";

    if (currentWrongMap.empty()) {
        // 没有最近列表时自动加载当前用户全部有效错题，保证编辑入口可独立使用。
        cout << "当前没有展示列表，正在自动加载全部错题...\n\n";

        // 只加载当前用户且 active=true 的错题。
        for (size_t i = 0; i < wrongs.size(); ++i) {
            if (wrongs[i].userId == currentUserId && wrongs[i].active) {
                currentWrongMap.push_back(i);
            }
        }

        // 把自动加载的列表展示给用户，序号从 1 开始。
        for (size_t i = 0; i < currentWrongMap.size(); ++i) {
            printWrongBrief(i + 1, currentWrongMap[i]);
        }
        cout << "\n";
    } else {
        // 如果已经有最近列表，沿用它，用户可以先查询/分类再修改指定结果。
        cout << "（当前使用的是最近一次查询/查看的列表，共 " << currentWrongMap.size() << " 条记录）\n\n";
    }

    // 没有任何可编辑错题时结束。
    if (currentWrongMap.empty()) {
        cout << "未找到任何可用错题。\n";
        pauseScreen();
        return;
    }

    // 用户输入的是展示序号，不是 wrongId。
    cout << "请输入要修改的错题序号（1~" << currentWrongMap.size() << "，直接回车取消）：";
    string line;
    getline(cin, line);
    line = trim(line);

    // 直接回车表示取消修改。
    if (line.empty()) return;

    // 解析并校验展示序号范围。
    int displayIdx;
    if (!parseInt(line, displayIdx) || displayIdx < 1 || displayIdx > static_cast<int>(currentWrongMap.size())) {
        cout << "序号无效。\n";
        pauseScreen();
        return;
    }

    // 把展示序号转换成 wrongs 全局容器下标。
    int found = currentWrongMap[displayIdx - 1];

    // 先展示当前详情，避免用户改错对象。
    cout << "\n当前错题信息：\n";
    printWrongDetail(found);

    // 编辑模式下，空输入代表保留原值。
    cout << "\n修改提示：直接回车保留原值。\n\n";

    // 引用要修改的错题对象，后续直接改全局容器中的数据。
    WrongQuestion& w = wrongs[found];

    // 复用 input 接收每个字段的新值。
    string input;

    // 修改学科；空输入保留原值。
    input = readOptionalLine("学科 [" + w.subject + "]：");
    if (!input.empty()) w.subject = input;

    // 修改章节；空输入保留原值。
    input = readOptionalLine("章节 [" + w.chapter + "]：");
    if (!input.empty()) w.chapter = input;

    // 修改题目长文本；空输入保留原值。
    input = readOptionalTextField("题目 [" + w.question + "]（可输入 |；多行先输入 .multi）：");
    if (!input.empty()) w.question = input;

    // 修改正确答案长文本；空输入保留原值。
    input = readOptionalTextField("正确答案 [" + w.correctAnswer + "]（可输入 |；多行先输入 .multi）：");
    if (!input.empty()) w.correctAnswer = input;

    // 修改用户错误答案长文本；空输入保留原值。
    input = readOptionalTextField("用户答案 [" + w.wrongAnswer + "]（可输入 |；多行先输入 .multi）：");
    if (!input.empty()) w.wrongAnswer = input;

    // 修改错因分析长文本；空输入保留原值。
    input = readOptionalTextField("错因分析 [" + w.reason + "]（可输入 |；多行先输入 .multi）：");
    if (!input.empty()) w.reason = input;

    // 允许清空错因类型，保证用户在无法准确分类时不会留下错误分类。
    cout << "当前错因类型：" << (w.errorType.empty() ? "未分类" : w.errorType) << "\n";
    cout << "是否修改错因类型？(y/n)：";
    getline(cin, input);
    input = trim(input);
    if (input == "y" || input == "Y") {
        // 重新走错因类型菜单；用户直接回车时 newType 会是空字符串。
        string newType = inputErrorType();
        w.errorType = newType;  // 允许清空（用户直接回车）
    }

    // 修改完成后立即保存 wrongs.txt。
    saveWrongs();
    cout << "\n错题信息修改成功！\n";
    pauseScreen();
}

// ========== 删除错题 ==========

void deleteWrong() {
    // 进入删除错题页面前清屏。
    clearScreen();
    cout << "==============================\n";
    cout << "       删除错题记录\n";
    cout << "==============================\n";

    if (currentWrongMap.empty()) {
        // 没有最近列表时自动加载当前用户全部有效错题。
        cout << "当前没有展示列表，正在自动加载全部错题...\n\n";

        // 构造可删除错题列表。
        for (size_t i = 0; i < wrongs.size(); ++i) {
            if (wrongs[i].userId == currentUserId && wrongs[i].active) {
                currentWrongMap.push_back(i);
            }
        }

        // 展示可删除对象。
        for (size_t i = 0; i < currentWrongMap.size(); ++i) {
            printWrongBrief(i + 1, currentWrongMap[i]);
        }
        cout << "\n";
    } else {
        // 沿用最近列表，方便用户先查询再删除。
        cout << "（当前使用的是最近一次查询/查看的列表，共 " << currentWrongMap.size() << " 条记录）\n\n";
    }

    // 没有可删除错题时结束。
    if (currentWrongMap.empty()) {
        cout << "未找到任何可用错题。\n";
        pauseScreen();
        return;
    }

    // 用户输入展示序号。
    cout << "请输入要删除的错题序号（1~" << currentWrongMap.size() << "，直接回车取消）：";
    string line;
    getline(cin, line);
    line = trim(line);

    // 直接回车表示取消。
    if (line.empty()) return;

    // 校验展示序号。
    int displayIdx;
    if (!parseInt(line, displayIdx) || displayIdx < 1 || displayIdx > static_cast<int>(currentWrongMap.size())) {
        cout << "序号无效。\n";
        pauseScreen();
        return;
    }

    // 转换成 wrongs 容器下标。
    int found = currentWrongMap[displayIdx - 1];

    // 删除前展示完整详情，让用户二次确认。
    cout << "\n即将删除以下错题：\n";
    printWrongDetail(found);

    // 明确要求 y/Y 确认，降低误删概率。
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

    // 保存 active=false 的状态。
    saveWrongs();
    cout << "错题已删除。\n";
    pauseScreen();
}

// ========== 查看列表详情 ==========

void queryWrongById() {
    // 这个函数名保留历史叫法；实际用户输入的是展示序号，不是 wrongId。
    clearScreen();
    cout << "==============================\n";
    cout << "       查看错题详情\n";
    cout << "==============================\n";

    if (currentWrongMap.empty()) {
        // 自动加载当前用户全部有效错题，保证详情入口可独立使用。
        cout << "当前没有展示列表，正在自动加载全部错题...\n\n";

        // 构造最近展示列表。
        for (size_t i = 0; i < wrongs.size(); ++i) {
            if (wrongs[i].userId == currentUserId && wrongs[i].active) {
                currentWrongMap.push_back(i);
            }
        }

        // 展示可查看列表。
        for (size_t i = 0; i < currentWrongMap.size(); ++i) {
            printWrongBrief(i + 1, currentWrongMap[i]);
        }
        cout << "\n";
    } else {
        // 沿用最近一次查询/分类/查看列表。
        cout << "（当前使用的是最近一次查询/查看的列表，共 " << currentWrongMap.size() << " 条记录）\n\n";
    }

    // 没有可查看错题时结束。
    if (currentWrongMap.empty()) {
        cout << "未找到任何可用错题。\n";
        pauseScreen();
        return;
    }

    // 用户输入展示序号。
    cout << "请输入要查看的错题序号（1~" << currentWrongMap.size() << "，直接回车取消）：";
    string line;
    getline(cin, line);
    line = trim(line);

    // 直接回车表示返回。
    if (line.empty()) return;

    // 校验展示序号。
    int displayIdx;
    if (!parseInt(line, displayIdx) || displayIdx < 1 || displayIdx > static_cast<int>(currentWrongMap.size())) {
        cout << "序号无效。\n";
        pauseScreen();
        return;
    }

    // 展示对应错题详情。
    printWrongDetail(currentWrongMap[displayIdx - 1]);
    pauseScreen();
}

void queryWrongByKeyword() {
    // 进入关键字查询页面。
    clearScreen();
    cout << "==============================\n";
    cout << "     按关键字查询错题\n";
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

    // 每次查询都会重建展示映射，后续“查看详情/修改/删除/转卡片”可沿用这份结果。
    currentWrongMap.clear();

    // 扫描所有错题。
    for (size_t i = 0; i < wrongs.size(); ++i) {
        const WrongQuestion& w = wrongs[i];

        // 只搜索当前用户的有效错题。
        if (w.userId != currentUserId || !w.active) continue;

        // 题目、正确答案、用户答案、错因分析任一字段命中即可进入结果。
        if (containsKeyword(w.question, keyword) ||
            containsKeyword(w.correctAnswer, keyword) ||
            containsKeyword(w.wrongAnswer, keyword) ||
            containsKeyword(w.reason, keyword)) {
            // 保存 wrongs 下标，而不是 wrongId。
            currentWrongMap.push_back(static_cast<int>(i));
        }
    }

    // 没有结果时提示并返回。
    if (currentWrongMap.empty()) {
        cout << "未找到匹配的错题。\n";
        pauseScreen();
        return;
    }

    // 展示查询结果列表。
    cout << "\n共找到 " << currentWrongMap.size() << " 条结果：\n\n";
    for (size_t i = 0; i < currentWrongMap.size(); ++i) {
        printWrongBrief(i + 1, currentWrongMap[i]);
    }

    cout << "\n输入序号查看详情，或直接回车返回：";
    string line;
    getline(cin, line);
    line = trim(line);

    // 用户输入序号才查看详情；直接回车返回。
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
  currentWrongMap.clear();

  // 遍历所有错题，逐条判断是否满足全部条件。
  for (size_t i = 0; i < wrongs.size(); ++i) {
    // 只查询当前用户的有效错题。
    if (wrongs[i].userId != currentUserId || !wrongs[i].active) continue;
    
    // match 初始为 true，任何条件不满足就置为 false。
    bool match = true;

    // 学科条件是精确匹配。
    if (!subject.empty() && wrongs[i].subject != subject) match = false;

    // 章节条件是精确匹配。
    if (!chapter.empty() && wrongs[i].chapter != chapter) match = false;

    // 关键字条件覆盖题目、正确答案和错因分析。
    if (!keyword.empty()) {
        if (wrongs[i].question.find(keyword) == string::npos && 
            wrongs[i].correctAnswer.find(keyword) == string::npos && 
            wrongs[i].reason.find(keyword) == string::npos) {
            match = false;
        }
    }
    
    // 所有条件都满足时，加入结果映射。
    if (match) {
        currentWrongMap.push_back(i);
    }
  }

  // 输出查询结果。
  if (currentWrongMap.empty()) {
      cout << "\n未找到符合所有条件的错题。\n";
  } else {
      cout << "\n找到 " << currentWrongMap.size() << " 道符合条件的错题：\n\n";

      // 展示结果列表。
      for (size_t i = 0; i < currentWrongMap.size(); ++i) {
          printWrongBrief(i + 1, currentWrongMap[i]);
      }
      
      // 允许用户顺手查看其中一条详情。
      cout << "\n输入序号查看详情，或直接回车返回：";
      string line;
      getline(cin, line);
      line = trim(line);

      // 直接回车表示不查看详情。
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
    // 返回 wrongs 全局容器下标列表，不是 wrongId。
    vector<int> res;

    // 扫描所有错题。
    for (size_t i = 0; i < wrongs.size(); ++i) {
        // 只返回当前用户、未删除、学科精确匹配的错题。
        if (wrongs[i].userId == currentUserId && wrongs[i].active && wrongs[i].subject == subject) {
            res.push_back(i);
        }
    }

    // 调用方可以继续排序或映射成展示序号。
    return res;
}

vector<int> filterWrongsByChapter(const string& chapter) {
    // 返回 wrongs 全局容器下标列表，不是 wrongId。
    vector<int> res;

    // 扫描所有错题。
    for (size_t i = 0; i < wrongs.size(); ++i) {
        // 只返回当前用户、未删除、章节精确匹配的错题。
        if (wrongs[i].userId == currentUserId && wrongs[i].active && wrongs[i].chapter == chapter) {
            res.push_back(i);
        }
    }

    // 返回筛选结果。
    return res;
}

// ========== 分类查看 ==========

void viewWrongsByCategory() {
    // 进入分类查看页面。
    clearScreen();
    cout << "==============================\n";
    cout << "     按分类查看错题\n";
    cout << "==============================\n";
    cout << "1. 按学科查看\n";
    cout << "2. 按章节查看\n";
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
    if (choice == 0) return;

    // 只支持按学科和按章节两种分类方式。
    if (choice < 1 || choice > 2) {
        cout << "菜单选项不存在。\n";
        pauseScreen();
        return;
    }

    // 收集当前用户有效错题的分类值。
    set<string> categories;

    // 扫描当前用户所有有效错题。
    for (size_t i = 0; i < wrongs.size(); ++i) {
        const WrongQuestion& w = wrongs[i];

        // 跳过其他用户和逻辑删除的错题。
        if (w.userId != currentUserId || !w.active) continue;

        // 根据用户选择收集不同分类字段。
        if (choice == 1) categories.insert(w.subject);
        else categories.insert(w.chapter);
    }

    // 没有分类值说明当前没有可展示错题。
    if (categories.empty()) {
        cout << "当前没有错题数据。\n";
        pauseScreen();
        return;
    }

    // 根据 choice 生成展示标签。
    string label = (choice == 1) ? "学科" : "章节";

    // 输出分类列表。
    cout << "\n当前 " << label << " 列表：\n";
    int idx = 1;

    // set 转 vector，便于按用户输入序号反查分类值。
    vector<string> catList(categories.begin(), categories.end());

    // 展示分类序号。
    for (const string& cat : catList) {
        cout << "  " << idx++ << ". " << cat << "\n";
    }

    // 用户选择某个分类。
    cout << "\n输入序号查看该分类下的错题，或 0 返回：";
    getline(cin, line);
    int catChoice;

    // 校验分类序号。
    if (!parseInt(line, catChoice) || catChoice < 0 || catChoice > static_cast<int>(catList.size())) {
        cout << "输入无效。\n";
        pauseScreen();
        return;
    }

    // 0 表示返回。
    if (catChoice == 0) return;

    // 根据序号取出用户选中的分类值。
    string selected = catList[catChoice - 1];
    cout << "\n" << label << "：" << selected << "\n\n";

    // 重建当前分类下的展示映射。
    currentWrongMap.clear();

    // 再次扫描错题，找出属于该分类的记录。
    for (size_t i = 0; i < wrongs.size(); ++i) {
        const WrongQuestion& w = wrongs[i];

        // 只处理当前用户有效错题。
        if (w.userId != currentUserId || !w.active) continue;

        // 按用户选择的分类字段判断是否命中。
        bool match = (choice == 1) ? (w.subject == selected) : (w.chapter == selected);

        // 命中则加入展示映射。
        if (match) {
            currentWrongMap.push_back(static_cast<int>(i));
        }
    }
    
    // 输出分类下的错题列表。
    for (size_t i = 0; i < currentWrongMap.size(); ++i) {
        printWrongBrief(i + 1, currentWrongMap[i]);
    }
    
    // 输出总数。
    cout << "\n共 " << currentWrongMap.size() << " 条错题。\n";
    
    // 允许用户查看其中一条详情。
    cout << "\n输入序号查看详情，或直接回车返回：";
    getline(cin, line);
    line = trim(line);

    // 直接回车表示返回。
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
    // 进入全部错题页面。
    clearScreen();
    cout << "==============================\n";
    cout << "     全部错题记录\n";
    cout << "==============================\n";

    // 重建展示映射。
    currentWrongMap.clear();

    // 收集当前用户所有有效错题。
    for (size_t i = 0; i < wrongs.size(); ++i) {
        if (wrongs[i].userId == currentUserId && wrongs[i].active) {
            currentWrongMap.push_back(static_cast<int>(i));
        }
    }

    // 没有错题时只提示。
    if (currentWrongMap.empty()) {
        cout << "当前没有错题。\n";
    } else {
        // 输出全部错题简表。
        for (size_t i = 0; i < currentWrongMap.size(); ++i) {
            printWrongBrief(i + 1, currentWrongMap[i]);
        }

        // 输出总数。
        cout << "\n共 " << currentWrongMap.size() << " 条错题。\n";

        // 允许用户查看其中一条详情。
        cout << "\n输入序号查看详情，或直接回车返回：";
        string line;

        // 输入流关闭时直接返回，避免继续阻塞。
        if (!getline(cin, line)) return;
        line = trim(line);

        // 直接回车表示不查看详情。
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
    // 进入错题转卡片页面。
    clearScreen();
    cout << "==============================\n";
    cout << "     错题转知识卡片\n";
    cout << "==============================\n";

    if (currentWrongMap.empty()) {
        // 没有最近列表时自动加载当前用户全部有效错题，保证转换入口可独立使用。
        cout << "当前没有展示列表，正在自动加载全部错题...\n\n";

        // 构造可转换错题列表。
        for (size_t i = 0; i < wrongs.size(); ++i) {
            if (wrongs[i].userId == currentUserId && wrongs[i].active) {
                currentWrongMap.push_back(i);
            }
        }

        // 展示可转换对象。
        for (size_t i = 0; i < currentWrongMap.size(); ++i) {
            printWrongBrief(i + 1, currentWrongMap[i]);
        }
        cout << "\n";
    } else {
        // 沿用最近列表，方便用户先查询再转换。
        cout << "（当前使用的是最近一次查询/查看的列表，共 " << currentWrongMap.size() << " 条记录）\n\n";
    }

    // 没有可转换错题时结束。
    if (currentWrongMap.empty()) {
        cout << "未找到任何可用错题。\n";
        pauseScreen();
        return;
    }

    // 用户输入展示序号。
    cout << "请输入要转换的错题序号（1~" << currentWrongMap.size() << "，直接回车取消）：";
    string line;
    getline(cin, line);
    line = trim(line);

    // 直接回车表示取消转换。
    if (line.empty()) return;

    // 校验展示序号。
    int displayIdx;
    if (!parseInt(line, displayIdx) || displayIdx < 1 || displayIdx > static_cast<int>(currentWrongMap.size())) {
        cout << "序号无效。\n";
        pauseScreen();
        return;
    }

    // 把展示序号转换成 wrongs 全局容器下标。
    int found = currentWrongMap[displayIdx - 1];

    // 引用要转换的错题，后续需要写回 linkedCardId。
    WrongQuestion& w = wrongs[found];

    // 如果已经关联到当前用户可见卡片，阻止重复转换。
    if (w.linkedCardId != -1 && hasVisibleLinkedCard(w.linkedCardId)) {
        cout << "该错题已关联卡片（卡片编号：" << w.linkedCardId << "），无法重复转换。\n";
        pauseScreen();
        return;
    }

    // linkedCardId 非 -1 但目标不可见，说明是历史坏关联或目标卡片已删除。
    if (w.linkedCardId != -1) {
        // 临时兼容原因：旧数据可能保留了失效 linkedCardId；重新生成可见卡片后覆盖关联。
        // 移除条件：所有旧数据都经过 maintenance --fix 或版本迁移后，可改为提前清理。
        cout << "检测到该错题关联的卡片不存在或不可见，将重新生成卡片并修复关联。\n";
    }

    // 创建新卡片对象。
    Card c;

    // 分配全局唯一卡片 ID。
    c.cardId = getNextCardId();

    // 新卡片归属当前用户。
    c.userId = currentUserId;

    // 沿用错题学科。
    c.subject = w.subject;

    // 沿用错题章节。
    c.chapter = w.chapter;

    // 标题用统一前缀，便于用户识别这张卡来自错题。
    c.title = "[错题转化] " + w.subject + " - " + w.chapter;

    // 卡片正面使用错题题目。
    c.front = w.question;

    // 卡片背面先放正确答案。
    c.back = w.correctAnswer;

    // 如果有错因分析，把它追加到背面，复习时能同时看到原因。
    if (!w.reason.empty()) {
        c.back += "；错因分析：" + w.reason;
    }

    // 固定标签便于后续搜索或分类。
    c.tags = "错题转化";

    // 错题转化卡片默认高难度，让它在复习和排序中保持足够显著。
    c.difficulty = 5;

    // 转化卡片默认掌握度为 50，进入普通卡片复习体系。
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
    c.nextReviewDate = c.createDate;

    // active=true 表示正常可见。
    c.active = true;

    // 先把新卡片写入全局 cards 容器。
    cards.push_back(c);

    // 先保存 cards.txt，确保 linkedCardId 即将指向的卡片已经落盘。
    saveCards();

    // 错题转卡片会改变卡片列表，清理 card.cpp 中最近展示缓存，避免旧序号错位。
    resetCardDisplayCache();

    // 先保存新卡片，再写回 linkedCardId；即使后续错题保存失败，也不会留下悬空卡片引用。
    w.linkedCardId = c.cardId;

    // 保存 wrongs.txt，把 linkedCardId 写回错题记录。
    saveWrongs();

    cout << "\n转换成功！已生成新卡片（编号：" << c.cardId << "）。\n";
    pauseScreen();
}

// ========== 错题管理子菜单 ==========

void showWrongMenu() {
    // 错题管理子菜单循环，直到用户选择 0 返回主菜单。
    while (true) {
        // 每轮菜单前清屏。
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
            // 记录新错题。
            case 1: addWrong();              break;

            // 修改最近列表或自动加载列表中的错题。
            case 2: editWrong();             break;

            // 逻辑删除错题。
            case 3: deleteWrong();           break;

            // 查看最近展示列表中的某条详情。
            case 4: queryWrongById();        break;

            // 关键字查询。
            case 5: queryWrongByKeyword();   break;

            // 按学科/章节分类查看。
            case 6: viewWrongsByCategory();  break;

            // 多条件组合查询。
            case 7: queryWrongMultiCondition(); break;

            // 错题转知识卡片。
            case 8: convertWrongToCard();    break;

            // 查看当前用户全部有效错题。
            case 9: viewAllWrongs();         break;

            // 返回主菜单。
            case 0: return;

            default:
                // 其他整数不是合法菜单项。
                cout << "菜单选项不存在。\n";
                pauseScreen();
        }
    }
}
