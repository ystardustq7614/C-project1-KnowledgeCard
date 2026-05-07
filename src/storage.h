#ifndef STORAGE_H
#define STORAGE_H

#include <string>

/*
[导读]
- 本头文件声明项目的持久化边界：数据目录、文件初始化、加载、保存和自增编号。

[输入输出]
- 输入：data 目录下的文本文件，以及全局 users/cards/wrongs/logs 容器。
- 输出：加载后的内存容器，或保存后的 users.txt/cards.txt/wrongs.txt/review_logs.txt。

[易错点]
- 本模块不判断当前登录用户权限，也不做业务合法性修复；数据一致性修复由 maintenance.cpp 负责。
- 存储格式是一行一条记录，字段使用 | 分隔；字段顺序是兼容旧数据和测试 fixture 的契约。
- 数据目录优先级由 main.cpp 组织：--data-dir > PROJECT1_DATA_DIR > 默认 data。
*/

// 功能：对业务文本做百分号转义，保证字段内的 |、换行和 % 不破坏一行记录格式。
// 返回：可安全写入管道分隔文本文件的字段值。
std::string encodeStorageField(const std::string& value);

// 功能：只解码本项目定义的危险字符转义，未识别的 %XX 保持原样。
// 说明：保持未编码旧数据和普通百分号文本的兼容性。
std::string decodeStorageField(const std::string& value);

// 数据目录配置
// 参数：directory 为空时忽略，避免误把数据目录切到空路径。
void setDataDirectory(const std::string& directory);
void initDataDirectoryFromEnv();
std::string getDataDirectory();

// 文件初始化（不存在则自动创建）
void initFilesIfNeeded();

// 分模块加载
// 说明：加载函数会先清空对应全局容器；格式不完整的行会被跳过。
void loadUsers();
void loadCards();
void loadWrongs();
void loadLogs();

// 分模块保存
// 副作用：以当前内存容器完整覆写对应数据文件。
void saveUsers();
void saveCards();
void saveWrongs();
void saveLogs();

// 整体加载 / 整体保存
void loadAllData();
void saveAllData();

// 编号生成（扫描容器最大值 +1）
int getNextUserId();
int getNextCardId();
int getNextWrongId();
int getNextLogId();

#endif
