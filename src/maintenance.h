#ifndef MAINTENANCE_H
#define MAINTENANCE_H

/*
[导读]
- 本头文件声明数据维护模块，覆盖回收站、恢复、物理清理和数据一致性检查。

[输入输出]
- 输入：当前用户或 CLI 传入的 userIdFilter，以及 cards/wrongs/users 容器。
- 输出：控制台报告、修复后的数据文件和 CLI 退出码。

[易错点]
- 恢复逻辑只把 active 改回 true，不重建被物理删除的数据。
- CLI 检查使用退出码表达结果：0=通过，1=发现问题，2=参数错误由 main.cpp 处理。
- 自动修复只处理可安全归正的问题；重复 ID 等需人工判断的问题只报告不自动改。

[你以后可以改的地方]
- 可以新增数据检查项，例如重复标题、空字段、孤立日志等。
- 可以扩展自动修复策略，但只应修复确定不会丢失用户意图的问题。

[不建议随手改的地方]
- CLI 退出码语义；tools/run_cli_checks.ps1 和自动化测试依赖它。
- 回收站的 active=false 语义；卡片、错题、复习、统计模块都按它过滤。
*/

// ========== 数据维护模块 ==========
// 功能：展示当前用户 active=false 的卡片。
void showDeletedCardsOfCurrentUser();
// 功能：展示当前用户 active=false 的错题。
void showDeletedWrongsOfCurrentUser();
// 功能：把选中的已删除卡片恢复为 active=true。
bool restoreCard();
// 功能：把选中的已删除错题恢复为 active=true。
bool restoreWrong();

// 交互模式：检查当前登录用户范围，并在用户确认后自动修复可修复项。
void runDataConsistencyCheck();

// 非交互模式：供测试脚本/命令行调用，userIdFilter=-1 表示全部用户。
// 返回：0=检查通过，1=发现问题或修复后仍有问题，2=参数错误通常由 main.cpp 先处理。
int runDataConsistencyCli(bool fix, int userIdFilter);

// ========== 维护子菜单 ==========
// 功能：数据维护模块的交互入口，由 main.cpp 登录后主菜单调用。
void showMaintenanceMenu();

#endif
