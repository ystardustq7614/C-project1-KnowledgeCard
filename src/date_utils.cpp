// 引入日期工具头文件：Date 结构体和所有对外日期函数声明都在这里。
#include "date_utils.h"

// cctype 提供 isdigit，用于检查日期字符串中的年份/月/日是否都是数字。
#include <cctype>

// cstdio 提供 snprintf，用于把 Date 格式化成零填充的 YYYY-MM-DD。
#include <cstdio>

// ctime 提供 time_t/time/localtime_s/localtime_r，用于获取今天日期。
#include <ctime>

// 只引入本文件实际使用的标准库名字，避免命名空间整体引入污染。
using std::isdigit;
using std::size_t;
using std::snprintf;
using std::stoi;
using std::string;
using std::time;
using std::time_t;
using std::tm;

/*
[导读]
- 本文件是项目统一日期工具。
- 复习、衰减、统计和维护模块都不应该各自手写日期差或日期加减。
- 统一放在这里的好处是：跨月、跨年、闰年和非法日期策略只需要维护一份。

[输入输出]
- parseDate：输入 YYYY-MM-DD 字符串，输出 Date 结构。
- isValidDate：输入字符串，返回它是否是合法日期。
- daysBetweenDates：输入两个日期字符串，返回 to - from 的天数。
- addDaysToDate：输入日期和天数，返回加减后的 YYYY-MM-DD。
- todayDate：读取系统本地时间，返回今天的 YYYY-MM-DD。

[核心规则]
1. 日期字符串必须是固定 10 位：YYYY-MM-DD。
2. 年份范围限制为 1900~2100，避免教学项目里出现无意义极端日期。
3. 日期差通过“公历日期 -> 自 1970-01-01 起的天数”计算。
4. 日期加减先转成天数序号，加减后再转回年月日。
5. 非法日期不会抛异常：日期差返回 0，加减日期返回原字符串。

[你以后最常改的地方]
- 年份范围：parseDate 里的 1900 和 2100。
- 非法日期策略：daysBetweenDates 返回 0、addDaysToDate 返回原字符串。
- todayDate 的时区语义：当前使用系统本地时间，不使用 UTC。

[不建议随便改的地方]
- daysFromCivil/civilFromDays：它们是一组互逆算法，随意改一边会破坏日期加减。
- YYYY-MM-DD 的零填充格式：业务层可能依赖字典序比较日期字符串。
- 闰年判断规则：这是标准公历规则，不是项目自定义规则。

[学习重点]
- 这里没有用 mktime 来算日期差，是为了避开本地时区、夏令时和平台差异。
- 日期差和日期加减都基于整数天数序号，所以跨月、跨年、闰年会自然成立。
*/

// 判断某一年是否是标准公历闰年。
static bool isLeapYear(int year) {
    // 闰年规则：
    // 1. 能被 4 整除通常是闰年；
    // 2. 但能被 100 整除通常不是闰年；
    // 3. 但能被 400 整除又是闰年。
    return (year % 4 == 0 && year % 100 != 0) || (year % 400 == 0);
}

// 返回某年某月有多少天。
static int daysInMonth(int year, int month) {
    // 下标 0 占位不用，这样 table[1] 正好表示 1 月天数，阅读更直观。
    static const int table[] = {0, 31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};

    // 月份必须在 1~12；非法月份直接返回 0，让调用方统一判 invalid。
    if (month < 1 || month > 12) {
        return 0;
    }

    // 闰年 2 月有 29 天。
    if (month == 2 && isLeapYear(year)) {
        return 29;
    }

    // 非闰年 2 月以及其他月份，直接查表。
    return table[month];
}

// 检查 text 从 start 开始的 length 个字符是否全是数字。
static bool allDigits(const string& text, size_t start, size_t length) {
    // 如果检查范围已经超过字符串长度，肯定不合法。
    if (start + length > text.size()) {
        return false;
    }

    // 逐个字符检查是否是数字。
    for (size_t i = start; i < start + length; ++i) {
        // static_cast<unsigned char> 是为了避免 char 为负值时传给 isdigit 产生未定义行为。
        if (!isdigit(static_cast<unsigned char>(text[i]))) {
            return false;
        }
    }

    // 所有字符都通过 isdigit 检查。
    return true;
}

