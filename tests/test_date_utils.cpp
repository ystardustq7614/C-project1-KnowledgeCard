// 引入被测模块的头文件：Date 结构、parseDate/isValidDate/daysBetweenDates/addDaysToDate 都声明在这里。
#include "date_utils.h"

// iostream 提供 cout/cerr，用于输出测试通过/失败信息。
#include <iostream>

// string 提供 std::string，用于比较日期字符串和错误消息。
#include <string>

// 测试文件也避免整体引入 std 命名空间，只引入断言辅助函数实际使用的标准库名字。
using std::cerr;
using std::cout;
using std::string;
using std::to_string;

/*
[导读]
- 这个文件是 date_utils 的 C++ 单元测试。
- 它直接调用 C++ 日期工具函数，不启动 project1.exe，也不读写 data/*.txt。
- PowerShell 脚本 tools/run_algorithm_tests.ps1 负责编译并运行这个测试程序。

[测试目标]
1. parseDate：验证 YYYY-MM-DD 是否能解析成 Date 结构。
2. isValidDate：验证非法日期能否被拒绝。
3. daysBetweenDates：验证两个日期之间的天数差。
4. addDaysToDate：验证给日期加/减天数后的结果。

[对应业务]
- 今日复习：判断 nextReviewDate 是否到期。
- 记忆衰减：计算逾期天数。
- 统计分析：计算最近 7 天窗口。
- 复习计划：根据间隔天数计算下一次复习日期。

[你以后最常改的地方]
- 测试用例里的日期字符串：如果想增加边界场景，例如跨百年、更多非法格式，可以加 expect。
- 预期值：如果 date_utils 的非法日期策略改变，需要同步改这里。

[不建议随便改的地方]
- 非法日期策略：当前契约是日期差返回 0，加减日期返回原字符串；业务层依赖这个安全兜底。
- 跨月、跨年、闰年用例：这些是防止日期算法退回近似实现的核心保护。
*/

