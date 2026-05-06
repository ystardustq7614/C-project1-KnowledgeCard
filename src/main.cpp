#include <iostream>
#include <cstdlib>
#include <vector>
#include <string>

#include "globals.h"        // 全局变量声明（extern）
#include "storage.h"        // 数据存取（文件读写）
#include "user.h"           // 用户登录/注册/注销
#include "card.h"           // 知识卡片管理
#include "wrong.h"          // 错题管理
#include "review.h"         // 复习功能
#include "stats.h"          // 统计分析
#include "maintenance.h"    // 数据维护
#include "utils.h"          // 工具函数（字符串、日期、界面）
#include "algo_decay.h"     // 记忆衰减算法
#include "algo_recommend.h" // 弱项推荐算法
#include "practice.h"       // 自测练习

using namespace std;

/*
模块职责：
- 定义全局运行状态，处理命令行入口，组织欢迎菜单、登录后主菜单、登录后的衰减扫描和推荐展示。

关键约束：
- main.cpp 负责决定数据目录优先级：命令行 --data-dir > PROJECT1_DATA_DIR > 默认 data。
- 交互路径和 --check-data 非交互路径共用同一套加载逻辑，保证测试与实际运行的数据格式一致。
*/

// ========== 全局容器定义 ==========
vector<User> users;             // 所有用户
vector<Card> cards;             // 所有知识卡片
vector<WrongQuestion> wrongs;   // 所有错题
vector<ReviewLog> logs;         // 所有复习记录
int currentUserId = -1;         // -1 表示"未登录"
string currentUsername = "";    // 当前登录用户名，仅用于界面显示

// ========== 函数声明 ==========
void showWelcomeMenu();
void showMainMenu();

// 表示：命令行解析后的运行模式。
// 注意：valid=false 时 main 只打印错误和用法，不继续初始化数据文件。
struct CliOptions {
    bool help = false;      // 是否显示帮助
    bool checkData = false; // 是否做数据检查
    bool fix = false;       // 是否自动修复
    int userIdFilter = -1;  // 只检查某个用户，-1 表示全部
    string dataDir;         // 数据目录路径
    bool valid = true;      // 参数是否合法
    string errorMessage;    // 不合法时的错误信息
};

static void printCliUsage() {
    cout << "用法：\n";
    cout << "  project1.exe [--data-dir <path>]\n";//交互式运行
    cout << "  project1.exe --check-data [--fix] [--user-id <id>] [--data-dir <path>]\n";//非交互式数据检查
    cout << "  project1.exe --help\n\n";
    cout << "参数说明：\n";
    cout << "  --check-data      非交互执行数据一致性检查\n";
    cout << "  --fix             自动修复可修复的数据问题，需配合 --check-data\n";
    cout << "  --user-id <id>    只检查指定用户；不传则检查全部用户\n";
    cout << "  --data-dir <path> 指定本次运行使用的数据目录；默认读取 PROJECT1_DATA_DIR，未设置则使用 data\n";
    cout << "  --help            显示帮助\n\n";
    cout << "退出码：\n";
    cout << "  0  检查通过，或 --fix 后已无剩余问题\n";
    cout << "  1  检查发现问题，或仍有需人工处理的问题\n";
    cout << "  2  命令行参数错误\n";
}

