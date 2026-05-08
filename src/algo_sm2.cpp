#include "algo_sm2.h"
#include <algorithm>

using std::max;
using std::min;

/*
实现说明：
- 这里使用项目内简化版 SM-2，不追求完整 SuperMemo 参数模型。
- 业务目标是让初学者能理解和调试“反馈 -> 间隔变化”的闭环，因此采用固定阶段间隔。
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