// 匿名 namespace：让本文件里的辅助函数只在当前编译单元可见。
// 这样不会和其他测试文件里的同名 fail/pass/expectEqual 冲突。
namespace {

// 记录失败断言数量。
// main() 最后会用它决定测试程序返回 0 还是 1。
int failures = 0;

// 自定义轻量断言失败函数，保持测试可直接用 g++ 编译运行，不引入第三方测试框架。
void fail(const string& name, const string& message) {
    // 每失败一次，累计失败数量加 1。
    ++failures;

    // cerr 输出失败信息；通常 stderr 更适合错误和失败日志。
    cerr << "[FAIL] " << name << ": " << message << '\n';
}

// 自定义轻量断言通过函数。
void pass(const string& name) {
    // cout 输出通过信息，便于运行测试时看到每个 case 的结果。
    cout << "[PASS] " << name << '\n';
}

// 泛型相等断言：适用于 int、bool 等能用 to_string 转成文本的类型。
template <typename T>
void expectEqual(const string& name, const T& actual, const T& expected) {
    // 如果实际值和期望值不同，就记录失败并返回。
    if (actual != expected) {
        fail(name, "expected " + to_string(expected) + ", got " + to_string(actual));
        return;
    }

    // 值相等则记录通过。
    pass(name);
}

// string 专用相等断言。
// 单独重载是因为 std::to_string 不支持 string。
void expectEqual(const string& name, const string& actual, const string& expected) {
    // 字符串不相等时输出带引号的错误信息，便于看清空字符串或空格。
    if (actual != expected) {
        fail(name, "expected '" + expected + "', got '" + actual + "'");
        return;
    }

    // 字符串相等则记录通过。
    pass(name);
}

// 断言条件为 true。
void expectTrue(const string& name, bool condition) {
    // 条件为 false 时记录失败。
    if (!condition) {
        fail(name, "condition is false");
        return;
    }

    // 条件为 true 时记录通过。
    pass(name);
}

// 断言条件为 false。
void expectFalse(const string& name, bool condition) {
    // 条件为 true 时记录失败。
    if (condition) {
        fail(name, "condition is true");
        return;
    }

    // 条件为 false 时记录通过。
    pass(name);
}

// 测试日期解析和日期合法性判断。
void testParseDate() {
    // parseDate 既检查固定格式，也检查真实日历日期合法性。
    // Date 是 date_utils.h 中定义的结构体，通常包含 year/month/day。
    Date date;

    // 合法日期应解析成功，并把 year/month/day 写入 date。
    expectTrue("parse valid date", parseDate("2026-05-05", date));

    // 校验解析出的年份。
    expectEqual("parse year", date.year, 2026);

    // 校验解析出的月份。
    expectEqual("parse month", date.month, 5);

    // 校验解析出的日期。
    expectEqual("parse day", date.day, 5);

    // 斜杠分隔不是项目约定格式，应被拒绝。
    expectFalse("reject bad separator", isValidDate("2026/05/05"));

    // 月份 13 不存在，应被拒绝。
    expectFalse("reject invalid month", isValidDate("2026-13-01"));

    // 4 月没有 31 日，应被拒绝。
    expectFalse("reject invalid day", isValidDate("2026-04-31"));

    // 2025 不是闰年，2 月 29 日应被拒绝。
    expectFalse("reject non leap feb 29", isValidDate("2025-02-29"));

    // 2024 是闰年，2 月 29 日应被接受。
    expectTrue("accept leap feb 29", isValidDate("2024-02-29"));
}

// 测试两个日期之间的天数差。
void testDaysBetweenDates() {
    // 日期差必须支持反向结果，统计和衰减逻辑依赖负数表示未到期。

    // 同一天的日期差应为 0。
    expectEqual("same day difference", daysBetweenDates("2026-05-05", "2026-05-05"), 0);

    // 1 月 31 日到 2 月 1 日跨月，差 1 天。
    expectEqual("cross month difference", daysBetweenDates("2026-01-31", "2026-02-01"), 1);

    // 2025 年 12 月 31 日到 2026 年 1 月 1 日跨年，差 1 天。
    expectEqual("cross year difference", daysBetweenDates("2025-12-31", "2026-01-01"), 1);

    // 2024 是闰年，2 月 28 到 3 月 1 中间跨过 2 月 29，所以差 2 天。
    expectEqual("leap february difference", daysBetweenDates("2024-02-28", "2024-03-01"), 2);

    // 结束日期早于开始日期时应返回负数，表示“还没到期”。
    expectEqual("reverse difference", daysBetweenDates("2026-05-05", "2026-05-02"), -3);

    // 非法日期采用安全兜底：日期差返回 0，避免异常数据引发连锁错误。
    expectEqual("invalid date difference", daysBetweenDates("bad-date", "2026-05-05"), 0);
}

// 测试日期加减天数。
void testAddDaysToDate() {
    // 加减日期覆盖月末、年末和闰日，防止复习计划在边界日期漂移。

    // 1 月 31 日加 1 天应进入 2 月 1 日。
    expectEqual("add across month", addDaysToDate("2026-01-31", 1), string("2026-02-01"));

    // 年末加 1 天应进入下一年。
    expectEqual("add across year", addDaysToDate("2025-12-31", 1), string("2026-01-01"));

    // 闰年 2 月 28 日加 1 天应是 2 月 29 日。
    expectEqual("add leap day", addDaysToDate("2024-02-28", 1), string("2024-02-29"));

    // 闰年 2 月 28 日加 2 天应进入 3 月 1 日。
    expectEqual("add after leap day", addDaysToDate("2024-02-28", 2), string("2024-03-01"));

    // 支持负数天数：3 月 1 日减 1 天应是 2 月 28 日。
    expectEqual("subtract days", addDaysToDate("2026-03-01", -1), string("2026-02-28"));

    // 非法日期采用安全兜底：加减日期返回原字符串。
    expectEqual("invalid add returns original", addDaysToDate("bad-date", 3), string("bad-date"));
}

}  // namespace

// 测试程序入口。
int main() {
    // 运行日期解析与合法性测试。
    testParseDate();

    // 运行日期差测试。
    testDaysBetweenDates();

    // 运行日期加减测试。
    testAddDaysToDate();

    // 如果任何断言失败，failures 会大于 0。
    if (failures != 0) {
        // 输出失败数量。
        cerr << failures << " date assertion(s) failed." << '\n';

        // 返回非 0，PowerShell 调度脚本会据此判定测试失败。
        return 1;
    }

    // 所有断言通过。
    cout << "All date tests passed." << '\n';

    // 返回 0，表示测试通过。
    return 0;
}
