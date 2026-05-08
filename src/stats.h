#ifndef STATS_H
#define STATS_H

/*
[导读]
- 本头文件声明统计模块入口，用于从当前内存数据生成用户可读的统计视图。

[输入输出]
- 输入：cards/wrongs/logs 和 currentUserId。
- 输出：控制台统计展示。

[易错点]
- 统计模块只读全局容器，不修复数据、不保存文件。
- 统计口径默认排除 active=false 的卡片和错题；复习日志按 currentUserId 过滤。

[你以后可以改的地方]
- 可以新增统计口径或图表式文本展示，通常只需要增加一个 stat* 函数并接入 showStatsMenu。
- 可以调整分布区间，例如 mastery 档位或最近复习天数窗口。

[不建议随手改的地方]
- 不要在统计函数里修改 cards/wrongs/logs；维护和修复职责属于 maintenance.cpp。
- 不要把 active=false 纳入默认学习统计，否则回收站数据会污染结果。
*/

// ========== 统计功能 ==========
// 功能：统计当前用户有效卡片数量。
void statCardCount();
// 功能：统计当前用户有效错题数量。
void statWrongCount();
// 功能：按学科汇总卡片和错题分布。
void statSubjectDistribution();
// 功能：统计今天到期的复习材料数量。
void statDueTodayCount();
// 功能：按掌握度区间统计学习材料。
void statMasteryDistribution();
// 功能：统计最近一段时间的复习频率。
void statReviewFrequency();
// 功能：按错因类型统计错题分布。
void statErrorTypeDistribution();

// ========== 统计子菜单 ==========
// 功能：统计模块的交互入口，由 main.cpp 登录后主菜单调用。
void showStatsMenu();

#endif