// 把公历年月日转换成“自 1970-01-01 起的天数序号”。
static int daysFromCivil(int year, unsigned month, unsigned day) {
    // 公历日期序号算法不经过 mktime，避免本地时区、夏令时和运行平台差异影响日期差。

    // 把 1 月和 2 月视作上一年的第 13、14 月，简化闰年累计计算。
    year -= month <= 2;

    // 每 400 年是一个完整公历周期，共 146097 天。
    const int era = (year >= 0 ? year : year - 399) / 400;

    // yoe = year of era，表示当前 400 年周期内的第几年。
    const unsigned yoe = static_cast<unsigned>(year - era * 400);

    // doy = day of year，表示当前日期在“调整后年份”中的第几天。
    const unsigned doy = (153 * (month + (month > 2 ? -3 : 9)) + 2) / 5 + day - 1;

    // doe = day of era，表示当前日期在 400 年周期内的第几天。
    const unsigned doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;

    // 719468 是算法中的偏移量，用来让 1970-01-01 对应序号 0。
    return era * 146097 + static_cast<int>(doe) - 719468;
}

// 把“自 1970-01-01 起的天数序号”转换回公历年月日。
static Date civilFromDays(int serialDay) {
    // 与 daysFromCivil 保持互逆关系；addDaysToDate 依赖它把序号转换回零填充日期。

    // 先把以 1970-01-01 为 0 的序号转回算法内部使用的偏移序号。
    int z = serialDay + 719468;

    // 计算当前日期落在哪个 400 年公历周期内。
    const int era = (z >= 0 ? z : z - 146096) / 146097;

    // doe = day of era，表示当前日期在这个 400 年周期内的第几天。
    const unsigned doe = static_cast<unsigned>(z - era * 146097);

    // yoe = year of era，从 day-of-era 反推周期内年份。
    const unsigned yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;

    // 得到初步年份。
    int year = static_cast<int>(yoe) + era * 400;

    // doy = day of year，表示在当前“调整后年份”中的第几天。
    const unsigned doy = doe - (365 * yoe + yoe / 4 - yoe / 100);

    // mp 是调整后月份的中间编号。
    const unsigned mp = (5 * doy + 2) / 153;

    // 从中间编号反推日期。
    const unsigned day = doy - (153 * mp + 2) / 5 + 1;

    // 从中间编号反推月份，把 3 月作为起点的内部表示转回 1~12 月。
    const unsigned month = mp + (mp < 10 ? 3 : -9);

    // 如果月份是 1 月或 2 月，说明真实年份要加回来。
    year += month <= 2;

    // 返回 Date 结构体，month/day 转为 int 便于项目其他代码使用。
    return {year, static_cast<int>(month), static_cast<int>(day)};
}

// 把 Date 格式化成 YYYY-MM-DD 字符串。
static string formatDate(const Date& date) {
    // 16 字节足够容纳 "YYYY-MM-DD"、结尾 '\0'，以及当前年份范围内的安全余量。
    char buffer[16];

    // %04d/%02d 保证年份、月份、日期都有固定宽度和零填充。
    snprintf(buffer, sizeof(buffer), "%04d-%02d-%02d", date.year, date.month, date.day);

    // 转成 std::string 返回给调用方。
    return string(buffer);
}

