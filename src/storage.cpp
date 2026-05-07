#include "storage.h"
#include "globals.h"
#include "utils.h"
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <fstream>
#include <sstream>

using namespace std;
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

[实验]
- 在卡片正面输入包含 | 和换行的文本，再观察 cards.txt 中的 %7C/%0A 转义。
*/

// 数据文件路径（默认相对于程序运行目录，可由环境变量或 CLI 覆盖）
static string dataDirectory = "data";

void setDataDirectory(const string& directory) {
    // 空路径会让数据文件落到不可预期位置；这里选择忽略，由 CLI 参数校验负责报错。
    string normalized = trim(directory);
    if (!normalized.empty()) {
        dataDirectory = normalized;
    }
}

void initDataDirectoryFromEnv() {
    const char* envValue = getenv("PROJECT1_DATA_DIR");
    if (envValue != nullptr) {
        setDataDirectory(envValue);
    }
}

string getDataDirectory() {
    return dataDirectory;
}

static string dataFilePath(const string& fileName) {
    // 使用 fs::path 拼接，避免在 Windows/Linux 之间硬编码路径分隔符。
    return (fs::path(dataDirectory) / fileName).string();
}

static string userFile() { return dataFilePath("users.txt"); }
static string cardFile() { return dataFilePath("cards.txt"); }
static string wrongFile() { return dataFilePath("wrongs.txt"); }
static string logFile() { return dataFilePath("review_logs.txt"); }

// ========== 文件初始化 ==========

void initFilesIfNeeded() {
    fs::create_directories(fs::path(dataDirectory));

    // 四个空文件会让后续加载逻辑统一走“读空文件”路径，减少启动时的缺文件分支。
    const string files[] = { userFile(), cardFile(), wrongFile(), logFile() };
    for (const string& f : files) {
        ifstream test(f);
        if (!test.good()) {
            ofstream create(f);
            create.close();
        }
        test.close();
    }
}

// ========== 辅助：按 '|' 分割一行 ==========
static vector<string> splitLine(const string& line) {
    // 字段内容中的 | 会在保存前编码成 %7C，因此这里可以安全按原始分隔符切分。
    vector<string> parts;
    stringstream ss(line);
    string token;
    while (getline(ss, token, '|')) {
        parts.push_back(token);
    }
    return parts;
}

static int hexValue(char ch) {
    if (ch >= '0' && ch <= '9') return ch - '0';
    if (ch >= 'A' && ch <= 'F') return ch - 'A' + 10;
    if (ch >= 'a' && ch <= 'f') return ch - 'a' + 10;
    return -1;
}

// ========== 类 URL 编码的字段转义 ==========
std::string encodeStorageField(const string& value) {
    string encoded;
    for (char ch : value) {
        switch (ch) {
            case '%': encoded += "%25"; break;      // 先转义 %，避免写入后形成伪转义序列。
            case '|': encoded += "%7C"; break;
            case '\r': encoded += "%0D"; break;
            case '\n': encoded += "%0A"; break;
            default: encoded += ch; break;          // 其他字符原样保留
        }
    }
    return encoded;
}

// ========== 解码 ==========
std::string decodeStorageField(const string& value) {
    string decoded;
    for (size_t i = 0; i < value.size(); ++i) {
        if (value[i] == '%' && i + 2 < value.size()) {  // 确保后面还有两个字符
            int hi = hexValue(value[i + 1]);    // 高位十六进制值
            int lo = hexValue(value[i + 2]);    // 低位十六进制值
            if (hi != -1 && lo != -1) {
                char decodedChar = static_cast<char>(hi * 16 + lo);
                if (decodedChar == '%' || decodedChar == '|' ||
                    decodedChar == '\r' || decodedChar == '\n') {
                    decoded += decodedChar;
                    i += 2;
                    continue;
                }
            }
        }
        // 只解码本项目定义的危险字符；其他 %XX 原样保留以兼容旧数据和普通百分号文本。
        decoded += value[i];
    }
    return decoded;
}

// ========== 加载 ==========

// [输入输出] users.txt 的一行文本被转换为 User；异常短行直接跳过以兼容损坏/旧数据。
void loadUsers() {
    users.clear();                                  
    ifstream fin(userFile());                       
    if (!fin.is_open()) return;

    string line;
    while (getline(fin, line)) {
        line = trim(line);
        if (line.empty()) continue;

        vector<string> p = splitLine(line);
        if (p.size() < 4) continue;

        User u;
        u.userId    = stoi(p[0]); 
        u.username  = decodeStorageField(p[1]);
        u.password  = decodeStorageField(p[2]);
        u.createDate = decodeStorageField(p[3]);
        users.push_back(u);
    }
    fin.close();
}

// [输入输出] cards.txt 的字段顺序必须与 Card 结构和 README 数据模型保持一致。
void loadCards() {
    cards.clear();
    ifstream fin(cardFile());
    if (!fin.is_open()) return;

    string line;
    while (getline(fin, line)) {
        line = trim(line);
        if (line.empty()) continue;

        vector<string> p = splitLine(line);
        if (p.size() < 17) continue;

        Card c;
        c.cardId        = stoi(p[0]);
        c.userId        = stoi(p[1]);
        c.subject       = decodeStorageField(p[2]);
        c.chapter       = decodeStorageField(p[3]);
        c.title         = decodeStorageField(p[4]);
        c.front         = decodeStorageField(p[5]);
        c.back          = decodeStorageField(p[6]);
        c.tags          = decodeStorageField(p[7]);
        c.difficulty    = stoi(p[8]);
        c.mastery       = stoi(p[9]);
        c.reviewCount   = stoi(p[10]);
        c.correctStreak = stoi(p[11]);
        c.intervalDays  = stoi(p[12]);
        c.createDate    = decodeStorageField(p[13]);
        c.lastReviewDate = decodeStorageField(p[14]);
        c.nextReviewDate = decodeStorageField(p[15]);
        c.active        = (p[16] == "1");
        cards.push_back(c);
    }
    fin.close();
}

