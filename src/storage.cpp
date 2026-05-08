// 引入存储模块头文件：数据目录、加载、保存、编号生成等声明都在这里。
#include "storage.h"

// 引入全局容器：users、cards、wrongs、logs 都在这里定义。
#include "globals.h"

// 引入通用工具：trim 用于清理数据目录和读取行内容。
#include "utils.h"

// cstdlib 提供 getenv，用于读取 PROJECT1_DATA_DIR 环境变量。
#include <cstdlib>

// filesystem 提供跨平台路径拼接和目录创建能力。
#include <filesystem>

// iostream 提供错误输出。
#include <iostream>

// fstream 提供 ifstream/ofstream，用于读写文本文件。
#include <fstream>

// sstream 提供 stringstream，用于按 | 分割一行文本。
#include <sstream>

// 只引入本文件实际使用的标准库名字，避免命名空间整体引入污染。
using std::cout;
using std::endl;
using std::getenv;
using std::getline;
using std::ifstream;
using std::ofstream;
using std::stoi;
using std::string;
using std::stringstream;
using std::vector;
namespace fs = std::filesystem;

/*
[导读]
- 本文件是“内存容器 <-> data/*.txt”的唯一转换层。
- 先读 models.h 了解字段，再读本文件看字段如何落到文本文件。

[对应流程图]
- 加载阶段：data/*.txt -> splitLine/decode -> users/cards/wrongs/logs。
- 保存阶段：users/cards/wrongs/logs -> encode -> data/*.txt。

[输入输出]
- 输入：全局容器和 data 目录下的四个文本文件。
- 输出：更新后的全局容器，或覆写后的 users.txt/cards.txt/wrongs.txt/review_logs.txt。

[易错点]
- 字段顺序就是持久化契约，新增字段必须兼容旧数据和测试 fixture。
- 写入采用整文件覆写，适合单机教学项目，不支持多进程并发写同一数据目录。
- load* 函数遇到字段数量不足会跳过整行，但数值字段如果不是数字，stoi 仍可能抛异常。
- save* 函数会完整覆写对应文件，不是追加写；调用前应确保内存容器就是当前可信状态。

[实验]
- 在卡片正面输入包含 | 和换行的文本，再观察 cards.txt 中的 %7C/%0A 转义。
- 手动准备一份旧格式 wrongs.txt，观察 loadWrongs() 如何把缺失 errorType 的记录补为空字符串。
*/

// 数据文件路径（默认相对于程序运行目录，可由环境变量或 CLI 覆盖）
// 这是存储模块内部状态；业务模块不直接拼路径，而是通过这里的函数读写。
static string dataDirectory = "data";

void setDataDirectory(const string& directory) {
    // 空路径会让数据文件落到不可预期位置；这里选择忽略，由 CLI 参数校验负责报错。
    // trim 后再判断，避免 "   " 这种输入被当成有效目录。
    string normalized = trim(directory);

    // 只有非空目录才会覆盖当前数据目录。
    if (!normalized.empty()) {
        dataDirectory = normalized;
    }
}

void initDataDirectoryFromEnv() {
    // 读取环境变量 PROJECT1_DATA_DIR。
    const char* envValue = getenv("PROJECT1_DATA_DIR");

    // 环境变量存在时尝试设置数据目录。
    if (envValue != nullptr) {
        setDataDirectory(envValue);
    }
}

string getDataDirectory() {
    // 返回当前生效的数据目录，供 main.cpp 或测试脚本确认路径。
    return dataDirectory;
}

static string dataFilePath(const string& fileName) {
    // 使用 fs::path 拼接，避免在 Windows/Linux 之间硬编码路径分隔符。
    // 返回 string 是为了和 ifstream/ofstream 的旧接口保持简单兼容。
    return (fs::path(dataDirectory) / fileName).string();
}

// 用户数据文件路径。
static string userFile() { return dataFilePath("users.txt"); }

// 知识卡片数据文件路径。
static string cardFile() { return dataFilePath("cards.txt"); }

// 错题数据文件路径。
static string wrongFile() { return dataFilePath("wrongs.txt"); }

// 复习日志数据文件路径。
static string logFile() { return dataFilePath("review_logs.txt"); }

// ========== 文件初始化 ==========

void initFilesIfNeeded() {
    // 先确保数据目录存在；不存在则递归创建。
    fs::create_directories(fs::path(dataDirectory));

    // 四个空文件会让后续加载逻辑统一走“读空文件”路径，减少启动时的缺文件分支。
    const string files[] = { userFile(), cardFile(), wrongFile(), logFile() };

    // 逐个检查文件是否存在。
    for (const string& f : files) {
        // 尝试以输入流打开文件。
        ifstream test(f);

        // 打不开或状态不正常时，创建一个空文件。
        if (!test.good()) {
            ofstream create(f);
            create.close();
        }

        // 关闭测试输入流。
        test.close();
    }
}

