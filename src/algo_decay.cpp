#include "algo_decay.h"
#include "date_utils.h"
#include <algorithm>

using namespace std;

/*
[导读]
- 本文件计算“记录逾期后是否需要记忆衰减”，调用方是 main.cpp 登录后的 applyGlobalDecay()。

[输入输出]
- 输入：当前掌握度、当前复习间隔、下次复习日期、今天日期。
- 输出：DecayResult；不直接修改卡片/错题，也不保存文件。

[学习重点]
- 逾期天数来自 date_utils 的真实公历日期差，不再使用按月份近似的算法。

[简化说明]
- 衰减采用线性扣分和间隔减半，便于教学解释；真实系统可能会按材料类型、历史表现和难度动态调整。

[实验]
- 把宽限期从 3 天改成 1 天，再运行算法测试观察逾期边界如何变化。
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
