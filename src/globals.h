#ifndef GLOBALS_H
#define GLOBALS_H

#include <vector>
#include <string>
#include "models.h"

using namespace std;

/*
模块职责：
- 暴露进程内共享的业务数据容器和当前登录状态。

关键约束：
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
