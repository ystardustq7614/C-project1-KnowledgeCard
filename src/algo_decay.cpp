#include "algo_decay.h"
#include "date_utils.h"
#include <algorithm>

using namespace std;

/*
实现说明：
- 衰减只在逾期足够明显时触发，避免用户晚一两天复习就被反复扣分。
- 掌握度下限保留到 30，是为了让旧知识重新进入复习队列，而不是把记录打成“不可恢复”状态。
*/
DecayResult calculateDecay(int currentMastery, int currentInterval, const string& nextReviewDate, const string& todayDate) {
    DecayResult res;
    res.newMastery = currentMastery;
    res.newInterval = currentInterval;
    res.decayed = false;

    int overdueDays = daysBetweenDates(nextReviewDate, todayDate);
    
    // 3 天宽限期是业务阈值；修改后需要同步算法测试和 README 中的算法说明。
    if (overdueDays >= 3) {
        res.decayed = true;

        // 按逾期天数线性扣分，便于向用户解释“越拖越需要复习”的原因。
        int penalty = overdueDays * 2;
        int targetMastery = currentMastery - penalty;
        
        res.newMastery = max(30, targetMastery);
        
        // 间隔减半让逾期内容更快回到今日复习；下限保护异常旧数据。
        res.newInterval = max(1, currentInterval / 2);
    }

    return res;
}
