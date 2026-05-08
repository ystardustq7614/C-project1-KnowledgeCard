// iostream 提供 cin/cout，用于所有顶层菜单和 CLI 输出。
#include <iostream>

// cstdlib 提供 exit，用于菜单退出和 EOF 退出。
#include <cstdlib>

// vector 用于定义全局 users/cards/wrongs/logs 容器。
#include <vector>

// string 用于用户名、数据目录、命令行参数等文本。
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

// 只引入本文件实际使用的标准库名字，避免命名空间整体引入污染。
using std::cin;
using std::cout;
using std::exit;
using std::getline;
using std::string;
using std::vector;

/*
[导读]
- 本文件是程序总入口，建议在读完 models.h/globals.h 后阅读。
- 它把“启动 -> 数据加载 -> 登录/注册 -> 主菜单 -> 各业务模块”串成完整运行路径。

[对应流程图]
- 启动层：main()
- 入口菜单：showWelcomeMenu()
- 登录后业务分发：showMainMenu()
- 登录后自动处理：applyGlobalDecay() 与 displayRecommendations()

[输入输出]
- 输入：命令行参数、环境变量 PROJECT1_DATA_DIR、控制台菜单输入。
- 输出：加载后的全局容器状态、控制台页面、必要时写回 data/*.txt。

[易错点]
- 数据目录优先级固定为：--data-dir > PROJECT1_DATA_DIR > 默认 data。
- 交互路径和 --check-data 路径必须共用 loadAllData()，否则测试数据格式可能与真实运行脱节。

[实验]
- 改变 --data-dir 的传入目录，再运行 --check-data，观察程序是否完全切换到新的数据目录。
- 传入 --fix 但不传 --check-data，观察参数错误路径如何返回退出码 2。
*/

// ========== 全局容器定义 ==========
// 这些变量在 globals.h 中以 extern 声明，在 main.cpp 中真正定义。
// 所有业务模块共享同一份内存容器。
vector<User> users;             // 所有用户
vector<Card> cards;             // 所有知识卡片
vector<WrongQuestion> wrongs;   // 所有错题
vector<ReviewLog> logs;         // 所有复习记录

// 当前登录用户 ID；-1 是未登录哨兵值。
int currentUserId = -1;         // -1 表示"未登录"

// 当前登录用户名，主要用于界面显示。
string currentUsername = "";    // 当前登录用户名，仅用于界面显示

// ========== 函数声明 ==========
// 登录前入口菜单。
void showWelcomeMenu();

// 登录后主菜单。
void showMainMenu();

// 表示：命令行解析后的运行模式。
// 注意：valid=false 时 main 只打印错误和用法，不继续初始化数据文件。
struct CliOptions {
    // true 表示只显示帮助，不进入交互，也不执行检查。
    bool help = false;      // 是否显示帮助

    // true 表示执行非交互数据一致性检查。
    bool checkData = false; // 是否做数据检查

    // true 表示在检查后自动修复可修复问题。
    bool fix = false;       // 是否自动修复

    // 指定只检查某个用户；-1 表示全部用户。
    int userIdFilter = -1;  // 只检查某个用户，-1 表示全部

    // 本次运行指定的数据目录；空字符串表示不覆盖环境变量/默认目录。
    string dataDir;         // 数据目录路径

    // 参数是否合法；false 时 main 返回退出码 2。
    bool valid = true;      // 参数是否合法

    // 参数不合法时的人类可读错误信息。
    string errorMessage;    // 不合法时的错误信息
};

