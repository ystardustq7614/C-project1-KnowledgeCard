#ifndef USER_H
#define USER_H

// string 用于用户名、密码等账号输入。
#include <string>
// 头文件不整体引入 std 命名空间；接口中直接写 std::string。

/*
[导读]
- 本头文件声明本地用户流程：注册、登录、改密和退出登录。

[输入输出]
- 输入：控制台账号密码。
- 输出：users 容器、users.txt 和 currentUserId/currentUsername。

[易错点]
- 用户状态写入 globals.h 中的 currentUserId/currentUsername，其他模块通过该状态做数据隔离。
- 当前项目为教学 MVP，密码明文存储；不要将该模块直接用于真实账号系统。

[你以后可以改的地方]
- 可以增强用户名/密码校验规则，例如长度、字符范围、重复提示。
- 可以把明文密码升级为哈希存储，但要同步注册、登录、改密、fixture 和旧数据迁移。

[不建议随手改的地方]
- currentUserId=-1 的未登录语义；主菜单循环和各业务模块都依赖它。
- findUserIndex* 返回 users 下标的约定；调用方依赖 -1 表示未找到。
*/

// 用户查找
// 返回：找不到时返回 -1；调用方不得把 -1 当作 users 下标使用。
// 功能：只判断用户名是否存在，常用于注册前校验。
bool usernameExists(const std::string& username);
// 功能：按用户名查找 users 容器下标。
int findUserIndexByName(const std::string& username);
// 功能：按 userId 查找 users 容器下标。
int findUserIndexById(int userId);

// 用户操作
// 返回：true 表示操作完成并已持久化或已登录；false 表示用户取消/输入非法/校验失败。
// 功能：新增本地用户并保存 users.txt。
bool registerUser();
// 功能：校验账号密码，成功后写入 currentUserId/currentUsername。
bool loginUser();
// 功能：修改当前登录用户密码并保存 users.txt。
bool changePassword();
// 功能：清空当前登录状态，让主菜单回到未登录入口。
void logoutUser();

#endif
