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
*/

// ========== 统计功能 ==========
void statCardCount();
void statWrongCount();
void statSubjectDistribution();
void statDueTodayCount();
void statMasteryDistribution();
void statReviewFrequency();
void statErrorTypeDistribution();

// ========== 统计子菜单 ==========
void showStatsMenu();

#endif
