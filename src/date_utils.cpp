#include "date_utils.h"

#include <cctype>
#include <cstdio>
#include <ctime>

using std::string;
using std::stoi;

/*
实现说明：
- 业务层要求日期可比较、可加减，且不依赖平台本地时区以外的复杂时间库。
- 这里把合法公历日期转换为“自 1970-01-01 起的天数”进行计算，覆盖跨月、跨年和闰年。
*/

static bool isLeapYear(int year) {
    return (year % 4 == 0 && year % 100 != 0) || (year % 400 == 0);
}

static int daysInMonth(int year, int month) {
    static const int table[] = {0, 31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
    if (month < 1 || month > 12) {
        return 0;
    }
    if (month == 2 && isLeapYear(year)) {
        return 29;
    }
    return table[month];
}

static bool allDigits(const string& text, size_t start, size_t length) {
    if (start + length > text.size()) {
        return false;
    }
    for (size_t i = start; i < start + length; ++i) {
        if (!isdigit(static_cast<unsigned char>(text[i]))) {
            return false;
        }
    }
    return true;
}

static int daysFromCivil(int year, unsigned month, unsigned day) {
    // 公历日期序号算法不经过 mktime，避免本地时区、夏令时和运行平台差异影响日期差。
    year -= month <= 2;
    const int era = (year >= 0 ? year : year - 399) / 400;
    const unsigned yoe = static_cast<unsigned>(year - era * 400);
    const unsigned doy = (153 * (month + (month > 2 ? -3 : 9)) + 2) / 5 + day - 1;
    const unsigned doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    return era * 146097 + static_cast<int>(doe) - 719468;
}

static Date civilFromDays(int serialDay) {
    // 与 daysFromCivil 保持互逆关系；addDaysToDate 依赖它把序号转换回零填充日期。
    int z = serialDay + 719468;
    const int era = (z >= 0 ? z : z - 146096) / 146097;
    const unsigned doe = static_cast<unsigned>(z - era * 146097);
    const unsigned yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
    int year = static_cast<int>(yoe) + era * 400;
    const unsigned doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
    const unsigned mp = (5 * doy + 2) / 153;
    const unsigned day = doy - (153 * mp + 2) / 5 + 1;
    const unsigned month = mp + (mp < 10 ? 3 : -9);
    year += month <= 2;
    return {year, static_cast<int>(month), static_cast<int>(day)};
}

static string formatDate(const Date& date) {
    char buffer[16];
    snprintf(buffer, sizeof(buffer), "%04d-%02d-%02d", date.year, date.month, date.day);
    return string(buffer);
}

bool parseDate(const string& text, Date& out) {
    // 先检查固定位置和数字，再调用 stoi，避免非法输入触发异常作为常规控制流。
    if (text.size() != 10 || text[4] != '-' || text[7] != '-') {
        return false;
    }
    if (!allDigits(text, 0, 4) || !allDigits(text, 5, 2) || !allDigits(text, 8, 2)) {
        return false;
    }

    Date parsed;
    parsed.year = stoi(text.substr(0, 4));
    parsed.month = stoi(text.substr(5, 2));
    parsed.day = stoi(text.substr(8, 2));

    if (parsed.year < 1900 || parsed.year > 2100) {
        // 项目数据只服务学习复习计划，限制范围可及时暴露误录入年份。
        return false;
    }
    const int maxDay = daysInMonth(parsed.year, parsed.month);
    if (maxDay == 0 || parsed.day < 1 || parsed.day > maxDay) {
        return false;
    }

    out = parsed;
    return true;
}

bool isValidDate(const string& text) {
    Date ignored;
    return parseDate(text, ignored);
}

int daysBetweenDates(const string& from, const string& to) {
    Date start;
    Date end;
    if (!parseDate(from, start) || !parseDate(to, end)) {
        // 非法日期返回 0，避免记忆衰减把坏数据误判为严重逾期；维护模块会负责修复。
        return 0;
    }

    return daysFromCivil(end.year, end.month, end.day) -
           daysFromCivil(start.year, start.month, start.day);
}

string addDaysToDate(const string& date, int days) {
    Date parsed;
    if (!parseDate(date, parsed)) {
        // 保留原值比静默替换为今天更安全，调用方可在一致性检查中看到原始坏数据。
        return date;
    }

    const int serialDay = daysFromCivil(parsed.year, parsed.month, parsed.day) + days;
    return formatDate(civilFromDays(serialDay));
}

string todayDate() {
    time_t now = time(nullptr);
    tm localTime = {};

#ifdef _WIN32
    localtime_s(&localTime, &now);
#else
    localtime_r(&now, &localTime);
#endif

    Date today;
    today.year = localTime.tm_year + 1900;
    today.month = localTime.tm_mon + 1;
    today.day = localTime.tm_mday;
    return formatDate(today);
}