// 解析 YYYY-MM-DD 字符串。
bool parseDate(const string& text, Date& out) {
    // 先检查固定位置和数字，再调用 stoi，避免非法输入触发异常作为常规控制流。

    // 格式必须刚好 10 位，并且第 5/8 个字符是 '-'。
    if (text.size() != 10 || text[4] != '-' || text[7] != '-') {
        return false;
    }

    // 年份 4 位、月份 2 位、日期 2 位都必须是数字。
    if (!allDigits(text, 0, 4) || !allDigits(text, 5, 2) || !allDigits(text, 8, 2)) {
        return false;
    }

    // 先解析到临时变量，只有完全合法后才写入 out。
    Date parsed;

    // 解析年份。
    parsed.year = stoi(text.substr(0, 4));

    // 解析月份。
    parsed.month = stoi(text.substr(5, 2));

    // 解析日期。
    parsed.day = stoi(text.substr(8, 2));

    // 限制年份范围，避免教学项目中无意义的极端日期进入复习调度。
    // [可改] 如果将来要支持更早或更晚的历史数据，先改这里，再补测试。
    if (parsed.year < 1900 || parsed.year > 2100) {
        // 项目数据只服务学习复习计划，限制范围可及时暴露误录入年份。
        return false;
    }

    // 查出该年该月最多有多少天；非法月份会得到 0。
    const int maxDay = daysInMonth(parsed.year, parsed.month);

    // 月份非法、日期小于 1、日期超过当月最大天数，都判为非法日期。
    if (maxDay == 0 || parsed.day < 1 || parsed.day > maxDay) {
        return false;
    }

    // 到这里才把结果写入 out，保证 false 返回时调用方不会拿到半成品日期。
    out = parsed;

    // 解析成功。
    return true;
}

// 判断字符串是否是合法日期。
bool isValidDate(const string& text) {
    // 这里只关心是否合法，不关心解析出的 Date 具体值。
    Date ignored;

    // 复用 parseDate，避免合法性规则出现两份实现。
    return parseDate(text, ignored);
}

// 计算两个日期之间的天数差：to - from。
int daysBetweenDates(const string& from, const string& to) {
    // 起始日期。
    Date start;

    // 结束日期。
    Date end;

    // 任意一个日期非法，都返回 0。
    if (!parseDate(from, start) || !parseDate(to, end)) {
        // 非法日期返回 0，避免记忆衰减把坏数据误判为严重逾期；维护模块会负责修复。
        return 0;
    }

    // 日期差 = 结束日期序号 - 起始日期序号。
    // 结果可以为负数，表示 to 早于 from；复习到期判断会用到这个语义。
    return daysFromCivil(end.year, end.month, end.day) -
           daysFromCivil(start.year, start.month, start.day);
}

// 给日期加上 days 天；days 可以为负数。
string addDaysToDate(const string& date, int days) {
    // 先解析原始日期。
    Date parsed;

    // 非法日期不做修正，原样返回。
    if (!parseDate(date, parsed)) {
        // 保留原值比静默替换为今天更安全，调用方可在一致性检查中看到原始坏数据。
        return date;
    }

    // 先把日期转成天数序号，再加上 days。
    const int serialDay = daysFromCivil(parsed.year, parsed.month, parsed.day) + days;

    // 再把新的天数序号转回 Date，最后格式化为 YYYY-MM-DD。
    return formatDate(civilFromDays(serialDay));
}

// 返回系统本地日期对应的 YYYY-MM-DD。
string todayDate() {
    // 读取当前系统时间戳。
    time_t now = time(nullptr);

    // 存放本地时间分解结果。
    tm localTime = {};

#ifdef _WIN32
    // Windows 使用线程安全版本 localtime_s。
    localtime_s(&localTime, &now);
#else
    // Linux/macOS 使用线程安全版本 localtime_r。
    localtime_r(&now, &localTime);
#endif

    // 把 tm 结构转换成项目自己的 Date 结构。
    Date today;

    // tm_year 表示从 1900 年开始的偏移。
    today.year = localTime.tm_year + 1900;

    // tm_mon 是 0~11，所以要加 1 转成 1~12。
    today.month = localTime.tm_mon + 1;

    // tm_mday 已经是 1~31。
    today.day = localTime.tm_mday;

    // 统一格式化成 YYYY-MM-DD。
    return formatDate(today);
}
