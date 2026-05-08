// 引入本算法对应的头文件：DecayResult 结构体和 calculateDecay 声明都在这里。
#include "algo_decay.h"

// 引入日期工具：daysBetweenDates 用来计算真实公历日期差。
#include "date_utils.h"

// algorithm 提供 std::max，用于掌握度和间隔的下限保护。
#include <algorithm>

// 只引入本文件实际使用的标准库名字，避免命名空间整体引入污染。
using std::max;
using std::string;

/*
[导读]
- 本文件计算“记录逾期后是否需要记忆衰减”。
- 调用方是 main.cpp 登录后的 applyGlobalDecay()。
- 这里只做纯计算，不直接修改卡片/错题，也不保存文件。

[输入输出]
- 输入 currentMastery：当前掌握度。
- 输入 currentInterval：当前复习间隔。
- 输入 nextReviewDate：原计划下一次复习日期，格式应为 YYYY-MM-DD。
- 输入 todayDate：今天日期，格式应为 YYYY-MM-DD。
- 输出 DecayResult：衰减后的掌握度、衰减后的间隔、是否真的触发衰减。

[核心规则]
1. overdueDays = todayDate - nextReviewDate。
2. overdueDays < 3：仍在宽限期内，不衰减。
3. overdueDays >= 3：触发衰减。
4. 掌握度扣分 = overdueDays * 2，但最低保留到 30。
5. 间隔减半，但最低保留到 1。

[你以后最常改的地方]
- 宽限期阈值：if (overdueDays >= 3) 里的 3。
- 扣分倍率：int penalty = overdueDays * 2 里的 2。
- 掌握度下限：max(30, targetMastery) 里的 30。
- 间隔缩短策略：currentInterval / 2。

[不建议随便改的地方]
- daysBetweenDates(...)：必须用真实日期差，不能退回只比较 day 字段的近似算法。
- max(1, ...)：必须保护间隔不能变成 0，否则下一轮复习计划会异常。
- decayed=false 时保持原值：调用方依赖这个返回契约来简化写回逻辑。

[简化说明]
- 衰减采用线性扣分和间隔减半，便于教学解释。
- 真实系统可能会按材料类型、历史表现和难度动态调整，但当前 MVP 先保持可解释。
*/
DecayResult calculateDecay(int currentMastery, int currentInterval, const string& nextReviewDate, const string& todayDate) {
    // 创建返回结果对象。
    DecayResult res;

    // 默认不衰减：先把输出值设置成输入值。
    // 如果后面判断没有逾期，函数可以直接返回这个默认结果。
    res.newMastery = currentMastery;

    // 默认保持原复习间隔。
    res.newInterval = currentInterval;

    // 默认标记为未衰减。
    res.decayed = false;

    // 计算 todayDate 相对于 nextReviewDate 晚了多少天。
    // 结果为负数表示还没到复习日，0 表示当天，正数表示已经逾期。
    int overdueDays = daysBetweenDates(nextReviewDate, todayDate);
    
    // 3 天宽限期是业务阈值；修改后需要同步算法测试和 README 中的算法说明。
    // [可改] 如果想让系统更严格，可以把 3 改小；如果想更宽松，可以改大。
    if (overdueDays >= 3) {
        // 只有进入这个分支，才算真正发生衰减。
        res.decayed = true;

        // 按逾期天数线性扣分，便于向用户解释“越拖越需要复习”的原因。
        // [可改] 这里的 2 是每天逾期扣 2 分。
        int penalty = overdueDays * 2;

        // 先计算未做下限保护的目标掌握度。
        int targetMastery = currentMastery - penalty;
        
        // 掌握度最低保留到 30，避免长期未复习后直接掉到不可恢复的极低值。
        res.newMastery = max(30, targetMastery);
        
        // 间隔减半让逾期内容更快回到今日复习；下限保护异常旧数据。
        // [可改] 如果想更激进，可以改成 currentInterval / 3；如果想更温和，可以减少缩短幅度。
        res.newInterval = max(1, currentInterval / 2);
    }

    // 返回计算结果；调用方负责根据 decayed 决定是否写日志、保存数据。
    return res;
}