// ========== 辅助：按 '|' 分割一行 ==========
static vector<string> splitLine(const string& line) {
    // 字段内容中的 | 会在保存前编码成 %7C，因此这里可以安全按原始分隔符切分。
    // 返回值中的字段仍是编码后的原始字段，调用方需要按需 decodeStorageField。
    vector<string> parts;

    // 用 stringstream 把一整行当成输入流。
    stringstream ss(line);

    // token 保存每次读出的字段。
    string token;

    // 按 | 分隔读取，直到行尾。
    while (getline(ss, token, '|')) {
        parts.push_back(token);
    }

    // 返回字段数组。
    return parts;
}

static int hexValue(char ch) {
    // 十六进制数字 0~9。
    if (ch >= '0' && ch <= '9') return ch - '0';

    // 十六进制大写 A~F。
    if (ch >= 'A' && ch <= 'F') return ch - 'A' + 10;

    // 十六进制小写 a~f。
    if (ch >= 'a' && ch <= 'f') return ch - 'a' + 10;

    // 不是合法十六进制字符。
    return -1;
}

// ========== 类 URL 编码的字段转义 ==========
std::string encodeStorageField(const string& value) {
    // 保存编码后的字段内容。
    string encoded;

    // 逐字符扫描输入字段。
    for (char ch : value) {
        // 只转义会破坏存储格式或转义语义的字符。
        switch (ch) {
            case '%': encoded += "%25"; break;      // 先转义 %，避免写入后形成伪转义序列。
            case '|': encoded += "%7C"; break;
            case '\r': encoded += "%0D"; break;
            case '\n': encoded += "%0A"; break;
            default: encoded += ch; break;          // 其他字符原样保留
        }
    }

    // 返回可安全写入单行管道分隔文件的字段。
    return encoded;
}

// ========== 解码 ==========
std::string decodeStorageField(const string& value) {
    // 保存解码后的字段内容。
    string decoded;

    // 逐字符扫描编码字段。
    for (size_t i = 0; i < value.size(); ++i) {
        // 只有形如 %XX 且后面还有两个字符时才尝试解码。
        if (value[i] == '%' && i + 2 < value.size()) {  // 确保后面还有两个字符
            int hi = hexValue(value[i + 1]);    // 高位十六进制值
            int lo = hexValue(value[i + 2]);    // 低位十六进制值

            // 两位都是合法十六进制时，才计算解码字符。
            if (hi != -1 && lo != -1) {
                char decodedChar = static_cast<char>(hi * 16 + lo);

                // 只解码本项目主动编码的危险字符。
                if (decodedChar == '%' || decodedChar == '|' ||
                    decodedChar == '\r' || decodedChar == '\n') {
                    decoded += decodedChar;

                    // 跳过已经消费的两个十六进制字符。
                    i += 2;
                    continue;
                }
            }
        }
        // 只解码本项目定义的危险字符；其他 %XX 原样保留以兼容旧数据和普通百分号文本。
        decoded += value[i];
    }

    // 返回解码结果。
    return decoded;
}

// ========== 加载 ==========

// [输入输出] users.txt 的一行文本被转换为 User；异常短行直接跳过以兼容损坏/旧数据。
void loadUsers() {
    // 加载前清空全局用户容器，避免重复加载叠加旧数据。
    users.clear();                                  

    // 打开 users.txt。
    ifstream fin(userFile());                       

    // 文件打不开时直接返回；initFilesIfNeeded 通常会保证文件存在。
    if (!fin.is_open()) return;

    // 逐行读取用户记录。
    string line;
    while (getline(fin, line)) {
        // 去掉首尾空白；空行跳过。
        line = trim(line);
        if (line.empty()) continue;

        // 按 | 拆字段。
        vector<string> p = splitLine(line);

        // 用户记录至少 4 个字段：userId、username、password、createDate。
        if (p.size() < 4) continue;

        // 组装 User 对象。
        User u;

        // 数值字段直接 stoi；如果内容不是数字，会抛异常并暴露数据损坏。
        u.userId    = stoi(p[0]); 

        // 文本字段需要解码，恢复 |、换行和 %。
        u.username  = decodeStorageField(p[1]);
        u.password  = decodeStorageField(p[2]);
        u.createDate = decodeStorageField(p[3]);

        // 加入全局 users 容器。
        users.push_back(u);
    }

    // 关闭文件。
    fin.close();
}

