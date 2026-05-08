#ifndef GLOBALS_H
#define GLOBALS_H

// vector 用来保存所有用户、卡片、错题和复习日志的内存列表。
#include <vector>
// string 用来保存当前登录用户名。
#include <string>
// 全局容器里的元素类型都定义在 models.h。
#include "models.h"

// 头文件不整体引入 std 命名空间，避免把 std 的所有名字暴露给包含方。

/*
[导读]
- 本文件暴露进程内共享状态，是理解“模块之间如何交换数据”的关键入口。

[输入输出]
- 输入：storage.cpp 启动加载、业务模块运行时修改。
- 输出：所有模块通过 extern 看到同一份 users/cards/wrongs/logs 和当前登录用户。

[易错点]
- 这些变量的唯一定义在 main.cpp；其他模块只能通过 extern 共享同一份内存状态。
- 单机控制台程序没有并发写入保护，不适合多个进程同时操作同一数据目录。
*/

// 全局数据容器：启动时由 storage.cpp 加载，业务操作后按模块保存。
/*
[你以后可以改的地方]
- 如果以后要取消全局变量，可以从这里开始收口，把这些状态封装成 AppState 之类的结构体再传给各模块。
- 如果要新增一种长期保存的数据，也需要在这里声明对应容器，并在 main.cpp、storage.cpp 同步接入。

[不建议随手改的地方]
- 不要在 globals.h 里给变量赋初值，否则多个 .cpp 包含这个头文件时会出现重复定义。
- 不要绕过 currentUserId 过滤数据，否则会产生跨用户串数据的问题。
*/

// extern 表示“这里先声明有这个变量”，真正的内存空间在 main.cpp 中创建。
// users 保存所有本地账号；注册/登录模块主要操作它。
extern std::vector<User> users;
// cards 保存所有知识卡片；卡片、复习、统计、推荐模块都会读取它。
extern std::vector<Card> cards;
// wrongs 保存所有错题记录；错题、复习、统计、维护模块都会读取它。
extern std::vector<WrongQuestion> wrongs;
// logs 保存复习历史；复习模块追加，统计模块读取。
extern std::vector<ReviewLog> logs;

// 当前登录状态：currentUserId = -1 表示未登录，业务模块必须据此过滤数据。
// currentUserId 是真正的业务身份依据：列表、复习、统计都按它筛选数据。
extern int currentUserId;       // 业务判断用，-1 表示未登录
// currentUsername 主要用于菜单和提示文本；不要只靠它判断用户身份。
extern std::string currentUsername; // 仅用于界面显示

#endif
