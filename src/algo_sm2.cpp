// 引入本算法对应的头文件：ReviewResult 结构体和 calculateNextReview 声明都在这里。
#include "algo_sm2.h"

// algorithm 提供 std::max/std::min，用于把掌握度和间隔限制在安全范围内。
#include <algorithm>

// 只引入本文件实际使用的标准库函数，避免命名空间整体引入污染。
using std::max;
using std::min;

/*
[导读]
- 本文件只负责“一次复习反馈 -> 新掌握度/新间隔/新连续答对次数”的纯计算。
- review.cpp 会调用这里的结果，再写回 Card/WrongQuestion 和 ReviewLog。
- 这里不读用户输入、不修改全局数组、不保存文件，因此很适合做 C++ 单元测试。

[输入输出]
- 输入 oldMastery：复习前掌握度，业务上通常是 0~100。
- 输入 oldInterval：复习前间隔天数，业务上最小应为 1；这里也会对异常旧数据做下限保护。
- 输入 oldStreak：复习前连续答对次数。
- 输入 score：用户评分，当前交互约定为 1=忘记，2=模糊，3=记牢。
- 输出 ReviewResult：新的掌握度、新的间隔、新的连续答对次数。

[核心规则]
1. 忘记：掌握度 -20，间隔重置为 1，连续答对清零。
2. 模糊：掌握度 +5，间隔保持但至少为 1，连续答对清零。
3. 记牢：掌握度 +20，连续答对 +1，间隔按 1/3/7/15/翻倍推进。

[你以后最常改的地方]
- 掌握度增减幅度：oldMastery - 20、oldMastery + 5、oldMastery + 20。
- 间隔阶梯：1 -> 3 -> 7 -> 15 -> 翻倍。
- score 分支含义：如果 UI 评分项变化，要同步改 review.cpp、测试和文档。

[不建议随便改的地方]
- max(0, ...) 和 min(100, ...)：它们保护掌握度永远在 0~100。
- max(1, oldInterval)：它保护旧数据或异常数据不会产生 0 天复习间隔。
- 忘记/模糊清空 correctStreak：否则下一次记牢可能直接继承旧 streak，导致间隔增长过快。

[简化说明]
- 这里不是完整 SuperMemo SM-2 参数模型，目的是让新人能直接观察“反馈 -> 间隔变化”的闭环。
- 如果将来要做更复杂的算法，优先先扩展 tests/test_algorithms.cpp，再改这里。
*/
ReviewResult calculateNextReview(int oldMastery, int oldInterval, int oldStreak, int score) {
    // 创建返回结果对象。
    // 下面每个分支都会把 newMastery/newInterval/correctStreak 三个字段都填完整。
    ReviewResult res;
    
    // score == 1：用户反馈“忘记”。
    if (score == 1) { 
        // 忘记后应尽快再次复习，所以把下一次间隔重置为 1 天。
        res.newInterval = 1;

        // 忘记时必须重置连续答对，避免下一次“记牢”沿用旧 streak 直接放大间隔。
        res.correctStreak = 0;

        // 掌握度扣 20，但不能低于 0。
        // [可改] 如果觉得忘记惩罚太重或太轻，主要改这里的 20，并同步测试预期值。
        res.newMastery = max(0, oldMastery - 20);
    } 
    // score == 2：用户反馈“模糊”。
    else if (score == 2) { 
        // 模糊不缩短间隔，避免用户在“部分记得”时被过度惩罚。
        // max(1, oldInterval) 用来保护旧数据中可能出现的 0 或负间隔。
        res.newInterval = max(1, oldInterval);

        // 模糊不算真正答对，因此连续答对清零。
        res.correctStreak = 0;

        // 掌握度小幅增加 5，但不能超过 100。
        // [可改] 如果你想让“模糊”更接近惩罚或奖励，可以改这里的 5。
        res.newMastery = min(100, oldMastery + 5);
    } 
    // 其他情况当前按“记牢”处理；正常调用方应只传 score == 3 进入这里。
    else { 
        // 记牢会增加连续答对次数。
        res.correctStreak = oldStreak + 1;
        
        // 固定阶段 1/3/7/15 便于解释和测试；长期阶段再翻倍，避免短期内增长过快。
        // [可改] 如果想调整复习节奏，优先改下面这些阶梯值，并同步 tests/test_algorithms.cpp。
        if (oldInterval <= 1) {
            // 新卡片或异常小间隔，记牢后进入 3 天间隔。
            res.newInterval = 3;
        } else if (oldInterval == 3) {
            // 3 天间隔记牢后进入 7 天间隔。
            res.newInterval = 7;
        } else if (oldInterval == 7) {
            // 7 天间隔记牢后进入 15 天间隔。
            res.newInterval = 15;
        } else {
            // 15 天及以上继续记牢时，当前规则直接翻倍。
            res.newInterval = oldInterval * 2;
        }
        
        // 掌握度增加 20，但不能超过 100。
        // [可改] 如果希望“记牢”奖励更平缓，可以改这里的 20。
        res.newMastery = min(100, oldMastery + 20);
    }
    
    // 返回完整计算结果；调用方负责把结果写回卡片/错题并保存。
    return res;
}
