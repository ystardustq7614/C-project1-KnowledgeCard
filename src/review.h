#ifndef REVIEW_H
#define REVIEW_H

#include <vector>
#include "models.h"

/*
模块职责：
- 生成今日待复习任务，驱动卡片/错题复习流程，并记录复习日志。

关键约束：
- ReviewTask 只在内存中使用，不落盘；持久化的是卡片/错题状态和 ReviewLog。
- 任务排序由 review.cpp 的优先级公式决定，调用方不应自行假设列表顺序。
- 复习状态更新会调用算法层纯函数，再由本模块负责保存数据。
*/

// ========== 今日复习任务 ==========
// 返回：当前用户所有 nextReviewDate <= 今天的有效卡片和错题，已按优先级排序。
vector<ReviewTask> generateTodayTasks();
void showTodayTasks();

// ========== 复习流程 ==========
// 副作用：更新被复习对象的掌握度、间隔、复习次数、下次复习日期，并追加 ReviewLog。
void startReviewSession();

// ========== 复习日志 ==========
// 参数：itemType 仅接受 "card" 或 "wrong"，调用方需保证 itemId 属于对应类型。
void addReviewLog(int itemId, const string& itemType, int result,
                  int oldInterval, int newInterval,
                  int oldMastery, int newMastery);
void showReviewHistory();

// ========== 复习子菜单 ==========
void showReviewMenu();

#endif
