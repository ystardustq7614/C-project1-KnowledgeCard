#ifndef USER_H
#define USER_H

#include <string>
using namespace std;

/*
[导读]
- 本头文件声明本地用户流程：注册、登录、改密和退出登录。

[输入输出]
- 输入：控制台账号密码。
- 输出：users 容器、users.txt 和 currentUserId/currentUsername。

[易错点]
- 用户状态写入 globals.h 中的 currentUserId/currentUsername，其他模块通过该状态做数据隔离。
- 当前项目为教学 MVP，密码明文存储；不要将该模块直接用于真实账号系统。
*/

// 用户查找
// 返回：找不到时返回 -1；调用方不得把 -1 当作 users 下标使用。
bool usernameExists(const string& username);
int findUserIndexByName(const string& username);
int findUserIndexById(int userId);

// 用户操作
// 返回：true 表示操作完成并已持久化或已登录；false 表示用户取消/输入非法/校验失败。
bool registerUser();
bool loginUser();
bool changePassword();
void logoutUser();

#endif