// [输入输出] wrongs.txt 同时兼容 v1 旧格式和 v1.1 后含 errorType 的新格式。
void loadWrongs() {
    wrongs.clear();
    ifstream fin(wrongFile());
    if (!fin.is_open()) return;

    string line;
    while (getline(fin, line)) {
        line = trim(line);
        if (line.empty()) continue;

        vector<string> p = splitLine(line);
        if (p.size() < 17) continue;

        WrongQuestion w;
        w.wrongId       = stoi(p[0]);
        w.userId        = stoi(p[1]);
        w.subject       = decodeStorageField(p[2]);
        w.chapter       = decodeStorageField(p[3]);
        w.question      = decodeStorageField(p[4]);
        w.correctAnswer = decodeStorageField(p[5]);
        w.wrongAnswer   = decodeStorageField(p[6]);
        w.reason        = decodeStorageField(p[7]);

        if (p.size() >= 18) {
            // V1.1 新格式：含 errorType
            w.errorType     = decodeStorageField(p[8]);
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
            w.errorType     = "";
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
        wrongs.push_back(w);
    }
    fin.close();
}

// [输入输出] review_logs.txt 只负责还原审计日志，不校验 itemId 当前是否仍存在。
void loadLogs() {
    logs.clear();
    ifstream fin(logFile());
    if (!fin.is_open()) return;

    string line;
    while (getline(fin, line)) {
        line = trim(line);
        if (line.empty()) continue;

        vector<string> p = splitLine(line);
        if (p.size() < 10) continue;

        ReviewLog lg;
        lg.logId       = stoi(p[0]);
        lg.userId      = stoi(p[1]);
        lg.itemId      = stoi(p[2]);
        lg.itemType    = decodeStorageField(p[3]);
        lg.reviewDate  = decodeStorageField(p[4]);
        lg.result      = stoi(p[5]);
        lg.oldInterval = stoi(p[6]);
        lg.newInterval = stoi(p[7]);
        lg.oldMastery  = stoi(p[8]);
        lg.newMastery  = stoi(p[9]);
        logs.push_back(lg);
    }
    fin.close();
}

// ========== 保存 ==========

// [输入输出] User -> users.txt；保存前编码用户名和密码，避免分隔符破坏字段切分。
void saveUsers() {
    string path = userFile();
    ofstream fout(path);
    if (!fout.is_open()) {
        cout << "[错误] 无法写入 " << path << endl;
        return;
    }
    for (const User& u : users) {
        fout << u.userId << "|"
             << encodeStorageField(u.username) << "|"
             << encodeStorageField(u.password) << "|"
             << encodeStorageField(u.createDate) << "\n";
    }
    fout.close();
}

// [输入输出] Card -> cards.txt；长文本字段必须先 encodeStorageField，再写入一行记录。
void saveCards() {
    string path = cardFile();
    ofstream fout(path);
    if (!fout.is_open()) {
        cout << "[错误] 无法写入 " << path << endl;
        return;
    }
    for (const Card& c : cards) {
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
    fout.close();
}

// [输入输出] WrongQuestion -> wrongs.txt；当前始终保存含 errorType 的新格式。
void saveWrongs() {
    string path = wrongFile();
    ofstream fout(path);
    if (!fout.is_open()) {
        cout << "[错误] 无法写入 " << path << endl;
        return;
    }
    for (const WrongQuestion& w : wrongs) {
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
    fout.close();
}

// [输入输出] ReviewLog -> review_logs.txt；日志保存当前结果，不重新计算复习状态。
void saveLogs() {
    string path = logFile();
    ofstream fout(path);
    if (!fout.is_open()) {
        cout << "[错误] 无法写入 " << path << endl;
        return;
    }
    for (const ReviewLog& lg : logs) {
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
    fout.close();
}

// ========== 整体加载 / 保存 ==========

// [导读] 启动时集中加载，保证所有业务模块看到的是同一批全局内存数据。
void loadAllData() {
    loadUsers();
    loadCards();
    loadWrongs();
    loadLogs();
}

// [导读] 退出或兜底保存时集中写回；多数业务操作仍会在修改后即时保存对应文件。
void saveAllData() {
    saveUsers();
    saveCards();
    saveWrongs();
    saveLogs();
}

// ========== 编号生成 ==========

// [易错点] ID 使用最大值 + 1，不复用删除记录的旧 ID，避免日志和关联字段混淆。
int getNextUserId() {
    int maxId = 0;
    for (const User& u : users) {
        if (u.userId > maxId) maxId = u.userId;
    }
    return maxId + 1;
}

int getNextCardId() {
    int maxId = 0;
    for (const Card& c : cards) {
        if (c.cardId > maxId) maxId = c.cardId;
    }
    return maxId + 1;
}

int getNextWrongId() {
    int maxId = 0;
    for (const WrongQuestion& w : wrongs) {
        if (w.wrongId > maxId) maxId = w.wrongId;
    }
    return maxId + 1;
}

int getNextLogId() {
    int maxId = 0;
    for (const ReviewLog& lg : logs) {
        if (lg.logId > maxId) maxId = lg.logId;
    }
    return maxId + 1;
}
