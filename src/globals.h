#ifndef GLOBALS_H
#define GLOBALS_H

#include <vector>
#include <string>
#include "models.h"

using namespace std;

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
extern vector<User> users;
extern vector<Card> cards;
extern vector<WrongQuestion> wrongs;
extern vector<ReviewLog> logs;

// 当前登录状态：currentUserId = -1 表示未登录，业务模块必须据此过滤数据。
extern int currentUserId;       // 业务判断用，-1 表示未登录
extern string currentUsername;   // 仅用于界面显示

#endif