// [输入输出] cards.txt 的字段顺序必须与 Card 结构和 README 数据模型保持一致。
void loadCards() {
    // 加载前清空全局卡片容器。
    cards.clear();

    // 打开 cards.txt。
    ifstream fin(cardFile());

    // 文件打不开时直接返回。
    if (!fin.is_open()) return;

    // 逐行读取卡片记录。
    string line;
    while (getline(fin, line)) {
        // 去掉首尾空白；空行跳过。
        line = trim(line);
        if (line.empty()) continue;

        // 按 | 拆字段。
        vector<string> p = splitLine(line);

        // 当前 Card 持久化格式需要 17 个字段。
        if (p.size() < 17) continue;

        // 组装 Card 对象。
        Card c;

        // 前两个字段是 ID。
        c.cardId        = stoi(p[0]);
        c.userId        = stoi(p[1]);

        // 2~7 是文本字段，需要解码。
        c.subject       = decodeStorageField(p[2]);
        c.chapter       = decodeStorageField(p[3]);
        c.title         = decodeStorageField(p[4]);
        c.front         = decodeStorageField(p[5]);
        c.back          = decodeStorageField(p[6]);
        c.tags          = decodeStorageField(p[7]);

        // 8~12 是复习调度相关数值字段。
        c.difficulty    = stoi(p[8]);
        c.mastery       = stoi(p[9]);
        c.reviewCount   = stoi(p[10]);
        c.correctStreak = stoi(p[11]);
        c.intervalDays  = stoi(p[12]);

        // 13~15 是日期字符串，仍按文本字段解码。
        c.createDate    = decodeStorageField(p[13]);
        c.lastReviewDate = decodeStorageField(p[14]);
        c.nextReviewDate = decodeStorageField(p[15]);

        // active 使用 "1"/"0" 表示布尔值。
        c.active        = (p[16] == "1");

        // 加入全局 cards 容器。
        cards.push_back(c);
    }

    // 关闭文件。
    fin.close();
}

// [输入输出] wrongs.txt 同时兼容 v1 旧格式和 v1.1 后含 errorType 的新格式。
void loadWrongs() {
    // 加载前清空全局错题容器。
    wrongs.clear();

    // 打开 wrongs.txt。
    ifstream fin(wrongFile());

    // 文件打不开时直接返回。
    if (!fin.is_open()) return;

    // 逐行读取错题记录。
    string line;
    while (getline(fin, line)) {
        // 去掉首尾空白；空行跳过。
        line = trim(line);
        if (line.empty()) continue;

        // 按 | 拆字段。
        vector<string> p = splitLine(line);

        // v1 旧格式至少 17 个字段；更短的损坏行跳过。
        if (p.size() < 17) continue;

        // 组装 WrongQuestion 对象。
        WrongQuestion w;

        // 前两个字段是 ID。
        w.wrongId       = stoi(p[0]);
        w.userId        = stoi(p[1]);

        // 2~7 是基础文本字段。
        w.subject       = decodeStorageField(p[2]);
        w.chapter       = decodeStorageField(p[3]);
        w.question      = decodeStorageField(p[4]);
        w.correctAnswer = decodeStorageField(p[5]);
        w.wrongAnswer   = decodeStorageField(p[6]);
        w.reason        = decodeStorageField(p[7]);

        // v1.1 新格式多了 errorType，因此字段数 >= 18。
        if (p.size() >= 18) {
            // V1.1 新格式：含 errorType
            w.errorType     = decodeStorageField(p[8]);

            // 新格式中 linkedCardId 从 p[9] 开始。
            w.linkedCardId  = stoi(p[9]);
            w.mastery       = stoi(p[10]);
            w.reviewCount   = stoi(p[11]);
            w.correctStreak = stoi(p[12]);
            w.intervalDays  = stoi(p[13]);
            w.createDate    = decodeStorageField(p[14]);
            w.lastReviewDate = decodeStorageField(p[15]);
            w.nextReviewDate = decodeStorageField(p[16]);
            w.active        = (p[17] == "1");
        } else {
            // V1 旧格式：无 errorType，默认为空
            // 旧数据没有错因类型，加载时补为空字符串。
            w.errorType     = "";

            // 旧格式中 linkedCardId 从 p[8] 开始。
            w.linkedCardId  = stoi(p[8]);
            w.mastery       = stoi(p[9]);
            w.reviewCount   = stoi(p[10]);
            w.correctStreak = stoi(p[11]);
            w.intervalDays  = stoi(p[12]);
            w.createDate    = decodeStorageField(p[13]);
            w.lastReviewDate = decodeStorageField(p[14]);
            w.nextReviewDate = decodeStorageField(p[15]);
            w.active        = (p[16] == "1");
        }

        // 加入全局 wrongs 容器。
        wrongs.push_back(w);
    }

    // 关闭文件。
    fin.close();
}

