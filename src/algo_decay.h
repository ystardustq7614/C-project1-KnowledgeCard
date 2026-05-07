#ifndef ALGO_DECAY_H
#define ALGO_DECAY_H

#include <string>

/*
[导读]
- 本头文件声明记忆衰减算法，用于登录后的全局逾期扫描。

[输入输出]
- 输入：当前掌握度、当前间隔、下次复习日期、今天日期。
- 输出：衰减后的掌握度/间隔，以及是否触发衰减。

[易错点]
- 这是纯函数，不修改卡片/错题，不保存文件；登录后的全局扫描由 main.cpp 负责。
- 日期必须是 YYYY-MM-DD；非法日期由 date_utils 返回 0 天差，因此不会触发衰减。
*/

// ================================================================
// 纯数据结构：记忆衰减结果
// ================================================================
struct DecayResult {
    int newMastery;
    int newInterval;
    bool decayed;
};

// ================================================================
// 纯函数算法：计算记忆热度衰减
// ================================================================
// 依据逾期天数，计算惩罚后的新掌握度与新间隔。
// 引入“破产保护”：掌握度最多退化到 30，避免极大挫败感。
// ================================================================
// 返回：decayed=false 时 newMastery/newInterval 保持输入值，调用方无需额外判断原值。
DecayResult calculateDecay(int currentMastery, int currentInterval, const std::string& nextReviewDate, const std::string& todayDate);

#endif
