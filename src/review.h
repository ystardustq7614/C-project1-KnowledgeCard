#ifndef REVIEW_H
#define REVIEW_H

// vector 用来返回今日复习任务列表。
#include <vector>
// string 用于 itemType 参数。
#include <string>
// models.h 提供 ReviewTask 结构。
#include "models.h"

/*
[导读]
- 本头文件声明今日复习模块的公开入口，是“学习材料 -> 复习任务 -> 状态更新 -> 日志”的闭环边界。

[输入输出]
- 输入：当前用户有效卡片/错题和控制台评分。
- 输出：更新后的卡片/错题状态、ReviewLog 和控制台复习历史。

[易错点]
- ReviewTask 只在内存中使用，不落盘；持久化的是卡片/错题状态和 ReviewLog。
- 任务排序由 review.cpp 的优先级公式决定，调用方不应自行假设列表顺序。
- 复习状态更新会调用算法层纯函数，再由本模块负责保存数据。

[你以后可以改的地方]
- 可以调整 generateTodayTasks 的优先级公式，让逾期、低掌握度或错题权重更高。
- 可以调整 startReviewSession 的交互提示，但要同步 e2e 输入序列。

[不建议随手改的地方]
- ReviewTask 不落盘的约定；真正持久化的是 cards/wrongs/logs。
- addReviewLog 的 itemType 只能是 "card" 或 "wrong"，否则历史记录无法准确解释。
*/

// ========== 今日复习任务 ==========
// 返回：当前用户所有 nextReviewDate <= 今天的有效卡片和错题，已按优先级排序。
std::vector<ReviewTask> generateTodayTasks();
// 功能：只展示今日任务，不修改复习状态。
void showTodayTasks();

// ========== 复习流程 ==========
// 副作用：更新被复习对象的掌握度、间隔、复习次数、下次复习日期，并追加 ReviewLog。
void startReviewSession();

// ========== 复习日志 ==========
// 参数：itemType 仅接受 "card" 或 "wrong"，调用方需保证 itemId 属于对应类型。
// 说明：old/new 参数由复习流程在状态更新前后捕获，用于形成可追溯历史。
void addReviewLog(int itemId, const std::string& itemType, int result,
                  int oldInterval, int newInterval,
                  int oldMastery, int newMastery);
// 功能：展示当前用户的复习历史记录。
void showReviewHistory();

// ========== 复习子菜单 ==========
// 功能：复习模块的交互入口，由 main.cpp 登录后主菜单调用。
void showReviewMenu();

#endif