// [输入输出] review_logs.txt 只负责还原审计日志，不校验 itemId 当前是否仍存在。
void loadLogs() {
    // 加载前清空全局日志容器。
    logs.clear();

    // 打开 review_logs.txt。
    ifstream fin(logFile());

    // 文件打不开时直接返回。
    if (!fin.is_open()) return;

    // 逐行读取复习日志。
    string line;
    while (getline(fin, line)) {
        // 去掉首尾空白；空行跳过。
        line = trim(line);
        if (line.empty()) continue;

        // 按 | 拆字段。
        vector<string> p = splitLine(line);

        // ReviewLog 当前格式需要 10 个字段。
        if (p.size() < 10) continue;

        // 组装 ReviewLog 对象。
        ReviewLog lg;

        // 0~2 是日志 ID、用户 ID、被复习对象 ID。
        lg.logId       = stoi(p[0]);
        lg.userId      = stoi(p[1]);
        lg.itemId      = stoi(p[2]);

        // itemType 和 reviewDate 是文本字段。
        lg.itemType    = decodeStorageField(p[3]);
        lg.reviewDate  = decodeStorageField(p[4]);

        // 后续字段是评分和新旧状态。
        lg.result      = stoi(p[5]);
        lg.oldInterval = stoi(p[6]);
        lg.newInterval = stoi(p[7]);
        lg.oldMastery  = stoi(p[8]);
        lg.newMastery  = stoi(p[9]);

        // 加入全局 logs 容器。
        logs.push_back(lg);
    }

    // 关闭文件。
    fin.close();
}

// ========== 保存 ==========

// [输入输出] User -> users.txt；保存前编码用户名和密码，避免分隔符破坏字段切分。
void saveUsers() {
    // 计算 users.txt 路径。
    string path = userFile();

    // 以输出模式打开文件；默认会截断旧内容，实现整文件覆写。
    ofstream fout(path);

    // 打不开时输出错误并返回。
    if (!fout.is_open()) {
        cout << "[错误] 无法写入 " << path << endl;
        return;
    }

    // 逐个用户写成一行。
    for (const User& u : users) {
        // 字段顺序：userId|username|password|createDate。
        fout << u.userId << "|"
             << encodeStorageField(u.username) << "|"
             << encodeStorageField(u.password) << "|"
             << encodeStorageField(u.createDate) << "\n";
    }

    // 关闭文件并刷新内容。
    fout.close();
}

// [输入输出] Card -> cards.txt；长文本字段必须先 encodeStorageField，再写入一行记录。
void saveCards() {
    // 计算 cards.txt 路径。
    string path = cardFile();

    // 以输出模式打开文件；默认会截断旧内容，实现整文件覆写。
    ofstream fout(path);

    // 打不开时输出错误并返回。
    if (!fout.is_open()) {
        cout << "[错误] 无法写入 " << path << endl;
        return;
    }

    // 逐张卡片写成一行。
    for (const Card& c : cards) {
        // 字段顺序必须和 loadCards 保持一致。
        fout << c.cardId << "|"
             << c.userId << "|"
             << encodeStorageField(c.subject) << "|"
             << encodeStorageField(c.chapter) << "|"
             << encodeStorageField(c.title) << "|"
             << encodeStorageField(c.front) << "|"
             << encodeStorageField(c.back) << "|"
             << encodeStorageField(c.tags) << "|"
             << c.difficulty << "|"
             << c.mastery << "|"
             << c.reviewCount << "|"
             << c.correctStreak << "|"
             << c.intervalDays << "|"
             << encodeStorageField(c.createDate) << "|"
             << encodeStorageField(c.lastReviewDate) << "|"
             << encodeStorageField(c.nextReviewDate) << "|"
             << (c.active ? "1" : "0") << "\n";
    }

    // 关闭文件并刷新内容。
    fout.close();
}