// [导读] 帮助文本既服务人工运行，也服务测试脚本校验 CLI 行为边界。
static void printCliUsage() {
    // 用法部分列出交互模式、检查模式和帮助模式。
    cout << "用法：\n";
    cout << "  project1.exe [--data-dir <path>]\n";//交互式运行
    cout << "  project1.exe --check-data [--fix] [--user-id <id>] [--data-dir <path>]\n";//非交互式数据检查
    cout << "  project1.exe --help\n\n";

    // 参数说明部分给人工用户和测试脚本提供稳定文本。
    cout << "参数说明：\n";
    cout << "  --check-data      非交互执行数据一致性检查\n";
    cout << "  --fix             自动修复可修复的数据问题，需配合 --check-data\n";
    cout << "  --user-id <id>    只检查指定用户；不传则检查全部用户\n";
    cout << "  --data-dir <path> 指定本次运行使用的数据目录；默认读取 PROJECT1_DATA_DIR，未设置则使用 data\n";
    cout << "  --help            显示帮助\n\n";

    // 退出码说明是 CLI 测试的重要契约。
    cout << "退出码：\n";
    cout << "  0  检查通过，或 --fix 后已无剩余问题\n";
    cout << "  1  检查发现问题，或仍有需人工处理的问题\n";
    cout << "  2  命令行参数错误\n";
}