static CliOptions parseCliOptions(int argc, char* argv[]) {
    CliOptions options;
    // CLI 支持空格和等号两种写法，便于脚本调用和人工调试共享同一入口。
    for (int i = 1; i < argc; ++i) {
        string arg = argv[i];
        if (arg == "--help" || arg == "-h") {
            options.help = true;
        } else if (arg == "--check-data") {
            options.checkData = true;
        } else if (arg == "--fix") {
            options.fix = true;
        } else if (arg == "--data-dir") {
            if (i + 1 >= argc) {
                options.valid = false;
                options.errorMessage = "--data-dir 需要跟一个数据目录路径";
                return options;
            }
            options.dataDir = trim(argv[++i]);  
            if (options.dataDir.empty()) {
                options.valid = false;
                options.errorMessage = "--data-dir 不能为空";
                return options;
            }
        } else if (arg.rfind("--data-dir=", 0) == 0) {
            options.dataDir = trim(arg.substr(11));
            if (options.dataDir.empty()) {
                options.valid = false;
                options.errorMessage = "--data-dir 不能为空";
                return options;
            }
        } else if (arg == "--user-id") {
            if (i + 1 >= argc) {
                options.valid = false;
                options.errorMessage = "--user-id 需要跟一个整数用户 ID";
                return options;
            }
            int userId;
            if (!parseInt(argv[++i], userId) || userId < 1) {
                options.valid = false;
                options.errorMessage = "--user-id 只能是正整数";
                return options;
            }
            options.userIdFilter = userId;
        } else if (arg.rfind("--user-id=", 0) == 0) {
            string value = arg.substr(10);
            int userId;
            if (!parseInt(value, userId) || userId < 1) {
                options.valid = false;
                options.errorMessage = "--user-id 只能是正整数";
                return options;
            }
            options.userIdFilter = userId;
        } else {
            options.valid = false;
            options.errorMessage = "未知参数：" + arg;
            return options;
        }
    }

    if (options.fix && !options.checkData) {
        // 自动修复必须依附检查报告，单独 --fix 没有明确修复范围。
        options.valid = false;
        options.errorMessage = "--fix 需要配合 --check-data 使用";
    }
    return options;
}

// ========== 主函数 ==========

int main(int argc, char* argv[]) {
    initTUI();
    CliOptions cli = parseCliOptions(argc, argv);
    if (!cli.valid) {
        cout << "[参数错误] " << cli.errorMessage << "\n\n";    
        printCliUsage();
        return 2;
    }
    if (cli.help) {
        printCliUsage();
        return 0;
    }

    initDataDirectoryFromEnv();
    if (!cli.dataDir.empty()) {
        setDataDirectory(cli.dataDir);
    }

    initFilesIfNeeded();
    loadAllData();

    if (cli.checkData) {
        if (cli.userIdFilter != -1 && findUserIndexById(cli.userIdFilter) == -1) {
            cout << "[参数错误] 不存在用户 ID：" << cli.userIdFilter << "\n";
            return 2;
        }
        return runDataConsistencyCli(cli.fix, cli.userIdFilter);
    }

    // 交互入口由菜单分支显式 exit 或 EOF 退出；循环外保存保留为未来改造时的兜底。
    while (true) {
        showWelcomeMenu();
    }

    saveAllData();
    return 0;
}

// ==========记忆衰减和智能推荐==========

// 登录成功后执行一次全局衰减扫描。
// 副作用：仅当存在衰减记录时保存 cards.txt/wrongs.txt，避免无变化时反复写盘。
void applyGlobalDecay() {
    string today = getTodayDate();
    int decayCount = 0;
    
    for (Card& c : cards) {
        if (c.userId != currentUserId || !c.active) continue;
        DecayResult res = calculateDecay(c.mastery, c.intervalDays, c.nextReviewDate, today);
        if (res.decayed) {
            c.mastery = res.newMastery;
            c.intervalDays = res.newInterval;
            decayCount++;
        }
    }
    
    for (WrongQuestion& w : wrongs) {
        if (w.userId != currentUserId || !w.active) continue;
        DecayResult res = calculateDecay(w.mastery, w.intervalDays, w.nextReviewDate, today);
        if (res.decayed) {
            w.mastery = res.newMastery;
            w.intervalDays = res.newInterval;
            decayCount++;
        }
    }
    
    if (decayCount > 0) {
        saveCards();
        saveWrongs();
        cout << ANSI_BOLD << ANSI_RED << "\n[系统提示] 检测到您有 " << decayCount << " 条记录存在记忆衰减，已自动扣除掌握度并缩短复习间隔。\n" << ANSI_RESET;
        cout << ANSI_YELLOW << "学习如逆水行舟，不进则退！快去今日复习看看吧！\n" << ANSI_RESET;
        pauseScreen();
    }
}