// [输入输出] WrongQuestion -> wrongs.txt；当前始终保存含 errorType 的新格式。
void saveWrongs() {
    // 计算 wrongs.txt 路径。
    string path = wrongFile();

    // 以输出模式打开文件；默认会截断旧内容，实现整文件覆写。
    ofstream fout(path);

    // 打不开时输出错误并返回。
    if (!fout.is_open()) {
        cout << "[错误] 无法写入 " << path << endl;
        return;
    }

    // 逐道错题写成一行。
    for (const WrongQuestion& w : wrongs) {
        // 当前保存时始终写 v1.1 新格式，包含 errorType。
        fout << w.wrongId << "|"
             << w.userId << "|"
             << encodeStorageField(w.subject) << "|"
             << encodeStorageField(w.chapter) << "|"
             << encodeStorageField(w.question) << "|"
             << encodeStorageField(w.correctAnswer) << "|"
             << encodeStorageField(w.wrongAnswer) << "|"
             << encodeStorageField(w.reason) << "|"
             << encodeStorageField(w.errorType) << "|"
             << w.linkedCardId << "|"
             << w.mastery << "|"
             << w.reviewCount << "|"
             << w.correctStreak << "|"
             << w.intervalDays << "|"
             << encodeStorageField(w.createDate) << "|"
             << encodeStorageField(w.lastReviewDate) << "|"
             << encodeStorageField(w.nextReviewDate) << "|"
             << (w.active ? "1" : "0") << "\n";
    }

    // 关闭文件并刷新内容。
    fout.close();
}

// [输入输出] ReviewLog -> review_logs.txt；日志保存当前结果，不重新计算复习状态。
void saveLogs() {
    // 计算 review_logs.txt 路径。
    string path = logFile();

    // 以输出模式打开文件；默认会截断旧内容，实现整文件覆写。
    ofstream fout(path);

    // 打不开时输出错误并返回。
    if (!fout.is_open()) {
        cout << "[错误] 无法写入 " << path << endl;
        return;
    }

    // 逐条日志写成一行。
    for (const ReviewLog& lg : logs) {
        // 字段顺序必须和 loadLogs 保持一致。
        fout << lg.logId << "|"
             << lg.userId << "|"
             << lg.itemId << "|"
             << encodeStorageField(lg.itemType) << "|"
             << encodeStorageField(lg.reviewDate) << "|"
             << lg.result << "|"
             << lg.oldInterval << "|"
             << lg.newInterval << "|"
             << lg.oldMastery << "|"
             << lg.newMastery << "\n";
    }

    // 关闭文件并刷新内容。
    fout.close();
}

// ========== 整体加载 / 保存 ==========

// [导读] 启动时集中加载，保证所有业务模块看到的是同一批全局内存数据。
void loadAllData() {
    // 加载用户数据。
    loadUsers();

    // 加载卡片数据。
    loadCards();

    // 加载错题数据。
    loadWrongs();

    // 加载复习日志。
    loadLogs();
}

// [导读] 退出或兜底保存时集中写回；多数业务操作仍会在修改后即时保存对应文件。
void saveAllData() {
    // 保存用户数据。
    saveUsers();

    // 保存卡片数据。
    saveCards();

    // 保存错题数据。
    saveWrongs();

    // 保存复习日志。
    saveLogs();
}

// ========== 编号生成 ==========

// [易错点] ID 使用最大值 + 1，不复用删除记录的旧 ID，避免日志和关联字段混淆。
int getNextUserId() {
    // 从 0 开始扫描最大 userId。
    int maxId = 0;

    // 遍历现有用户。
    for (const User& u : users) {
        // 找到更大的 ID 就更新 maxId。
        if (u.userId > maxId) maxId = u.userId;
    }

    // 新 ID = 当前最大值 + 1。
    return maxId + 1;
}

int getNextCardId() {
    // 从 0 开始扫描最大 cardId。
    int maxId = 0;

    // 遍历现有卡片，包括 active=false 的逻辑删除卡片。
    for (const Card& c : cards) {
        // 找到更大的 ID 就更新 maxId。
        if (c.cardId > maxId) maxId = c.cardId;
    }

    // 不复用删除卡片的旧 ID，避免 linkedCardId 和复习日志混淆。
    return maxId + 1;
}

int getNextWrongId() {
    // 从 0 开始扫描最大 wrongId。
    int maxId = 0;

    // 遍历现有错题，包括 active=false 的逻辑删除错题。
    for (const WrongQuestion& w : wrongs) {
        // 找到更大的 ID 就更新 maxId。
        if (w.wrongId > maxId) maxId = w.wrongId;
    }

    // 不复用删除错题的旧 ID，避免复习日志 itemId 混淆。
    return maxId + 1;
}

int getNextLogId() {
    // 从 0 开始扫描最大 logId。
    int maxId = 0;

    // 遍历现有复习日志。
    for (const ReviewLog& lg : logs) {
        // 找到更大的 ID 就更新 maxId。
        if (lg.logId > maxId) maxId = lg.logId;
    }

    // 新日志 ID = 当前最大值 + 1。
    return maxId + 1;
}
