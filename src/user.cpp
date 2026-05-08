// 引入用户模块头文件：注册、登录、改密、退出等函数声明都在这里。
#include "user.h"

// 引入全局状态：users 容器、currentUserId/currentUsername 登录态来自这里。
#include "globals.h"

// 引入存储层接口：getNextUserId 和 saveUsers 来自这里。
#include "storage.h"

// 引入通用工具：trim、getTodayDate、pauseScreen、clearScreen 等来自这里。
#include "utils.h"

// iostream 提供 cin/cout，用于控制台账号交互。
#include <iostream>

// 只引入本文件实际使用的标准库名字，避免命名空间整体引入污染。
using std::cin;
using std::cout;
using std::getline;
using std::string;

/*
[导读]
- 本文件负责用户身份流程：注册、登录、修改密码、退出登录。
- 登录成功后写入 currentUserId/currentUsername，后续业务模块都依赖这两个全局状态做数据隔离。

[输入输出]
- 输入：控制台用户名和密码。
- 输出：users 容器、users.txt，以及当前登录状态。

[简化说明]
- 这是教学/MVP 版本，密码明文保存，只适合理解本地账号流程，不适合真实账号系统。
- 本模块不做密码哈希、盐值、权限分级或登录尝试次数限制。

[易错点]
- 注册和改密后必须立即保存 users.txt，否则异常退出会丢失账号变更。
- currentUserId = -1 表示未登录；登录成功后必须设置为真实 userId。
- 用户名和密码禁止 |，因为 users.txt 使用 | 作为字段分隔符。
*/

// ========== 用户查找 ==========

bool usernameExists(const string& username) {
    // 遍历全局 users 容器。
    for (const User& u : users) {
        // 用户名完全相同就认为已存在；当前项目不做大小写折叠。
        if (u.username == username) return true;
    }

    // 没找到同名用户。
    return false;
}

int findUserIndexByName(const string& username) {
    // 返回的是 users 容器下标，不是 userId。
    for (size_t i = 0; i < users.size(); ++i) {
        // 用户名完全匹配时返回当前位置。
        if (users[i].username == username) return static_cast<int>(i);
    }

    // 找不到时返回 -1；调用方必须先判断，不能直接当下标使用。
    return -1;
}

int findUserIndexById(int userId) {
    // 返回的是 users 容器下标，不是 userId 本身。
    for (size_t i = 0; i < users.size(); ++i) {
        // userId 匹配时返回当前位置。
        if (users[i].userId == userId) return static_cast<int>(i);
    }

    // 找不到时返回 -1。
    return -1;
}

// ========== 用户注册 ==========

// [输入输出] 控制台输入 -> User 结构 -> users 容器和 users.txt。
bool registerUser() {
    // 进入注册页面前清屏。
    clearScreen();
    cout << "==============================\n";
    cout << "       用户注册\n";
    cout << "==============================\n";

    // 保存用户输入的用户名、密码和确认密码。
    string username, password, confirmPwd;

    // 读取用户名。
    cout << "请输入用户名：";
    getline(cin, username);

    // 用户名按短字段处理，去掉首尾空白。
    username = trim(username);

    // 用户名不能为空。
    if (username.empty()) {
        cout << "用户名不能为空。\n";
        pauseScreen();
        return false;
    }

    // users.txt 使用 | 分隔字段，用户名禁止包含 |。
    if (username.find('|') != string::npos) {
        cout << "用户名不允许包含 | 字符。\n";
        pauseScreen();
        return false;
    }

    // 用户名必须唯一。
    if (usernameExists(username)) {
        cout << "该用户名已被注册，请换一个。\n";
        pauseScreen();
        return false;
    }

    // 读取密码。
    cout << "请输入密码：";
    getline(cin, password);

    // 当前 MVP 按短字段处理密码，去掉首尾空白。
    password = trim(password);

    // 密码不能为空。
    if (password.empty()) {
        cout << "密码不能为空。\n";
        pauseScreen();
        return false;
    }

    // users.txt 使用 | 分隔字段，密码禁止包含 |。
    if (password.find('|') != string::npos) {
        cout << "密码不允许包含 | 字符。\n";
        pauseScreen();
        return false;
    }

    // 再次读取密码，用于确认用户没有输入错。
    cout << "请再次输入密码：";
    getline(cin, confirmPwd);
    confirmPwd = trim(confirmPwd);

    // 两次密码必须一致。
    if (password != confirmPwd) {
        cout << "两次密码不一致。\n";
        pauseScreen();
        return false;
    }

    // userId 使用当前最大值 +1，物理删除用户功能不存在，因此不会复用历史 ID。
    // 创建新用户对象。
    User u;

    // 分配全局唯一 userId。
    u.userId     = getNextUserId();

    // 写入用户名。
    u.username   = username;

    // 写入明文密码；教学 MVP 简化处理。
    u.password   = password;

    // 创建日期使用系统本地今天。
    u.createDate = getTodayDate();

    // 加入全局 users 容器。
    users.push_back(u);

    // 注册成功后立即保存 users.txt，避免异常退出丢失账号。
    saveUsers();

    // 输出注册成功提示。
    cout << "注册成功！用户名：" << username << "\n";
    pauseScreen();

    // 返回 true 表示注册完成。
    return true;
}

