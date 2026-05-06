#include "utils.h"
#include "date_utils.h"
#include <cctype>
#include <cstdlib>
#include <iostream>
#include <vector>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

using namespace std;

/*
实现说明：
- utils 保留历史工具函数名，内部把日期能力委托给 date_utils，降低 v1.3 日期收敛时的改动面。
- 输入相关函数统一使用 getline，避免 cin >> 与后续 getline 混用造成残留换行问题。
*/

// ========== TUI 支持 ==========

void initTUI() {
#ifdef _WIN32
  HANDLE hOut = GetStdHandle(STD_OUTPUT_HANDLE);
  if (hOut == INVALID_HANDLE_VALUE)
    return;
  DWORD dwMode = 0;
  if (!GetConsoleMode(hOut, &dwMode))
    return;
  dwMode |= ENABLE_VIRTUAL_TERMINAL_PROCESSING;
  SetConsoleMode(hOut, dwMode);

  // Windows 控制台默认编码不稳定；统一到 UTF-8 以支撑中文自动化输入和输出断言。
  SetConsoleCP(65001);
  SetConsoleOutputCP(65001);
#endif
}

// ========== 字符串处理 ==========

string trim(const string &s) {
  size_t start = s.find_first_not_of(" \t\r\n");
  if (start == string::npos)
    return "";
  size_t end = s.find_last_not_of(" \t\r\n");
  return s.substr(start, end - start + 1);
}

string toLowerCase(const string &s) {
  string result = s;
  for (size_t i = 0; i < result.size(); ++i) {
    result[i] = tolower(static_cast<unsigned char>(result[i]));
  }
  return result;
}

bool containsKeyword(const string &text, const string &key) {
  // 当前只对 ASCII 大小写做折叠；中文关键字按原字节序列匹配，不做拼音或分词。
  string lowerText = toLowerCase(text);
  string lowerKey = toLowerCase(key);
  return lowerText.find(lowerKey) != string::npos;
}

// ========== 输入解析 ==========

bool parseInt(const string &text, int &value) {
  string trimmed = trim(text);
  if (trimmed.empty())
    return false;

  // 先做整串数字校验，再调用 stoi，保证 "12abc" 这类输入不会被部分接受。
  size_t start = 0;
  if (trimmed[0] == '-' || trimmed[0] == '+') {
    start = 1;
  }
  if (start >= trimmed.size())
    return false;

  for (size_t i = start; i < trimmed.size(); ++i) {
    if (!isdigit(static_cast<unsigned char>(trimmed[i]))) {
      return false;
    }
  }

  try {
    value = stoi(trimmed);
    return true;
  } catch (...) {
    return false;
  }
}

string readTextInput(const string& prompt) {
  cout << prompt;
  string firstLine;
  if (!getline(cin, firstLine)) {
    return "";
  }

  if (trim(firstLine) != ".multi") {
    return trim(firstLine);
  }

  // 多行协议只在明确输入 .multi 后启用，避免普通答案里偶然出现换行影响菜单流程。
  cout << "请输入多行内容，单独输入 .end 结束。\n";
  vector<string> lines;
  string line;
  while (getline(cin, line)) {
    if (trim(line) == ".end") {
      break;
    }
    lines.push_back(line);
  }

  string result;
  for (size_t i = 0; i < lines.size(); ++i) {
    if (i > 0) {
      result += '\n';
    }
    result += lines[i];
  }
  return result;
}

string getTodayDate() {
  return todayDate();
}

int compareDate(const string &a, const string &b) {
  // 依赖 date_utils 的零填充格式契约：YYYY-MM-DD 的字典序等同于时间先后。
  if (a < b)
    return -1;
  if (a > b)
    return 1;
  return 0;
}

string addDays(const string &date, int days) {
  return addDaysToDate(date, days);
}

int daysBetween(const string &from, const string &to) {
  return daysBetweenDates(from, to);
}

bool isDateDue(const string &date) {
  // 非法日期会按字符串参与比较；正常数据路径由输入校验和维护检查保证日期合法。
  string today = getTodayDate();
  return compareDate(date, today) <= 0;
}

// ========== 界面辅助 ==========

void pauseScreen() {
  cout << "\n按回车键继续...";
  string dummy;
  if (!getline(cin, dummy)) {
    exit(0);
  }
}

void clearScreen() {
#ifdef _WIN32
  system("cls");
#else
  system("clear");
#endif
}
