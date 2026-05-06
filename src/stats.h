#ifndef STATS_H
#define STATS_H

/*
模块职责：
- 基于当前内存数据输出当前用户的卡片、错题、复习和错因统计。

关键约束：
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
