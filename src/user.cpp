#include "user.h"
#include "globals.h"
#include "storage.h"
#include "utils.h"
#include <iostream>

using namespace std;

/*
[导读]
- 本文件负责用户身份流程：注册、登录、修改密码、退出登录。
- 登录成功后写入 currentUserId/currentUsername，后续业务模块都依赖这两个全局状态做数据隔离。

[输入输出]
- 输入：控制台用户名和密码。
- 输出：users 容器、users.txt，以及当前登录状态。

[简化说明]
- 这是教学/MVP 版本，密码明文保存，只适合理解本地账号流程，不适合真实账号系统。

[易错点]
- 注册和改密后必须立即保存 users.txt，否则异常退出会丢失账号变更。
*/

// ========== 用户查找 ==========

bool usernameExists(const string& username) {
    for (const User& u : users) {
        if (u.username == username) return true;
    }
    return false;
}

int findUserIndexByName(const string& username) {
    for (size_t i = 0; i < users.size(); ++i) {
        if (users[i].username == username) return static_cast<int>(i);
    }
    return -1;
}

int findUserIndexById(int userId) {
    for (size_t i = 0; i < users.size(); ++i) {
        if (users[i].userId == userId) return static_cast<int>(i);
    }
    return -1;
}

// ========== 用户注册 ==========

// [输入输出] 控制台输入 -> User 结构 -> users 容器和 users.txt。
bool registerUser() {
    clearScreen();
    cout << "==============================\n";
    cout << "       用户注册\n";
    cout << "==============================\n";

    string username, password, confirmPwd;

    cout << "请输入用户名：";
    getline(cin, username);
    username = trim(username);
    if (username.empty()) {
        cout << "用户名不能为空。\n";
        pauseScreen();
        return false;
    }
    if (username.find('|') != string::npos) {
        cout << "用户名不允许包含 | 字符。\n";
        pauseScreen();
        return false;
    }
    if (usernameExists(username)) {
        cout << "该用户名已被注册，请换一个。\n";
        pauseScreen();
        return false;
    }

    cout << "请输入密码：";
    getline(cin, password);
    password = trim(password);
    if (password.empty()) {
        cout << "密码不能为空。\n";
        pauseScreen();
        return false;
    }
    if (password.find('|') != string::npos) {
        cout << "密码不允许包含 | 字符。\n";
        pauseScreen();
        return false;
    }

    cout << "请再次输入密码：";
    getline(cin, confirmPwd);
    confirmPwd = trim(confirmPwd);
    if (password != confirmPwd) {
        cout << "两次密码不一致。\n";
        pauseScreen();
        return false;
    }

    // userId 使用当前最大值 +1，物理删除用户功能不存在，因此不会复用历史 ID。
    User u;
    u.userId     = getNextUserId();
    u.username   = username;
    u.password   = password;
    u.createDate = getTodayDate();
    users.push_back(u);

    saveUsers();

    cout << "注册成功！用户名：" << username << "\n";
    pauseScreen();
    return true;
}

// ========== 用户登录 ==========

// [学习重点] 登录成功后只设置全局会话状态，不重新加载业务数据。
bool loginUser() {
    clearScreen();
    cout << "==============================\n";
    cout << "       用户登录\n";
    cout << "==============================\n";

    string username, password;

    cout << "请输入用户名：";
    getline(cin, username);
    username = trim(username);

    int idx = findUserIndexByName(username);
    if (idx == -1) {
        cout << "用户不存在。\n";
        pauseScreen();
        return false;
    }

    cout << "请输入密码：";
    getline(cin, password);
    password = trim(password);

    if (users[idx].password != password) {
        cout << "密码错误。\n";
        pauseScreen();
        return false;
    }

    // 后续所有业务模块都依赖 currentUserId 做数据隔离，登录成功后必须同步两个全局状态。
    currentUserId   = users[idx].userId;
    currentUsername  = users[idx].username;

    cout << "登录成功！欢迎，" << currentUsername << "。\n";
    pauseScreen();
    return true;
}

// ========== 修改密码 ==========

bool changePassword() {
    clearScreen();
    cout << "==============================\n";
    cout << "       修改密码\n";
    cout << "==============================\n";

    int idx = findUserIndexById(currentUserId);
    if (idx == -1) {
        cout << "系统错误：找不到当前用户。\n";
        pauseScreen();
        return false;
    }

    string oldPwd, newPwd, confirmPwd;

    cout << "请输入旧密码：";
    getline(cin, oldPwd);
    oldPwd = trim(oldPwd);

    if (users[idx].password != oldPwd) {
        cout << "旧密码错误。\n";
        pauseScreen();
        return false;
    }

    cout << "请输入新密码：";
    getline(cin, newPwd);
    newPwd = trim(newPwd);
    if (newPwd.empty()) {
        cout << "新密码不能为空。\n";
        pauseScreen();
        return false;
    }
    if (newPwd.find('|') != string::npos) {
        cout << "密码不允许包含 | 字符。\n";
        pauseScreen();
        return false;
    }

    cout << "请再次输入新密码：";
    getline(cin, confirmPwd);
    confirmPwd = trim(confirmPwd);
    if (newPwd != confirmPwd) {
        cout << "两次密码不一致。\n";
        pauseScreen();
        return false;
    }

    users[idx].password = newPwd;
    saveUsers();

    cout << "密码修改成功！\n";
    pauseScreen();
    return true;
}

// ========== 退出登录 ==========

void logoutUser() {
    cout << "已退出登录，再见，" << currentUsername << "。\n";
    // 退出登录只清空会话状态，不卸载内存数据；重新登录同一进程仍复用已加载容器。
    currentUserId  = -1;
    currentUsername = "";
    pauseScreen();
}
