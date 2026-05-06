#ifndef MAINTENANCE_H
#define MAINTENANCE_H

/*
模块职责：
- 提供回收站查看/恢复/物理清理，以及数据一致性检查和可自动修复项处理。

关键约束：
- 恢复逻辑只把 active 改回 true，不重建被物理删除的数据。
- CLI 检查使用退出码表达结果：0=通过，1=发现问题，2=参数错误由 main.cpp 处理。
- 自动修复只处理可安全归正的问题；重复 ID 等需人工判断的问题只报告不自动改。
*/

// ========== 数据维护模块 ==========
void showDeletedCardsOfCurrentUser();
void showDeletedWrongsOfCurrentUser();
bool restoreCard();
bool restoreWrong();

// 交互模式：检查当前登录用户范围，并在用户确认后自动修复可修复项。
void runDataConsistencyCheck();

// 非交互模式：供测试脚本/命令行调用，userIdFilter=-1 表示全部用户。
int runDataConsistencyCli(bool fix, int userIdFilter);

// ========== 维护子菜单 ==========
void showMaintenanceMenu();

#endif
