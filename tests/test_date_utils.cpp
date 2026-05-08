#include "date_utils.h"

#include <iostream>
#include <string>

using std::cerr;
using std::cout;
using std::string;
using std::to_string;

/*
测试职责：
- 验证 date_utils 的 YYYY-MM-DD 解析、合法性、日期差和日期加减契约。

关键约束：
- 业务层依赖这些函数支撑复习到期、记忆衰减和统计窗口；跨月、跨年、闰年边界不能退回近似算法。
- 非法日期的返回策略是业务安全契约：日期差返回 0，日期加减返回原字符串。
*/
namespace {

int failures = 0;

// 自定义轻量断言，保持测试可直接用 g++ 编译运行。
void fail(const string& name, const string& message) {
    ++failures;
    cerr << "[FAIL] " << name << ": " << message << '\n';
}

void pass(const string& name) {
    cout << "[PASS] " << name << '\n';
}

template <typename T>
void expectEqual(const string& name, const T& actual, const T& expected) {
    if (actual != expected) {
        fail(name, "expected " + to_string(expected) + ", got " + to_string(actual));
        return;
    }
    pass(name);
}

void expectEqual(const string& name, const string& actual, const string& expected) {
    if (actual != expected) {
        fail(name, "expected '" + expected + "', got '" + actual + "'");
        return;
    }
    pass(name);
}

void expectTrue(const string& name, bool condition) {
    if (!condition) {
        fail(name, "condition is false");
        return;
    }
    pass(name);
}

void expectFalse(const string& name, bool condition) {
    if (condition) {
        fail(name, "condition is true");
        return;
    }
    pass(name);
}

void testParseDate() {
    // parseDate 既检查固定格式，也检查真实日历日期合法性。
    Date date;
    expectTrue("parse valid date", parseDate("2026-05-05", date));
    expectEqual("parse year", date.year, 2026);
    expectEqual("parse month", date.month, 5);
    expectEqual("parse day", date.day, 5);

    expectFalse("reject bad separator", isValidDate("2026/05/05"));
    expectFalse("reject invalid month", isValidDate("2026-13-01"));
    expectFalse("reject invalid day", isValidDate("2026-04-31"));
    expectFalse("reject non leap feb 29", isValidDate("2025-02-29"));
    expectTrue("accept leap feb 29", isValidDate("2024-02-29"));
}

void testDaysBetweenDates() {
    // 日期差必须支持反向结果，统计和衰减逻辑依赖负数表示未到期。
    expectEqual("same day difference", daysBetweenDates("2026-05-05", "2026-05-05"), 0);
    expectEqual("cross month difference", daysBetweenDates("2026-01-31", "2026-02-01"), 1);
    expectEqual("cross year difference", daysBetweenDates("2025-12-31", "2026-01-01"), 1);
    expectEqual("leap february difference", daysBetweenDates("2024-02-28", "2024-03-01"), 2);
    expectEqual("reverse difference", daysBetweenDates("2026-05-05", "2026-05-02"), -3);
    expectEqual("invalid date difference", daysBetweenDates("bad-date", "2026-05-05"), 0);
}

void testAddDaysToDate() {
    // 加减日期覆盖月末、年末和闰日，防止复习计划在边界日期漂移。
    expectEqual("add across month", addDaysToDate("2026-01-31", 1), string("2026-02-01"));
    expectEqual("add across year", addDaysToDate("2025-12-31", 1), string("2026-01-01"));
    expectEqual("add leap day", addDaysToDate("2024-02-28", 1), string("2024-02-29"));
    expectEqual("add after leap day", addDaysToDate("2024-02-28", 2), string("2024-03-01"));
    expectEqual("subtract days", addDaysToDate("2026-03-01", -1), string("2026-02-28"));
    expectEqual("invalid add returns original", addDaysToDate("bad-date", 3), string("bad-date"));
}

}  // namespace

int main() {
    testParseDate();
    testDaysBetweenDates();
    testAddDaysToDate();

    if (failures != 0) {
        cerr << failures << " date assertion(s) failed." << '\n';
        return 1;
    }

    cout << "All date tests passed." << '\n';
    return 0;
}
