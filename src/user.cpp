#include "user.h"
#include "globals.h"
#include "storage.h"
#include "utils.h"
#include "tui.h"
#include <iostream>

using std::cout;
using std::cin;
using std::getline;
using std::string;

/*
模块职责：
- 管理本地用户身份流程，并把登录结果写入全局 currentUserId/currentUsername。

关键约束：
- 账号数据以教学项目最简形式保存，密码明文仅用于本地练习，不具备生产安全性。
- 注册/改密后立即保存 users.txt，避免异常退出丢失账号变更。
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

bool registerUser() {
    clearScreen();
    renderPageHeader("用户注册", "创建一个本地学习账号。");

    string username, password, confirmPwd;

    cout << "请输入用户名：";
    getline(cin, username);
    username = trim(username);
    if (username.empty()) {
        printTuiNotice(TuiNoticeLevel::Error, "用户名不能为空。");
        pauseScreen();
        return false;
    }
    if (username.find('|') != string::npos) {
        printTuiNotice(TuiNoticeLevel::Error, "用户名不允许包含 | 字符。");
        pauseScreen();
        return false;
    }
    if (usernameExists(username)) {
        printTuiNotice(TuiNoticeLevel::Warning, "该用户名已被注册，请换一个。");
        pauseScreen();
        return false;
    }

    cout << "请输入密码：";
    getline(cin, password);
    password = trim(password);
    if (password.empty()) {
        printTuiNotice(TuiNoticeLevel::Error, "密码不能为空。");
        pauseScreen();
        return false;
    }
    if (password.find('|') != string::npos) {
        printTuiNotice(TuiNoticeLevel::Error, "密码不允许包含 | 字符。");
        pauseScreen();
        return false;
    }

    cout << "请再次输入密码：";
    getline(cin, confirmPwd);
    confirmPwd = trim(confirmPwd);
    if (password != confirmPwd) {
        printTuiNotice(TuiNoticeLevel::Error, "两次密码不一致。");
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

    printTuiNotice(TuiNoticeLevel::Success, "注册成功，用户名：" + username);
    pauseScreen();
    return true;
}

// ========== 用户登录 ==========

bool loginUser() {
    clearScreen();
    renderPageHeader("用户登录", "登录后进入个人学习数据空间。");

    string username, password;

    cout << "请输入用户名：";
    getline(cin, username);
    username = trim(username);

    int idx = findUserIndexByName(username);
    if (idx == -1) {
        printTuiNotice(TuiNoticeLevel::Error, "用户不存在。");
        pauseScreen();
        return false;
    }

    cout << "请输入密码：";
    getline(cin, password);
    password = trim(password);

    if (users[idx].password != password) {
        printTuiNotice(TuiNoticeLevel::Error, "密码错误。");
        pauseScreen();
        return false;
    }

    // 后续所有业务模块都依赖 currentUserId 做数据隔离，登录成功后必须同步两个全局状态。
    currentUserId   = users[idx].userId;
    currentUsername  = users[idx].username;

    printTuiNotice(TuiNoticeLevel::Success, "登录成功，欢迎 " + currentUsername + "。");
    pauseScreen();
    return true;
}

// ========== 修改密码 ==========

bool changePassword() {
    clearScreen();
    renderPageHeader("修改密码", "更新当前账号的本地登录密码。");

    int idx = findUserIndexById(currentUserId);
    if (idx == -1) {
        printTuiNotice(TuiNoticeLevel::Error, "系统错误：找不到当前用户。");
        pauseScreen();
        return false;
    }

    string oldPwd, newPwd, confirmPwd;

    cout << "请输入旧密码：";
    getline(cin, oldPwd);
    oldPwd = trim(oldPwd);

    if (users[idx].password != oldPwd) {
        printTuiNotice(TuiNoticeLevel::Error, "旧密码错误。");
        pauseScreen();
        return false;
    }

    cout << "请输入新密码：";
    getline(cin, newPwd);
    newPwd = trim(newPwd);
    if (newPwd.empty()) {
        printTuiNotice(TuiNoticeLevel::Error, "新密码不能为空。");
        pauseScreen();
        return false;
    }
    if (newPwd.find('|') != string::npos) {
        printTuiNotice(TuiNoticeLevel::Error, "密码不允许包含 | 字符。");
        pauseScreen();
        return false;
    }

    cout << "请再次输入新密码：";
    getline(cin, confirmPwd);
    confirmPwd = trim(confirmPwd);
    if (newPwd != confirmPwd) {
        printTuiNotice(TuiNoticeLevel::Error, "两次密码不一致。");
        pauseScreen();
        return false;
    }

    users[idx].password = newPwd;
    saveUsers();

    printTuiNotice(TuiNoticeLevel::Success, "密码修改成功。");
    pauseScreen();
    return true;
}

// ========== 退出登录 ==========

void logoutUser() {
    printTuiNotice(TuiNoticeLevel::Info, "已退出登录，再见，" + currentUsername + "。");
    // 退出登录只清空会话状态，不卸载内存数据；重新登录同一进程仍复用已加载容器。
    currentUserId  = -1;
    currentUsername = "";
    pauseScreen();
}
