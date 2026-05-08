// 引入记忆衰减算法：calculateDecay 和 DecayResult 都声明在这里。
#include "algo_decay.h"

// 引入薄弱章节推荐算法：calculateWeakestChapters 及推荐输入/输出结构声明在这里。
#include "algo_recommend.h"

// 引入 SM2 风格复习调度算法：calculateNextReview 和 ReviewResult 都声明在这里。
#include "algo_sm2.h"

// iostream 提供 cout/cerr，用于输出测试通过/失败信息。
#include <iostream>

// string 提供 std::string，用于断言名称和错误消息。
#include <string>

// vector 提供 std::vector，用于构造推荐算法的多条输入数据。
#include <vector>

// 测试文件也避免整体引入 std 命名空间，只引入断言辅助函数实际使用的标准库名字。
using std::cerr;
using std::cout;
using std::string;
using std::to_string;
using std::vector;

/*
[导读]
- 这个文件是算法层 C++ 单元测试。
- 它直接调用 algo_sm2、algo_decay、algo_recommend 的纯函数。
- 它不启动 project1.exe，不读写 data/*.txt，也不依赖用户登录状态。
- PowerShell 脚本 tools/run_algorithm_tests.ps1 负责编译并运行这个测试程序。

[测试目标]
1. SM2 复习调度：验证“忘记 / 模糊 / 记牢”三类评分如何影响掌握度、复习间隔和连续答对次数。
2. 记忆衰减：验证逾期天数如何触发扣分、缩短间隔，以及下限保护。
3. 薄弱章节推荐：验证空输入、topN、平均掌握度、样本数量和排序规则。

[对应业务]
- review.cpp -> algo_sm2：用户复习卡片后，更新下一次复习计划。
- main.cpp 登录衰减 -> algo_decay：用户登录时，对逾期卡片做记忆衰减。
- main.cpp/practice.cpp 推荐 -> algo_recommend：根据掌握度推荐薄弱章节练习。

[你以后最常改的地方]
- calculateNextReview(...) 的输入参数和预期值：当你调整评分规则、掌握度增减幅度、间隔策略时改这里。
- calculateDecay(...) 的日期和预期值：当你调整宽限期、扣分公式、掌握度/间隔下限时改这里。
- 推荐算法 items 测试数据：当你想增加排序规则、过滤规则、更多科目章节场景时改这里。

[不建议随便改的地方]
- 下限/上限用例：例如掌握度不能低于 0、不能高于 100，间隔不能低于 1。
- 跨月和闰年衰减用例：这些保护算法必须使用真实日期差，而不是简单按月份或字符串估算。
- 推荐排序并列规则：业务展示依赖稳定排序，随意改会导致“同样数据每次推荐不同”的体验问题。
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

// 断言整数落在指定闭区间 [minValue, maxValue] 内。
void expectInRange(const string& name, int actual, int minValue, int maxValue) {
    // 只要低于下限或高于上限，就记录失败。
    if (actual < minValue || actual > maxValue) {
        fail(name, "expected value in range [" + to_string(minValue) + ", " + to_string(maxValue) + "], got " + to_string(actual));
        return;
    }

    // 落在区间内则记录通过。
    pass(name);
}

// 测试 SM2 调度里的“忘记”分支。
void testSm2Forget() {
    // 用户选择 1 通常表示“忘记”：掌握度要下降、复习间隔要重置、连续答对次数要清零。
    // [可改] 第 1 个参数是旧掌握度，第 2 个参数是旧间隔，第 3 个参数是旧连续答对次数，第 4 个参数是评分。
    ReviewResult result = calculateNextReview(60, 7, 3, 1);

    // 60 分忘记后应降低到 40 分，保护掌握度扣分规则。
    expectEqual("sm2 forget lowers mastery", result.newMastery, 40);

    // 忘记后下一次应很快再复习，所以间隔重置为 1 天。
    expectEqual("sm2 forget resets interval", result.newInterval, 1);

    // 忘记后连续答对记录清零。
    expectEqual("sm2 forget resets streak", result.correctStreak, 0);

    // 低掌握度继续忘记时，掌握度不能扣成负数。
    ReviewResult floorResult = calculateNextReview(10, 7, 2, 1);

    // 掌握度下限是 0。
    expectEqual("sm2 forget clamps mastery to zero", floorResult.newMastery, 0);
}

// 测试 SM2 调度里的“模糊”分支。
void testSm2Fuzzy() {
    // 用户选择 2 通常表示“模糊”：不奖励连续答对，但也不直接走忘记逻辑。
    // 这里 oldInterval=0 是为了保护旧数据或异常数据的修正能力。
    ReviewResult result = calculateNextReview(96, 0, 4, 2);

    // 96 分模糊后可能加分，但掌握度不能超过 100。
    expectEqual("sm2 fuzzy clamps mastery to 100", result.newMastery, 100);

    // 即使旧间隔是 0，算法也必须把新间隔保护到至少 1 天。
    expectEqual("sm2 fuzzy keeps interval at least one", result.newInterval, 1);

    // 模糊不算真正答对，所以连续答对次数清零。
    expectEqual("sm2 fuzzy resets streak", result.correctStreak, 0);
}

// 测试 SM2 调度里的“记牢”分支。
void testSm2Remembered() {
    // 用户选择 3 通常表示“记牢”：增加掌握度、增加连续答对次数，并拉长复习间隔。

    // 旧间隔为 1 天时，记牢后进入 3 天间隔。
    ReviewResult first = calculateNextReview(50, 1, 0, 3);

    // 保护 1 -> 3 的间隔升级规则。
    expectEqual("sm2 remembered interval 1 to 3", first.newInterval, 3);

    // 掌握度从 50 提升到 70。
    expectEqual("sm2 remembered increases mastery", first.newMastery, 70);

    // 连续答对次数从 0 增加到 1。
    expectEqual("sm2 remembered increments streak", first.correctStreak, 1);

    // 旧间隔为 3 天时，记牢后进入 7 天间隔。
    ReviewResult second = calculateNextReview(70, 3, 1, 3);

    // 保护 3 -> 7 的间隔升级规则。
    expectEqual("sm2 remembered interval 3 to 7", second.newInterval, 7);

    // 旧间隔为 7 天时，记牢后进入 15 天间隔。
    ReviewResult third = calculateNextReview(80, 7, 2, 3);

    // 保护 7 -> 15 的间隔升级规则。
    expectEqual("sm2 remembered interval 7 to 15", third.newInterval, 15);

    // 更长间隔继续记牢时，当前规则按翻倍推进。
    ReviewResult later = calculateNextReview(95, 15, 3, 3);

    // 保护 15 -> 30 的长间隔翻倍规则。
    expectEqual("sm2 remembered doubles long interval", later.newInterval, 30);

    // 掌握度即使继续奖励，也必须保持在 0~100 范围内。
    expectInRange("sm2 mastery stays within 0 to 100", later.newMastery, 0, 100);
}

// 测试记忆衰减里的“未逾期/宽限期”分支。
void testDecayNoOverdue() {
    // 同一天检查时不应该触发衰减。
    DecayResult sameDay = calculateDecay(80, 8, "2026-05-01", "2026-05-01");

    // decayed=false 表示没有真正发生衰减。
    expectFalse("decay same day does not trigger", sameDay.decayed);

    // 未衰减时掌握度保持原值。
    expectEqual("decay same day keeps mastery", sameDay.newMastery, 80);

    // 未衰减时间隔也保持原值。
    expectEqual("decay same day keeps interval", sameDay.newInterval, 8);

    // 当前规则把 0~2 天视作宽限期，避免用户稍晚复习就被扣分。
    DecayResult gracePeriod = calculateDecay(80, 8, "2026-05-01", "2026-05-03");

    // 2 天差仍不触发衰减。
    expectFalse("decay two day grace period does not trigger", gracePeriod.decayed);
}

// 测试记忆衰减里的“逾期”分支。
void testDecayOverdue() {
    // 5 月 1 日到 5 月 4 日相差 3 天，刚好越过宽限期，应触发衰减。
    DecayResult overdue = calculateDecay(80, 8, "2026-05-01", "2026-05-04");

    // 3 天逾期应触发 decayed=true。
    expectTrue("decay triggers after three overdue days", overdue.decayed);

    // 当前扣分规则下，80 应降到 74。
    expectEqual("decay applies overdue mastery penalty", overdue.newMastery, 74);

    // 当前衰减规则会把复习间隔从 8 天缩短到 4 天。
    expectEqual("decay halves interval", overdue.newInterval, 4);

    // 跨月场景：1 月 31 日到 2 月 3 日也是 3 天。
    DecayResult crossMonth = calculateDecay(80, 8, "2026-01-31", "2026-02-03");

    // 跨月时仍应触发衰减，证明算法不是简单比较 day 字段。
    expectTrue("decay cross month triggers after three days", crossMonth.decayed);

    // 跨月时扣分结果也应和普通 3 天逾期一致。
    expectEqual("decay cross month penalty is exact", crossMonth.newMastery, 74);

    // 闰年场景：2024-02-28 到 2024-03-02 中间包含 2 月 29 日，真实差值也是 3 天。
    DecayResult leapFebruary = calculateDecay(80, 8, "2024-02-28", "2024-03-02");

    // 闰年跨 2 月时也应触发衰减。
    expectTrue("decay leap february triggers after three days", leapFebruary.decayed);

    // 闰年场景的扣分应和普通 3 天逾期一致。
    expectEqual("decay leap february penalty is exact", leapFebruary.newMastery, 74);

    // 长时间逾期会产生较大扣分，但掌握度不能低于下限。
    DecayResult floorMastery = calculateDecay(35, 8, "2026-04-01", "2026-05-01");

    // 当前掌握度下限是 30。
    expectEqual("decay keeps mastery floor at 30", floorMastery.newMastery, 30);

    // 旧间隔只有 1 天时，即使衰减也不能缩到 0。
    DecayResult floorInterval = calculateDecay(80, 1, "2026-05-01", "2026-05-04");

    // 当前间隔下限是 1 天。
    expectEqual("decay keeps interval at least one", floorInterval.newInterval, 1);
}

// 测试推荐算法的空输入、topN 和基础排序。
void testRecommendEmptyAndTopN() {
    // 空输入应返回空推荐列表，调用方可以直接展示“暂无推荐”。
    vector<RecommendResult> empty = calculateWeakestChapters({}, 3);

    // 空输入不能崩溃，也不能凭空生成推荐项。
    expectTrue("recommend empty input returns empty", empty.empty());

    // 构造推荐输入数据。
    // [可改] 这里最适合新增科目、章节、掌握度样本，用来覆盖更多排序或过滤规则。
    vector<RecommendInputItem> items = {
        // Math/Algebra 只有一条记录，平均掌握度是 80。
        {"Math", "Algebra", 80},

        // Physics/Mechanics 有两条记录，平均掌握度是 (20 + 40) / 2 = 30。
        {"Physics", "Mechanics", 20},
        {"Physics", "Mechanics", 40},

        // English/Grammar 只有一条记录，平均掌握度是 50。
        {"English", "Grammar", 50},

        // 空科目应被过滤，避免生成无效推荐。
        {"", "Ignored", 0},

        // 空章节应被过滤，避免生成无效推荐。
        {"Math", "", 0},
    };

    // 只取最薄弱的前 2 个章节。
    vector<RecommendResult> topTwo = calculateWeakestChapters(items, 2);

    // topN=2 时结果数量应为 2。
    expectEqual("recommend top n size", static_cast<int>(topTwo.size()), 2);

    // 平均掌握度最低的是 Physics/Mechanics，所以排第一。
    expectEqual("recommend weakest subject first", topTwo[0].subject, string("Physics"));

    // 第一项的章节名应是 Mechanics。
    expectEqual("recommend weakest chapter first", topTwo[0].chapter, string("Mechanics"));

    // Physics/Mechanics 的平均掌握度应为 30。
    expectEqual("recommend average mastery", topTwo[0].avgMastery, 30);

    // Physics/Mechanics 聚合了 2 条卡片/样本。
    expectEqual("recommend item count", topTwo[0].itemCount, 2);

    // 第二薄弱的是 English/Grammar，平均掌握度 50。
    expectEqual("recommend second weakest subject", topTwo[1].subject, string("English"));

    // topN=0 表示调用方不需要任何推荐。
    vector<RecommendResult> topZero = calculateWeakestChapters(items, 0);

    // topN=0 应返回空列表。
    expectTrue("recommend top zero returns empty", topZero.empty());
}

// 测试推荐算法的并列排序规则。
void testRecommendTieBreaker() {
    // 当平均掌握度相同，当前规则优先推荐样本更多的章节。
    // 这样能避免只有一条偶然低分记录的章节过度影响推荐稳定性。
    vector<RecommendInputItem> items = {
        // A/One 平均掌握度 40，样本数 1。
        {"A", "One", 40},

        // B/Two 平均掌握度 40，样本数 2。
        {"B", "Two", 40},
        {"B", "Two", 40},

        // C/Three 平均掌握度 90，不属于薄弱章节。
        {"C", "Three", 90},
    };

    // 取前 2 个推荐结果。
    vector<RecommendResult> results = calculateWeakestChapters(items, 2);

    // A 和 B 平均掌握度相同，但 B 样本更多，所以 B 应排在 A 前面。
    expectEqual("recommend tie breaker prefers larger item count", results[0].subject, string("B"));

    // 确认 B/Two 的聚合样本数是 2。
    expectEqual("recommend tie breaker item count", results[0].itemCount, 2);
}

}  // namespace

// 测试程序入口。
int main() {
    // 运行 SM2 “忘记”分支测试。
    testSm2Forget();

    // 运行 SM2 “模糊”分支测试。
    testSm2Fuzzy();

    // 运行 SM2 “记牢”分支测试。
    testSm2Remembered();

    // 运行未逾期/宽限期衰减测试。
    testDecayNoOverdue();

    // 运行逾期衰减测试。
    testDecayOverdue();

    // 运行推荐算法空输入、topN 和基础排序测试。
    testRecommendEmptyAndTopN();

    // 运行推荐算法并列排序测试。
    testRecommendTieBreaker();

    // 如果任何断言失败，failures 会大于 0。
    if (failures != 0) {
        // 输出失败数量。
        cerr << failures << " algorithm assertion(s) failed." << '\n';

        // 返回非 0，PowerShell 调度脚本会据此判定测试失败。
        return 1;
    }

    // 所有断言通过。
    cout << "All algorithm tests passed." << '\n';

    // 返回 0，表示测试通过。
    return 0;
}
