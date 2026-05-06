#ifndef UTILS_H
#define UTILS_H

#include <string>
using namespace std;

/*
模块职责：
- 提供控制台显示、字符串处理、输入解析和日期函数的兼容包装。

关键约束：
- 日期核心实现在 date_utils.cpp；本文件保留 getTodayDate/addDays 等历史函数名，减少业务层改动。
- parseInt/readTextInput 统一处理交互输入，避免直接使用 cin >> 导致输入流残留或错误状态扩散。
*/

// ========== TUI 支持 ==========
#define ANSI_RESET   "\033[0m"
#define ANSI_BOLD    "\033[1m"
#define ANSI_RED     "\033[31m"
#define ANSI_GREEN   "\033[32m"
#define ANSI_YELLOW  "\033[33m"
#define ANSI_BLUE    "\033[34m"
#define ANSI_MAGENTA "\033[35m"
#define ANSI_CYAN    "\033[36m"
#define ANSI_WHITE   "\033[37m"

void initTUI();

// ========== 字符串处理 ==========
string trim(const string& s);
string toLowerCase(const string& s);
bool containsKeyword(const string& text, const string& key);

// ========== 输入解析 ==========
bool parseInt(const string& text, int& value);

// 功能：读取单行或以 .multi/.end 包裹的多行文本。
// 返回：普通单行会 trim；多行模式保留行内内容和换行，用于卡片/错题长文本。
string readTextInput(const string& prompt);

// ========== 日期处理（格式统一 YYYY-MM-DD） ==========
string getTodayDate();
bool isValidDate(const string& date);
int compareDate(const string& a, const string& b);
string addDays(const string& date, int days);
int daysBetween(const string& from, const string& to);
bool isDateDue(const string& date);   // date <= 今天 返回 true

// ========== 界面辅助 ==========
void pauseScreen();
void clearScreen();

#endif
