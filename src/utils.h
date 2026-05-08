#ifndef UTILS_H
#define UTILS_H

// string 是本工具头文件最常用的数据类型。
#include <string>

// 头文件不整体引入 std 命名空间；公开接口直接写 std::string，避免污染包含方。

/*
[导读]
- 本头文件声明跨模块小工具。
- 它不是某个业务模块，而是 card/wrong/user/review/stats/maintenance/storage 等模块共用的基础能力。

[功能范围]
1. TUI 支持：颜色宏和控制台初始化。
2. 字符串处理：去空白、转小写、关键字匹配。
3. 输入解析：安全解析整数、读取单行/多行文本。
4. 日期包装：保留旧函数名，内部委托给 date_utils。
5. 界面辅助：暂停和清屏。

[输入输出]
- 输入：控制台文本、普通字符串、日期字符串。
- 输出：解析结果、清洗结果、日期结果或控制台状态变化。

[你以后最常改的地方]
- 颜色宏：如果想调整 TUI 颜色风格，主要改 ANSI_*。
- readTextInput 的协议说明：如果改 .multi/.end，必须同步 E2E 脚本。
- 日期包装函数：如果业务层全部迁移到 date_utils，可以逐步减少这里的包装。

[不建议随便改的地方]
- parseInt/readTextInput：它们统一处理交互输入，避免直接使用 cin >> 导致输入流残留或错误状态扩散。
- isDateDue 的语义：当前是 date <= 今天 返回 true，复习队列依赖这个规则。
- std::string 接口：这是公开头文件契约，改成其他字符串类型会牵动所有业务模块。
*/

// ========== TUI 支持 ==========
// ANSI_RESET：重置控制台颜色和样式。
#define ANSI_RESET   "\033[0m"

// ANSI_BOLD：加粗显示。
#define ANSI_BOLD    "\033[1m"

// ANSI_RED：红色，常用于错误或删除提示。
#define ANSI_RED     "\033[31m"

// ANSI_GREEN：绿色，常用于成功提示。
#define ANSI_GREEN   "\033[32m"

// ANSI_YELLOW：黄色，常用于警告或强调。
#define ANSI_YELLOW  "\033[33m"

// ANSI_BLUE：蓝色，可用于普通分类标题。
#define ANSI_BLUE    "\033[34m"

// ANSI_MAGENTA：洋红色，可用于模块标题。
#define ANSI_MAGENTA "\033[35m"

// ANSI_CYAN：青色，常用于菜单标题或次级重点。
#define ANSI_CYAN    "\033[36m"

// ANSI_WHITE：白色。
#define ANSI_WHITE   "\033[37m"

// 初始化控制台能力：Windows 下启用 ANSI 转义和 UTF-8 输入输出。
void initTUI();

// ========== 字符串处理 ==========
// 返回去掉首尾空白后的字符串；不会修改原字符串。
std::string trim(const std::string& s);

// 返回 ASCII 字母转小写后的副本；中文内容保持原字节不变。
std::string toLowerCase(const std::string& s);

// 判断 text 是否包含 key；英文大小写不敏感，中文按原文匹配。
bool containsKeyword(const std::string& text, const std::string& key);

// ========== 输入解析 ==========
// 尝试把 text 完整解析成 int；成功写入 value 并返回 true，失败返回 false。
bool parseInt(const std::string& text, int& value);

// 功能：读取单行或以 .multi/.end 包裹的多行文本。
// 返回：普通单行会 trim；多行模式保留行内内容和换行，用于卡片/错题长文本。
std::string readTextInput(const std::string& prompt);

// ========== 日期处理（格式统一 YYYY-MM-DD） ==========
// 返回系统本地日期，格式为 YYYY-MM-DD。
std::string getTodayDate();

// 判断 date 是否是合法 YYYY-MM-DD；实际实现来自 date_utils.cpp。
bool isValidDate(const std::string& date);

// 比较两个零填充日期字符串；a < b 返回 -1，a > b 返回 1，相等返回 0。
int compareDate(const std::string& a, const std::string& b);

// 返回 date + days 后的日期；date 非法时沿用 date_utils 的兜底策略。
std::string addDays(const std::string& date, int days);

// 返回 to - from 的天数；任一日期非法时沿用 date_utils 的兜底策略。
int daysBetween(const std::string& from, const std::string& to);

// date <= 今天 返回 true。
bool isDateDue(const std::string& date);   // date <= 今天 返回 true

// ========== 界面辅助 ==========
// 等待用户按回车继续；输入流关闭时退出程序。
void pauseScreen();

// 清空控制台屏幕；Windows 用 cls，其他平台用 clear。
void clearScreen();

#endif
