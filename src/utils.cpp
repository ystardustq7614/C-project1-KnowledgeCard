// 引入工具函数头文件：本文件实现 utils.h 中声明的跨模块工具函数。
#include "utils.h"

// 引入日期工具：utils 中的 getTodayDate/addDays/daysBetween 会委托给这里。
#include "date_utils.h"

// cctype 提供 isdigit/tolower，用于整数解析和大小写折叠。
#include <cctype>

// cstdlib 提供 exit/system，用于退出程序和清屏命令。
#include <cstdlib>

// iostream 提供 cin/cout，用于控制台输入输出。
#include <iostream>

// vector 用于 readTextInput 收集多行文本。
#include <vector>

// 只有 Windows 平台才需要包含 windows.h 来设置控制台 UTF-8 和 ANSI 支持。
#ifdef _WIN32
// 减少 windows.h 引入的内容，降低宏污染和编译负担。
#define WIN32_LEAN_AND_MEAN

// 避免 Windows 头文件定义 min/max 宏，干扰 std::min/std::max。
#ifndef NOMINMAX
#define NOMINMAX
#endif

// Windows 控制台 API：GetStdHandle/GetConsoleMode/SetConsoleMode/SetConsoleCP 等。
#include <windows.h>
#endif

// 只引入本文件实际使用的标准库名字，避免命名空间整体引入污染。
using std::cin;
using std::cout;
using std::exit;
using std::getline;
using std::isdigit;
using std::size_t;
using std::stoi;
using std::string;
using std::tolower;
using std::vector;

/*
[导读]
- 本文件放置跨业务模块复用的小工具。
- 它连接了控制台显示、字符串处理、输入解析、长文本输入、日期包装和界面辅助。
- card.cpp、wrong.cpp、user.cpp、review.cpp、stats.cpp、maintenance.cpp 等模块都会调用这里。

[输入输出]
- 输入：控制台文本、普通字符串、日期字符串。
- 输出：清洗后的字符串、解析结果、日期包装结果或控制台状态变化。

[核心分区]
1. TUI 支持：初始化控制台编码和 ANSI 转义支持。
2. 字符串处理：trim、toLowerCase、containsKeyword。
3. 输入解析：parseInt、readTextInput。
4. 日期包装：getTodayDate、compareDate、addDays、daysBetween、isDateDue。
5. 界面辅助：pauseScreen、clearScreen。

[你以后最常改的地方]
- readTextInput 的多行协议提示文案：例如 .multi/.end 的说明。
- containsKeyword 的匹配策略：当前只做简单子串匹配，以后可扩展为多关键字或分词。
- clearScreen/pauseScreen 的交互表现：例如测试模式下是否跳过清屏或暂停。

[不建议随便改的地方]
- initTUI 里的 UTF-8 设置：E2E 中文输入和输出断言依赖它。
- readTextInput 的 .multi/.end 协议：E2E 脚本依赖这个菜单节奏。
- 日期包装函数的返回语义：业务层依赖 date_utils 的统一日期策略。

[学习重点]
- 输入相关函数统一使用 getline，避免 cin >> 与后续 getline 混用造成残留换行。
- 日期能力实际委托给 date_utils，utils 保留旧函数名是为了降低业务层改动面。
*/

// ========== TUI 支持 ==========

// 初始化控制台显示能力。
void initTUI() {
#ifdef _WIN32
  // 获取标准输出句柄，也就是当前控制台输出目标。
  HANDLE hOut = GetStdHandle(STD_OUTPUT_HANDLE);

  // 如果当前进程没有有效控制台，直接返回，避免后续 API 调用失败。
  if (hOut == INVALID_HANDLE_VALUE)
    return;

  // 读取当前控制台模式。
  DWORD dwMode = 0;

  // 如果读取失败，也直接返回；此时程序仍能运行，只是颜色/UTF-8 支持可能不完整。
  if (!GetConsoleMode(hOut, &dwMode))
    return;

  // 开启 ANSI/VT 转义序列支持，让 ANSI_RED、ANSI_GREEN 等颜色宏能在 Windows 控制台生效。
  dwMode |= ENABLE_VIRTUAL_TERMINAL_PROCESSING;

  // 写回新的控制台模式。
  SetConsoleMode(hOut, dwMode);

  // Windows 控制台默认编码不稳定；统一到 UTF-8 以支撑中文自动化输入和输出断言。
  SetConsoleCP(65001);

  // 同时设置输出代码页为 UTF-8，避免中文菜单显示乱码。
  SetConsoleOutputCP(65001);
#endif
}

// ========== 字符串处理 ==========

// 去掉字符串首尾的空格、制表符、回车和换行。
string trim(const string &s) {
  // 找到第一个非空白字符。
  size_t start = s.find_first_not_of(" \t\r\n");

  // 如果完全没有非空白字符，说明结果是空字符串。
  if (start == string::npos)
    return "";

  // 找到最后一个非空白字符。
  size_t end = s.find_last_not_of(" \t\r\n");

  // 截取中间的有效内容。
  return s.substr(start, end - start + 1);
}

// 把字符串中的 ASCII 字母转成小写。
string toLowerCase(const string &s) {
  // 先复制一份，避免修改调用方传入的原字符串。
  string result = s;

  // 逐字节处理；中文 UTF-8 字节不会被当成 ASCII 字母转换。
  for (size_t i = 0; i < result.size(); ++i) {
    // static_cast<unsigned char> 是为了避免 char 为负值时传给 tolower 产生未定义行为。
    result[i] = tolower(static_cast<unsigned char>(result[i]));
  }

  // 返回转换后的副本。
  return result;
}

