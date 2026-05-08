#ifndef STORAGE_H
#define STORAGE_H

// string 用于数据目录路径、字段转义和文件文本内容。
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

[你以后可以改的地方]
- 可以新增数据文件或字段转义规则，但必须同步加载、保存、fixture 和测试。
- 可以调整数据目录来源，但要保持 CLI 和 e2e 测试可指定隔离目录。

[不建议随手改的地方]
- 不要改变已有字段顺序；旧数据和测试 fixture 都按当前顺序解析。
- 不要在 load* 中做复杂业务修复；修复职责留给 maintenance.cpp，避免加载过程偷偷改数据。
*/

// 功能：对业务文本做百分号转义，保证字段内的 |、换行和 % 不破坏一行记录格式。
// 返回：可安全写入管道分隔文本文件的字段值。
std::string encodeStorageField(const std::string& value);

// 功能：只解码本项目定义的危险字符转义，未识别的 %XX 保持原样。
// 说明：保持未编码旧数据和普通百分号文本的兼容性。
std::string decodeStorageField(const std::string& value);

// 数据目录配置
// 参数：directory 为空时忽略，避免误把数据目录切到空路径。
// 说明：主要供 main.cpp 处理 --data-dir 参数时调用。
void setDataDirectory(const std::string& directory);
// 说明：从 PROJECT1_DATA_DIR 环境变量初始化数据目录。
void initDataDirectoryFromEnv();
// 返回：当前运行使用的数据目录。
std::string getDataDirectory();

// 文件初始化（不存在则自动创建）
// 功能：确保 users/cards/wrongs/review_logs 四个文本文件存在。
void initFilesIfNeeded();

// 分模块加载
// 说明：加载函数会先清空对应全局容器；格式不完整的行会被跳过。
// 功能：从 users.txt 加载 users 容器。
void loadUsers();
// 功能：从 cards.txt 加载 cards 容器。
void loadCards();
// 功能：从 wrongs.txt 加载 wrongs 容器。
void loadWrongs();
// 功能：从 review_logs.txt 加载 logs 容器。
void loadLogs();

// 分模块保存
// 副作用：以当前内存容器完整覆写对应数据文件。
// 功能：把 users 容器保存到 users.txt。
void saveUsers();
// 功能：把 cards 容器保存到 cards.txt。
void saveCards();
// 功能：把 wrongs 容器保存到 wrongs.txt。
void saveWrongs();
// 功能：把 logs 容器保存到 review_logs.txt。
void saveLogs();

// 整体加载 / 整体保存
// 功能：按固定顺序初始化文件并加载所有数据。
void loadAllData();
// 功能：按固定顺序保存所有全局容器。
void saveAllData();

// 编号生成（扫描容器最大值 +1）
// 返回：当前 users 中最大 userId + 1。
int getNextUserId();
// 返回：当前 cards 中最大 cardId + 1。
int getNextCardId();
// 返回：当前 wrongs 中最大 wrongId + 1。
int getNextWrongId();
// 返回：当前 logs 中最大 logId + 1。
int getNextLogId();

#endif