// 主菜单展示推荐只读内存数据，不写入日志；推荐结果是当前时刻快照。
void displayRecommendations() {
    vector<RecommendInputItem> inputs;
    for (const Card& c : cards) {
        if (c.userId != currentUserId || !c.active) continue;
        inputs.push_back({c.subject, c.chapter, c.mastery});
    }
    for (const WrongQuestion& w : wrongs) {
        if (w.userId != currentUserId || !w.active) continue;
        inputs.push_back({w.subject, w.chapter, w.mastery});
    }
    
    if (inputs.empty()) return;
    
    // Top 3 与 README 中“主菜单展示三个薄弱点”的产品口径保持一致。
    vector<RecommendResult> recs = calculateWeakestChapters(inputs, 3);
    if (!recs.empty()) {
        cout << ANSI_BOLD << ANSI_YELLOW << "[智能推荐] 发现您的薄弱知识点（建议优先复习）：\n" << ANSI_RESET;
        for (size_t i = 0; i < recs.size(); ++i) {
            cout << ANSI_RED << "  ★ Top " << (i+1) << ": " << recs[i].subject << " - " << recs[i].chapter 
                 << " (平均掌握度: " << recs[i].avgMastery << ", 包含 " << recs[i].itemCount << " 项)\n" << ANSI_RESET;
        }
        cout << ANSI_CYAN << "------------------------------\n" << ANSI_RESET;
    }
}

// ========== 一级菜单 ==========

void showWelcomeMenu() {
    clearScreen();
    cout << ANSI_CYAN << "==========================================\n" << ANSI_RESET;
    cout << ANSI_BOLD << ANSI_GREEN << "        知识卡片与错题复习管理系统\n" << ANSI_RESET;
    cout << ANSI_CYAN << "==========================================\n" << ANSI_RESET;
    cout << "1. 用户登录\n";
    cout << "2. 用户注册\n";
    cout << "0. 退出系统\n";
    cout << "请选择：";

    string line;
    if (!getline(cin, line)) {
        // 自动化脚本结束输入时会触发 EOF；退出前保存，确保已完成的交互步骤落盘。
        saveAllData();
        exit(0);
    }

    int choice;
    if (!parseInt(line, choice)) {
        cout << ANSI_RED << "输入无效，请重新输入。\n" << ANSI_RESET;
        pauseScreen();
        return;
    }

    switch (choice) {
        case 1:
            if (loginUser()) {
                // 衰减放在登录成功后执行，确保只影响当前用户的数据。
                applyGlobalDecay();
                showMainMenu();
            }
            break;
        case 2:
            registerUser();             // 注册
            break;
        case 0:
            saveAllData();
            cout << ANSI_YELLOW << "感谢使用，再见！\n" << ANSI_RESET;
            exit(0);                    // 直接退出进程
        default:                        // 输入了一个不在菜单里的数字
            cout << ANSI_RED << "菜单选项不存在。\n" << ANSI_RESET;
            pauseScreen();
    }
}

// ========== 登录后主菜单 ==========

void showMainMenu() {
    while (currentUserId != -1) {
        clearScreen();
        cout << ANSI_CYAN << "==========================================\n" << ANSI_RESET;
        cout << ANSI_BOLD << ANSI_GREEN << "    主菜单" << ANSI_RESET 
             << "（当前用户：" << ANSI_YELLOW << currentUsername << ANSI_RESET << "）\n";
        cout << ANSI_CYAN << "==========================================\n" << ANSI_RESET;
        
        displayRecommendations();
        
        cout << "1. 知识卡片管理\n";
        cout << "2. 错题管理\n";
        cout << "3. " << ANSI_BOLD << ANSI_BLUE << "今日复习\n" << ANSI_RESET;
        cout << "4. " << ANSI_MAGENTA << "自测练习中心\n" << ANSI_RESET;
        cout << "5. 统计分析\n";
        cout << "6. 数据维护\n";
        cout << "7. 修改密码\n";
        cout << "0. 退出登录\n";
        cout << "请选择：";

        string line;
        if (!getline(cin, line)) {
            saveAllData();
            exit(0);
        }
        int choice;
        if (!parseInt(line, choice)) {
            cout << ANSI_RED << "输入无效，请重新输入。\n" << ANSI_RESET;
            pauseScreen();
            continue;
        }

        switch (choice) {
            case 1:
                showCardMenu();
                break;
            case 2:
                showWrongMenu();
                break;
            case 3:
                showReviewMenu();
                break;
            case 4:
                showPracticeMenu();
                break;
            case 5:
                showStatsMenu();
                break;
            case 6:
                showMaintenanceMenu();
                break;
            case 7:
                changePassword();
                break;
            case 0:
                logoutUser();
                break;
            default:
                cout << "菜单选项不存在。\n";
                pauseScreen();
        }
    }
}
