#ifndef DATE_UTILS_H
#define DATE_UTILS_H

#include <string>

/*
[导读]
- 本头文件声明统一日期工具，复习、衰减、统计和维护模块都应通过这里处理日期。

[输入输出]
- 输入：YYYY-MM-DD 字符串。
- 输出：解析结果、合法性、日期差、日期加减后的字符串和今天日期。

[易错点]
- 业务层依赖日期字符串的字典序比较，因此必须保持零填充格式：YYYY-MM-DD。
- 解析范围限制在 1900~2100，避免教学项目中无意义的极端日期进入复习调度。
- 非法日期不会抛异常；依调用函数语义返回 false、0 或原始字符串。
*/

// 表示：已通过 parseDate 解析出的公历日期。
struct Date {
    int year = 0;
    int month = 0;
    int day = 0;
};

// 返回：true 表示 text 是合法日期并写入 out；false 时 out 不作为有效结果使用。
bool parseDate(const std::string& text, Date& out);
bool isValidDate(const std::string& text);

// 返回：to - from 的天数；任一日期非法时返回 0，避免调用方在异常路径上误触发逾期衰减。
int daysBetweenDates(const std::string& from, const std::string& to);

// 返回：date + days 后的日期；date 非法时返回原字符串，便于上层保留原始数据等待维护模块修复。
std::string addDaysToDate(const std::string& date, int days);
std::string todayDate();

#endif
