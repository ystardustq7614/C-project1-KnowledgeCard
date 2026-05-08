#include "algo_decay.h"
#include "algo_recommend.h"
#include "algo_sm2.h"

#include <iostream>
#include <string>
#include <vector>

using std::cerr;
using std::cout;
using std::string;
using std::to_string;
using std::vector;

/*
测试职责：
- 验证算法层纯函数的输入输出契约，不依赖全局登录状态、数据文件或控制台交互。

关键约束：
- 这些断言是 README 中复习、衰减、推荐规则的回归保护；调整算法阈值时必须同步修改文档和这里的期望值。
- 测试只覆盖确定性边界和排序规则，不验证业务层保存、日志写入或菜单流程。
*/
namespace {

int failures = 0;

// 自定义轻量断言，避免为教学项目引入第三方测试框架。
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

void expectInRange(const string& name, int actual, int minValue, int maxValue) {
    if (actual < minValue || actual > maxValue) {
        fail(name, "expected value in range [" + to_string(minValue) + ", " + to_string(maxValue) + "], got " + to_string(actual));
        return;
    }
    pass(name);
}

void testSm2Forget() {
    // 忘记场景保护“重置间隔、清空连续答对、掌握度下限”三个调度契约。
    ReviewResult result = calculateNextReview(60, 7, 3, 1);

    expectEqual("sm2 forget lowers mastery", result.newMastery, 40);
    expectEqual("sm2 forget resets interval", result.newInterval, 1);
    expectEqual("sm2 forget resets streak", result.correctStreak, 0);

    ReviewResult floorResult = calculateNextReview(10, 7, 2, 1);
    expectEqual("sm2 forget clamps mastery to zero", floorResult.newMastery, 0);
}

void testSm2Fuzzy() {
    // 模糊场景不应缩短间隔，但必须修正旧数据可能带来的 0 间隔。
    ReviewResult result = calculateNextReview(96, 0, 4, 2);

    expectEqual("sm2 fuzzy clamps mastery to 100", result.newMastery, 100);
    expectEqual("sm2 fuzzy keeps interval at least one", result.newInterval, 1);
    expectEqual("sm2 fuzzy resets streak", result.correctStreak, 0);
}

void testSm2Remembered() {
    // 记牢场景按 1 -> 3 -> 7 -> 15 -> 翻倍推进，是用户可解释的间隔模型。
    ReviewResult first = calculateNextReview(50, 1, 0, 3);
    expectEqual("sm2 remembered interval 1 to 3", first.newInterval, 3);
    expectEqual("sm2 remembered increases mastery", first.newMastery, 70);
    expectEqual("sm2 remembered increments streak", first.correctStreak, 1);

    ReviewResult second = calculateNextReview(70, 3, 1, 3);
    expectEqual("sm2 remembered interval 3 to 7", second.newInterval, 7);

    ReviewResult third = calculateNextReview(80, 7, 2, 3);
    expectEqual("sm2 remembered interval 7 to 15", third.newInterval, 15);

    ReviewResult later = calculateNextReview(95, 15, 3, 3);
    expectEqual("sm2 remembered doubles long interval", later.newInterval, 30);
    expectInRange("sm2 mastery stays within 0 to 100", later.newMastery, 0, 100);
}

void testDecayNoOverdue() {
    // 0~2 天属于宽限期，避免用户稍晚复习就被扣分。
    DecayResult sameDay = calculateDecay(80, 8, "2026-05-01", "2026-05-01");
    expectFalse("decay same day does not trigger", sameDay.decayed);
    expectEqual("decay same day keeps mastery", sameDay.newMastery, 80);
    expectEqual("decay same day keeps interval", sameDay.newInterval, 8);

    DecayResult gracePeriod = calculateDecay(80, 8, "2026-05-01", "2026-05-03");
    expectFalse("decay two day grace period does not trigger", gracePeriod.decayed);
}

void testDecayOverdue() {
    // 逾期惩罚依赖真实公历日期差，因此覆盖跨月和闰年边界。
    DecayResult overdue = calculateDecay(80, 8, "2026-05-01", "2026-05-04");
    expectTrue("decay triggers after three overdue days", overdue.decayed);
    expectEqual("decay applies overdue mastery penalty", overdue.newMastery, 74);
    expectEqual("decay halves interval", overdue.newInterval, 4);

    DecayResult crossMonth = calculateDecay(80, 8, "2026-01-31", "2026-02-03");
    expectTrue("decay cross month triggers after three days", crossMonth.decayed);
    expectEqual("decay cross month penalty is exact", crossMonth.newMastery, 74);

    DecayResult leapFebruary = calculateDecay(80, 8, "2024-02-28", "2024-03-02");
    expectTrue("decay leap february triggers after three days", leapFebruary.decayed);
    expectEqual("decay leap february penalty is exact", leapFebruary.newMastery, 74);

    DecayResult floorMastery = calculateDecay(35, 8, "2026-04-01", "2026-05-01");
    expectEqual("decay keeps mastery floor at 30", floorMastery.newMastery, 30);

    DecayResult floorInterval = calculateDecay(80, 1, "2026-05-01", "2026-05-04");
    expectEqual("decay keeps interval at least one", floorInterval.newInterval, 1);
}

void testRecommendEmptyAndTopN() {
    // 空输入和 topN=0 都应返回空列表，调用方可以直接安全展示“无推荐”。
    vector<RecommendResult> empty = calculateWeakestChapters({}, 3);
    expectTrue("recommend empty input returns empty", empty.empty());

    vector<RecommendInputItem> items = {
        {"Math", "Algebra", 80},
        {"Physics", "Mechanics", 20},
        {"Physics", "Mechanics", 40},
        {"English", "Grammar", 50},
        {"", "Ignored", 0},
        {"Math", "", 0},
    };

    vector<RecommendResult> topTwo = calculateWeakestChapters(items, 2);
    expectEqual("recommend top n size", static_cast<int>(topTwo.size()), 2);
    expectEqual("recommend weakest subject first", topTwo[0].subject, string("Physics"));
    expectEqual("recommend weakest chapter first", topTwo[0].chapter, string("Mechanics"));
    expectEqual("recommend average mastery", topTwo[0].avgMastery, 30);
    expectEqual("recommend item count", topTwo[0].itemCount, 2);
    expectEqual("recommend second weakest subject", topTwo[1].subject, string("English"));

    vector<RecommendResult> topZero = calculateWeakestChapters(items, 0);
    expectTrue("recommend top zero returns empty", topZero.empty());
}

void testRecommendTieBreaker() {
    // 同平均掌握度时优先推荐样本更多的章节，避免单条记录造成不稳定推荐。
    vector<RecommendInputItem> items = {
        {"A", "One", 40},
        {"B", "Two", 40},
        {"B", "Two", 40},
        {"C", "Three", 90},
    };

    vector<RecommendResult> results = calculateWeakestChapters(items, 2);
    expectEqual("recommend tie breaker prefers larger item count", results[0].subject, string("B"));
    expectEqual("recommend tie breaker item count", results[0].itemCount, 2);
}

}  // namespace

int main() {
    testSm2Forget();
    testSm2Fuzzy();
    testSm2Remembered();
    testDecayNoOverdue();
    testDecayOverdue();
    testRecommendEmptyAndTopN();
    testRecommendTieBreaker();

    if (failures != 0) {
        cerr << failures << " algorithm assertion(s) failed." << '\n';
        return 1;
    }

    cout << "All algorithm tests passed." << '\n';
    return 0;
}