// 判断 text 是否包含 key，英文大小写不敏感。
bool containsKeyword(const string &text, const string &key) {
  // 当前只对 ASCII 大小写做折叠；中文关键字按原字节序列匹配，不做拼音或分词。
  string lowerText = toLowerCase(text);

  // 关键字也做同样的小写转换，保证英文搜索不区分大小写。
  string lowerKey = toLowerCase(key);

  // string::find 找不到时返回 npos。
  return lowerText.find(lowerKey) != string::npos;
}

// ========== 输入解析 ==========

// 把一段文本完整解析成 int。
bool parseInt(const string &text, int &value) {
  // 先去掉首尾空白，允许用户输入 "  12  "。
  string trimmed = trim(text);

  // 空字符串不是合法整数。
  if (trimmed.empty())
    return false;

  // 先做整串数字校验，再调用 stoi，保证 "12abc" 这类输入不会被部分接受。
  size_t start = 0;

  // 支持可选的正负号。
  if (trimmed[0] == '-' || trimmed[0] == '+') {
    start = 1;
  }

  // 只有一个 "+" 或 "-"，没有数字，不是合法整数。
  if (start >= trimmed.size())
    return false;

  // 从符号之后开始检查每个字符都必须是数字。
  for (size_t i = start; i < trimmed.size(); ++i) {
    if (!isdigit(static_cast<unsigned char>(trimmed[i]))) {
      return false;
    }
  }

  // stoi 仍可能因为超出 int 范围而抛异常，所以保留 try/catch。
  try {
    // 解析成功时写入输出参数。
    value = stoi(trimmed);

    // 返回 true 表示 value 可用。
    return true;
  } catch (...) {
    // 溢出或其他解析异常都统一视为失败。
    return false;
  }
}

// 读取单行文本，或读取以 .multi 开始、.end 结束的多行文本。
string readTextInput(const string& prompt) {
  // 先输出提示语，例如“题目内容：”。
  cout << prompt;

  // 读取第一行输入。
  string firstLine;

  // 如果输入流关闭或读取失败，返回空字符串，让调用方按“未输入”处理。
  if (!getline(cin, firstLine)) {
    return "";
  }

  // 普通单行模式：不是 .multi，就直接返回 trim 后的第一行。
  if (trim(firstLine) != ".multi") {
    return trim(firstLine);
  }

  // 多行协议只在明确输入 .multi 后启用，避免普通答案里偶然出现换行影响菜单流程。
  cout << "请输入多行内容，单独输入 .end 结束。\n";

  // 收集多行内容；这里用 vector 便于最后重新拼接换行。
  vector<string> lines;

  // 循环读取每一行。
  string line;

  // 直到输入流结束，或读到单独一行 .end。
  while (getline(cin, line)) {
    // .end 前后允许有空白；trim 后等于 .end 就结束多行输入。
    if (trim(line) == ".end") {
      break;
    }

    // 多行正文不 trim，保留用户输入的行内空格。
    lines.push_back(line);
  }

  // 把多行重新拼成一个字符串。
  string result;

  // 用 '\n' 作为内部统一换行符。
  for (size_t i = 0; i < lines.size(); ++i) {
    if (i > 0) {
      result += '\n';
    }
    result += lines[i];
  }

  // 返回完整长文本。
  return result;
}

// 返回今天日期；保留旧函数名，实际实现委托给 date_utils。
string getTodayDate() {
  return todayDate();
}

// 比较两个 YYYY-MM-DD 日期字符串。
int compareDate(const string &a, const string &b) {
  // 依赖 date_utils 的零填充格式契约：YYYY-MM-DD 的字典序等同于时间先后。

  // a 更早时返回 -1。
  if (a < b)
    return -1;

  // a 更晚时返回 1。
  if (a > b)
    return 1;

  // 完全相等时返回 0。
  return 0;
}

// 给日期加 days 天；保留旧函数名，实际实现委托给 date_utils。
string addDays(const string &date, int days) {
  return addDaysToDate(date, days);
}

// 计算两个日期之间的天数差；保留旧函数名，实际实现委托给 date_utils。
int daysBetween(const string &from, const string &to) {
  return daysBetweenDates(from, to);
}

// 判断某个日期是否已经到期。
bool isDateDue(const string &date) {
  // 非法日期会按字符串参与比较；正常数据路径由输入校验和维护检查保证日期合法。
  string today = getTodayDate();

  // date <= today 表示已经到期或正好今天到期。
  return compareDate(date, today) <= 0;
}

// ========== 界面辅助 ==========

// 暂停屏幕，等待用户按回车继续。
void pauseScreen() {
  // 前面加空行，让提示不贴在上一段输出后面。
  cout << "\n按回车键继续...";

  // dummy 只是用来消耗这一行输入内容。
  string dummy;

  // 如果输入流已经关闭，直接退出程序，避免菜单循环继续空转。
  if (!getline(cin, dummy)) {
    exit(0);
  }
}

// 清屏。
void clearScreen() {
#ifdef _WIN32
  // Windows 使用 cls。
  system("cls");
#else
  // Linux/macOS 使用 clear。
  system("clear");
#endif
}