// [输入输出] 输入 argv，输出结构化 CliOptions；这里不初始化文件，只做参数解释和合法性判断。
static CliOptions parseCliOptions(int argc, char* argv[]) {
    // 先创建默认选项：交互模式、默认数据目录、参数合法。
    CliOptions options;

    // CLI 支持空格和等号两种写法，便于脚本调用和人工调试共享同一入口。
    // i 从 1 开始，因为 argv[0] 是程序路径。
    for (int i = 1; i < argc; ++i) {
        // 当前正在处理的参数。
        string arg = argv[i];

        // --help/-h：只显示帮助。
        if (arg == "--help" || arg == "-h") {
            options.help = true;

        // --check-data：进入非交互数据一致性检查模式。
        } else if (arg == "--check-data") {
            options.checkData = true;

        // --fix：要求自动修复，但后面还会检查它必须和 --check-data 一起使用。
        } else if (arg == "--fix") {
            options.fix = true;

        // --data-dir <path>：空格写法。
        } else if (arg == "--data-dir") {
            // 必须还有下一个参数作为路径。
            if (i + 1 >= argc) {
                options.valid = false;
                options.errorMessage = "--data-dir 需要跟一个数据目录路径";
                return options;
            }

            // 读取下一个参数，并前进 i。
            options.dataDir = trim(argv[++i]);  

            // 空目录非法。
            if (options.dataDir.empty()) {
                options.valid = false;
                options.errorMessage = "--data-dir 不能为空";
                return options;
            }

        // --data-dir=<path>：等号写法。
        } else if (arg.rfind("--data-dir=", 0) == 0) {
            // 去掉前缀，得到路径值。
            options.dataDir = trim(arg.substr(11));

            // 空目录非法。
            if (options.dataDir.empty()) {
                options.valid = false;
                options.errorMessage = "--data-dir 不能为空";
                return options;
            }

        // --user-id <id>：空格写法。
        } else if (arg == "--user-id") {
            // 必须还有下一个参数作为用户 ID。
            if (i + 1 >= argc) {
                options.valid = false;
                options.errorMessage = "--user-id 需要跟一个整数用户 ID";
                return options;
            }

            // 解析正整数用户 ID。
            int userId;
            if (!parseInt(argv[++i], userId) || userId < 1) {
                options.valid = false;
                options.errorMessage = "--user-id 只能是正整数";
                return options;
            }

            // 保存过滤条件。
            options.userIdFilter = userId;

        // --user-id=<id>：等号写法。
        } else if (arg.rfind("--user-id=", 0) == 0) {
            // 去掉前缀，得到用户 ID 文本。
            string value = arg.substr(10);

            // 解析正整数用户 ID。
            int userId;
            if (!parseInt(value, userId) || userId < 1) {
                options.valid = false;
                options.errorMessage = "--user-id 只能是正整数";
                return options;
            }

            // 保存过滤条件。
            options.userIdFilter = userId;

        // 任何未识别参数都视为错误。
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

    // 返回解析结果。
    return options;
}

// ========== 主函数 ==========

int main(int argc, char* argv[]) {
    // 初始化控制台能力：Windows 下启用 UTF-8 和 ANSI 颜色。
    initTUI();

    // 先解析命令行参数；此时还不读写数据文件。
    CliOptions cli = parseCliOptions(argc, argv);

    // 参数错误时打印错误和用法，返回 CLI 参数错误码 2。
    if (!cli.valid) {
        cout << "[参数错误] " << cli.errorMessage << "\n\n";    
        printCliUsage();
        return 2;
    }

    // 帮助模式只打印帮助并成功退出。
    if (cli.help) {
        printCliUsage();
        return 0;
    }

    // 先从环境变量读取数据目录。
    initDataDirectoryFromEnv();

    // 再用 CLI --data-dir 覆盖环境变量，落实优先级：--data-dir > 环境变量 > 默认 data。
    if (!cli.dataDir.empty()) {
        setDataDirectory(cli.dataDir);
    }

    // 确保数据目录和四个数据文件存在。
    initFilesIfNeeded();

    // 所有运行模式都先加载同一套数据，保证 CLI 检查和交互运行使用相同解析逻辑。
    loadAllData();

    // 非交互数据检查模式。
    if (cli.checkData) {
        // 如果指定了用户 ID，先确认该用户存在；不存在返回参数错误码 2。
        if (cli.userIdFilter != -1 && findUserIndexById(cli.userIdFilter) == -1) {
            cout << "[参数错误] 不存在用户 ID：" << cli.userIdFilter << "\n";
            return 2;
        }

        // 执行 CLI 数据一致性检查，并直接返回它的退出码。
        return runDataConsistencyCli(cli.fix, cli.userIdFilter);
    }

    // 交互入口由菜单分支显式 exit 或 EOF 退出；循环外保存保留为未来改造时的兜底。
    while (true) {
        // 每轮显示登录/注册/退出入口菜单。
        showWelcomeMenu();
    }

    // 当前 while(true) 正常情况下不会走到这里，保留兜底保存。
    saveAllData();
    return 0;
}

// ==========记忆衰减和智能推荐==========

// 登录成功后执行一次全局衰减扫描。
// 副作用：仅当存在衰减记录时保存 cards.txt/wrongs.txt，避免无变化时反复写盘。
/*
[学习重点]
- 这是登录后的状态扫描：不新建记录，只根据逾期情况调整当前用户的掌握度和间隔。

[输入输出]
- 输入：当前用户的 active 卡片/错题、今天日期。
- 输出：被衰减记录的新 mastery/intervalDays；有变化时保存 cards.txt 和 wrongs.txt。

[易错点]
- 必须按 currentUserId 过滤，否则一个用户登录会影响其他用户数据。
*/
void applyGlobalDecay() {
    // 今天日期用于判断每条记录是否逾期。
    string today = getTodayDate();

    // 统计本次登录扫描中发生衰减的记录数。
    int decayCount = 0;
    
    // 第一段：扫描当前用户的有效卡片。
    for (Card& c : cards) {
        // 只处理当前用户 active=true 的卡片。
        if (c.userId != currentUserId || !c.active) continue;

        // 调用衰减算法，算法层只返回结果，不直接修改 Card。
        DecayResult res = calculateDecay(c.mastery, c.intervalDays, c.nextReviewDate, today);

        // decayed=true 表示已达到逾期阈值，需要写回新状态。
        if (res.decayed) {
            // 写回衰减后的掌握度。
            c.mastery = res.newMastery;

            // 写回缩短后的复习间隔。
            c.intervalDays = res.newInterval;

            // 衰减记录数加 1。
            decayCount++;
        }
    }
    
    // 第二段：扫描当前用户的有效错题。
    for (WrongQuestion& w : wrongs) {
        // 只处理当前用户 active=true 的错题。
        if (w.userId != currentUserId || !w.active) continue;

        // 错题复用同一套衰减算法。
        DecayResult res = calculateDecay(w.mastery, w.intervalDays, w.nextReviewDate, today);

        // decayed=true 表示已达到逾期阈值，需要写回新状态。
        if (res.decayed) {
            // 写回衰减后的掌握度。
            w.mastery = res.newMastery;

            // 写回缩短后的复习间隔。
            w.intervalDays = res.newInterval;

            // 衰减记录数加 1。
            decayCount++;
        }
    }
    
    // 只有确实发生衰减时才保存，避免每次登录无意义覆写文件。
    if (decayCount > 0) {
        // 保存卡片变化。
        saveCards();

        // 保存错题变化。
        saveWrongs();

        // 给用户明确提示：系统已经自动调整了复习状态。
        cout << ANSI_BOLD << ANSI_RED << "\n[系统提示] 检测到您有 " << decayCount << " 条记录存在记忆衰减，已自动扣除掌握度并缩短复习间隔。\n" << ANSI_RESET;
        cout << ANSI_YELLOW << "学习如逆水行舟，不进则退！快去今日复习看看吧！\n" << ANSI_RESET;
        pauseScreen();
    }
}

// 主菜单展示推荐只读内存数据，不写入日志；推荐结果是当前时刻快照。
/*
[学习重点]
- 推荐展示是只读快照，把卡片和错题统一成 RecommendInputItem 后交给算法层。

[输入输出]
- 输入：当前用户有效卡片/错题的 subject、chapter、mastery。
- 输出：控制台 Top 3 展示；不写日志，不修改任何掌握度。
*/
void displayRecommendations() {
    // 推荐算法输入只需要 subject、chapter、mastery 三个字段。
    vector<RecommendInputItem> inputs;

    // 第一段：把当前用户有效卡片转成推荐输入。
    for (const Card& c : cards) {
        // 跳过其他用户和逻辑删除卡片。
        if (c.userId != currentUserId || !c.active) continue;

        // 卡片参与薄弱章节聚合。
        inputs.push_back({c.subject, c.chapter, c.mastery});
    }

    // 第二段：把当前用户有效错题转成推荐输入。
    for (const WrongQuestion& w : wrongs) {
        // 跳过其他用户和逻辑删除错题。
        if (w.userId != currentUserId || !w.active) continue;

        // 错题也参与同一套薄弱章节聚合。
        inputs.push_back({w.subject, w.chapter, w.mastery});
    }
    
    // 没有学习材料时不展示推荐区域。
    if (inputs.empty()) return;
    
    // Top 3 与 README 中“主菜单展示三个薄弱点”的产品口径保持一致。
    vector<RecommendResult> recs = calculateWeakestChapters(inputs, 3);

    // 有推荐结果时展示 Top 3。
    if (!recs.empty()) {
        cout << ANSI_BOLD << ANSI_YELLOW << "[智能推荐] 发现您的薄弱知识点（建议优先复习）：\n" << ANSI_RESET;

        // 逐条输出推荐结果。
        for (size_t i = 0; i < recs.size(); ++i) {
            cout << ANSI_RED << "  ★ Top " << (i+1) << ": " << recs[i].subject << " - " << recs[i].chapter 
                 << " (平均掌握度: " << recs[i].avgMastery << ", 包含 " << recs[i].itemCount << " 项)\n" << ANSI_RESET;
        }

        // 分隔线让推荐区域和主菜单项分开。
        cout << ANSI_CYAN << "------------------------------\n" << ANSI_RESET;
    }
}

// ========== 一级菜单 ==========

// [导读] 一级菜单只负责“进入系统或退出”，登录后的业务流程交给 showMainMenu()。
void showWelcomeMenu() {
    // 登录前入口菜单，每次展示前清屏。
    clearScreen();

    // 输出系统标题。
    cout << ANSI_CYAN << "==========================================\n" << ANSI_RESET;
    cout << ANSI_BOLD << ANSI_GREEN << "        知识卡片与错题复习管理系统\n" << ANSI_RESET;
    cout << ANSI_CYAN << "==========================================\n" << ANSI_RESET;
    cout << "1. 用户登录\n";
    cout << "2. 用户注册\n";
    cout << "0. 退出系统\n";
    cout << "请选择：";

    // 读取菜单输入。
    string line;
    if (!getline(cin, line)) {
        // 自动化脚本结束输入时会触发 EOF；退出前保存，确保已完成的交互步骤落盘。
        saveAllData();
        exit(0);
    }

    // 菜单项必须是整数。
    int choice;
    if (!parseInt(line, choice)) {
        cout << ANSI_RED << "输入无效，请重新输入。\n" << ANSI_RESET;
        pauseScreen();
        return;
    }

    // 根据登录前菜单选择分发。
    switch (choice) {
        case 1:
            // 登录成功后进入登录后流程。
            if (loginUser()) {
                // 衰减放在登录成功后执行，确保只影响当前用户的数据。
                applyGlobalDecay();

                // 进入登录后主菜单；退出登录后会返回这里。
                showMainMenu();
            }
            break;
        case 2:
            // 注册新用户；注册后仍停留在欢迎菜单，需要用户再登录。
            registerUser();             // 注册
            break;
        case 0:
            // 正常退出前做一次兜底保存。
            saveAllData();
            cout << ANSI_YELLOW << "感谢使用，再见！\n" << ANSI_RESET;
            exit(0);                    // 直接退出进程
        default:                        // 输入了一个不在菜单里的数字
            cout << ANSI_RED << "菜单选项不存在。\n" << ANSI_RESET;
            pauseScreen();
    }
}

// ========== 登录后主菜单 ==========

// [导读] 登录后所有业务模块从这里分发；新增主功能时通常先在这里接菜单项。
void showMainMenu() {
    // 只要 currentUserId 不是 -1，就保持登录后主菜单循环。
    while (currentUserId != -1) {
        // 每轮主菜单前清屏。
        clearScreen();

        // 输出当前登录用户和主菜单标题。
        cout << ANSI_CYAN << "==========================================\n" << ANSI_RESET;
        cout << ANSI_BOLD << ANSI_GREEN << "    主菜单" << ANSI_RESET 
             << "（当前用户：" << ANSI_YELLOW << currentUsername << ANSI_RESET << "）\n";
        cout << ANSI_CYAN << "==========================================\n" << ANSI_RESET;
        
        // 在主菜单顶部展示当前薄弱章节推荐；这是只读快照。
        displayRecommendations();
        
        // 输出业务菜单项。
        cout << "1. 知识卡片管理\n";
        cout << "2. 错题管理\n";
        cout << "3. " << ANSI_BOLD << ANSI_BLUE << "今日复习\n" << ANSI_RESET;
        cout << "4. " << ANSI_MAGENTA << "自测练习中心\n" << ANSI_RESET;
        cout << "5. 统计分析\n";
        cout << "6. 数据维护\n";
        cout << "7. 修改密码\n";
        cout << "0. 退出登录\n";
        cout << "请选择：";

        // 读取菜单输入。
        string line;
        if (!getline(cin, line)) {
            // EOF 时保存所有数据并退出进程，适配自动化脚本结束输入的场景。
            saveAllData();
            exit(0);
        }

        // 菜单项必须是整数。
        int choice;
        if (!parseInt(line, choice)) {
            cout << ANSI_RED << "输入无效，请重新输入。\n" << ANSI_RESET;
            pauseScreen();
            continue;
        }

        // 根据登录后主菜单选择分发到各业务模块。
        switch (choice) {
            case 1:
                // 知识卡片管理。
                showCardMenu();
                break;
            case 2:
                // 错题管理。
                showWrongMenu();
                break;
            case 3:
                // 今日复习。
                showReviewMenu();
                break;
            case 4:
                // 自测练习中心。
                showPracticeMenu();
                break;
            case 5:
                // 统计分析。
                showStatsMenu();
                break;
            case 6:
                // 数据维护。
                showMaintenanceMenu();
                break;
            case 7:
                // 修改当前登录用户密码。
                changePassword();
                break;
            case 0:
                // 退出登录会把 currentUserId 置回 -1，从而结束 while 循环。
                logoutUser();
                break;
            default:
                // 其他整数不是合法菜单项。
                cout << "菜单选项不存在。\n";
                pauseScreen();
        }
    }
}