// ========== 用户登录 ==========

// [学习重点] 登录成功后只设置全局会话状态，不重新加载业务数据。
bool loginUser() {
    // 进入登录页面前清屏。
    clearScreen();
    cout << "==============================\n";
    cout << "       用户登录\n";
    cout << "==============================\n";

    // 保存用户输入的用户名和密码。
    string username, password;

    // 读取用户名。
    cout << "请输入用户名：";
    getline(cin, username);

    // 用户名按短字段处理，去掉首尾空白。
    username = trim(username);

    // 通过用户名查找 users 容器下标。
    int idx = findUserIndexByName(username);

    // 找不到用户时登录失败。
    if (idx == -1) {
        cout << "用户不存在。\n";
        pauseScreen();
        return false;
    }

    // 读取密码。
    cout << "请输入密码：";
    getline(cin, password);

    // 当前 MVP 按短字段处理密码，去掉首尾空白。
    password = trim(password);

    // 直接比较明文密码；真实系统不应这样做。
    if (users[idx].password != password) {
        cout << "密码错误。\n";
        pauseScreen();
        return false;
    }

    // 后续所有业务模块都依赖 currentUserId 做数据隔离，登录成功后必须同步两个全局状态。
    // currentUserId 用于业务过滤。
    currentUserId   = users[idx].userId;

    // currentUsername 主要用于界面显示。
    currentUsername  = users[idx].username;

    // 输出登录成功提示。
    cout << "登录成功！欢迎，" << currentUsername << "。\n";
    pauseScreen();

    // 返回 true 表示登录成功。
    return true;
}

// ========== 修改密码 ==========

bool changePassword() {
    // 进入修改密码页面前清屏。
    clearScreen();
    cout << "==============================\n";
    cout << "       修改密码\n";
    cout << "==============================\n";

    // 通过当前登录 userId 找到 users 容器下标。
    int idx = findUserIndexById(currentUserId);

    // 理论上已登录时一定能找到当前用户；找不到表示内存状态异常。
    if (idx == -1) {
        cout << "系统错误：找不到当前用户。\n";
        pauseScreen();
        return false;
    }

    // 保存旧密码、新密码和确认密码。
    string oldPwd, newPwd, confirmPwd;

    // 读取旧密码。
    cout << "请输入旧密码：";
    getline(cin, oldPwd);
    oldPwd = trim(oldPwd);

    // 旧密码必须匹配当前用户密码。
    if (users[idx].password != oldPwd) {
        cout << "旧密码错误。\n";
        pauseScreen();
        return false;
    }

    // 读取新密码。
    cout << "请输入新密码：";
    getline(cin, newPwd);
    newPwd = trim(newPwd);

    // 新密码不能为空。
    if (newPwd.empty()) {
        cout << "新密码不能为空。\n";
        pauseScreen();
        return false;
    }

    // users.txt 使用 | 分隔字段，新密码禁止包含 |。
    if (newPwd.find('|') != string::npos) {
        cout << "密码不允许包含 | 字符。\n";
        pauseScreen();
        return false;
    }

    // 再次读取新密码确认。
    cout << "请再次输入新密码：";
    getline(cin, confirmPwd);
    confirmPwd = trim(confirmPwd);

    // 两次新密码必须一致。
    if (newPwd != confirmPwd) {
        cout << "两次密码不一致。\n";
        pauseScreen();
        return false;
    }

    // 写回新密码。
    users[idx].password = newPwd;

    // 改密后立即保存 users.txt。
    saveUsers();

    // 输出成功提示。
    cout << "密码修改成功！\n";
    pauseScreen();

    // 返回 true 表示修改完成。
    return true;
}

// ========== 退出登录 ==========

void logoutUser() {
    // 先用当前用户名输出提示，再清空状态。
    cout << "已退出登录，再见，" << currentUsername << "。\n";

    // 退出登录只清空会话状态，不卸载内存数据；重新登录同一进程仍复用已加载容器。
    // -1 是未登录哨兵值，其他模块应据此阻止或过滤业务操作。
    currentUserId  = -1;

    // 清空显示用用户名。
    currentUsername = "";

    // 暂停让用户看到退出提示。
    pauseScreen();
}
