#include "algo_sm2.h"
#include <algorithm>

using namespace std;

/*
[导读]
- 本文件只负责“一次复习反馈 -> 新掌握度/新间隔/新连续答对次数”的纯计算。
- review.cpp 会调用这里的结果，再写回 Card/WrongQuestion 和 ReviewLog。

[输入输出]
- 输入：旧掌握度、旧间隔、旧连续答对次数、用户评分 1/2/3。
- 输出：ReviewResult，不直接修改全局容器，不保存文件。

[公式对应]
- 教学版规则：忘记扣分并重置间隔；模糊小幅加分但不推进 streak；记牢按 1/3/7/15/翻倍推进。

[简化说明]
- 这里不是完整 SuperMemo SM-2 参数模型，目的是让新人能直接观察“反馈 -> 间隔变化”的闭环。

[实验]
- 调整 3/7/15 的阶梯，再运行 tools/run_algorithm_tests.ps1 观察哪些业务期望需要同步更新。
*/
ReviewResult calculateNextReview(int oldMastery, int oldInterval, int oldStreak, int score) {
    ReviewResult res;
    
    if (score == 1) { 
        // 忘记时必须重置连续答对，避免下一次“记牢”沿用旧 streak 直接放大间隔。
        res.newInterval = 1;
        res.correctStreak = 0;
        res.newMastery = max(0, oldMastery - 20);
    } 
    else if (score == 2) { 
        // 模糊不缩短间隔，避免用户在“部分记得”时被过度惩罚；下限保护旧数据中的异常间隔。
        res.newInterval = max(1, oldInterval);
        res.correctStreak = 0;
        res.newMastery = min(100, oldMastery + 5);
    } 
    else { 
        // 固定阶段 1/3/7/15 便于解释和测试；长期阶段再翻倍，避免短期内增长过快。
        res.correctStreak = oldStreak + 1;
        
        if (oldInterval <= 1) {
            res.newInterval = 3;
        } else if (oldInterval == 3) {
            res.newInterval = 7;
        } else if (oldInterval == 7) {
            res.newInterval = 15;
        } else {
            res.newInterval = oldInterval * 2;
        }
        
        res.newMastery = min(100, oldMastery + 20);
    }
    
    return res;
}
