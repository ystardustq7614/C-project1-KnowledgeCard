#ifndef MODELS_H
#define MODELS_H

// string 是这些模型里最常用的字段类型，用来保存用户名、题目、日期等文本。
#include <string>

// 生产代码中头文件不应整体引入 std 命名空间；这里直接写 std::string，避免污染所有包含方。

/*
[导读]
- 本文件是全项目的数据字典，建议作为源码阅读第一站。
- 这里只定义“数据长什么样”，不处理菜单、文件、算法和输入校验。

[对应流程图]
- User：账号与登录状态的来源。
- Card / WrongQuestion：学习材料主体，流向卡片管理、错题管理、复习、推荐和统计。
- ReviewLog：复习动作审计记录，流向统计与历史查看。
- ReviewTask：今日复习的运行时中间表示，不落盘。

[输入输出]
- 输入：业务模块创建或修改这些结构体。
- 输出：storage.cpp 按字段顺序把结构体保存到 data/*.txt。

[易错点]
- 字段顺序是持久化契约。新增、删除或调整字段时，必须同步 storage.cpp、fixture 和 README 的数据格式说明。
- 日期统一使用 YYYY-MM-DD 字符串；日期计算不要在模型层完成。
*/

// [简化说明] 本地账号只为教学闭环服务，密码明文存储，不适合作为真实账号系统复用。
/*
[你以后可以优先改的地方]
- subject/chapter/tags/errorType 这类分类字段，通常可以安全扩展取值或显示文案。
- difficulty/mastery/intervalDays 这类算法字段可以调规则，但要同步算法、测试和说明。

[不建议随手改的地方]
- 各结构体字段顺序：storage.cpp 按顺序读写，顺序一变旧数据就可能读错。
- userId/cardId/wrongId/logId：这些是关联关系的主键或外键，改动会牵动多个模块。
- active：这是软删除标记，很多列表、复习和统计都依赖 active=true 过滤。
*/

struct User {
    // userId 是用户主键；其他数据通过 userId 判断属于哪个用户。
    int userId;             // 自增整数 ID（注册时分配）
    // username 用于登录查找；当前项目没有做大小写归一化。
    std::string username;   // 登录名
    // password 当前是明文保存；如果以后做真实项目，应改为哈希存储并同步登录逻辑。
    std::string password;   // 明文密码
    // createDate 只记录创建日期，不记录具体时分秒。
    std::string createDate; // "YYYY-MM-DD" 格式的创建日期
};

/*
[学习重点]
- Card 是“知识卡片”在代码里的核心表示。
- 它既包含正反面内容，也包含复习调度所需的 mastery、intervalDays、nextReviewDate。
*/
struct Card {

    // 这一组字段用于识别、归属和分类；新增筛选维度时通常从这里扩展。
    //身份与分类
    int cardId;             // 卡片唯一 ID
    int userId;             // 属于哪个用户（外键，关联到 User.userId）
    std::string subject;    // 科目
    std::string chapter;    // 章节
    std::string title;      // 卡片标题
    std::string tags;       // 标签（可选）

    //卡片内容（卡的正反面）
    std::string front;      // 正面内容（问题）
    std::string back;       // 背面内容（答案）
   
    // 下面这组字段由复习流程和算法共同维护，手动改动会直接影响推荐和到期判断。
    //SM-2 间隔重复参数
    int difficulty;         // 难度等级
    int mastery;            // 掌握度（0-100 打分，SM-2/衰减算法的核心指标）
    int reviewCount;        // 被复习过的总次数
    int correctStreak;      // 连续正确次数
    int intervalDays;       // 当前复习间隔（天）
    std::string createDate;     // 创建日期
    std::string lastReviewDate; // 上次复习日期
    std::string nextReviewDate; // 下次复习预定日期

    bool active;            // 是否启用（false = 停用，相当于软删除————不想删记录但也不再使用，设为 false 即可）
};

/*
[学习重点]
- WrongQuestion 是错题记录，也可通过 linkedCardId 关联到“错题转卡片”生成的 Card。

[易错点]
- linkedCardId = -1 表示未关联；非 -1 也不一定有效，关联有效性由 maintenance.cpp 检查和修复。
*/
struct WrongQuestion {
    // wrongId 是错题主键；linkedCardId 关联的是 Card.cardId，不是 cards 的下标。
    int wrongId;            // 错题唯一 ID
    // userId 决定错题归属，所有错题列表都要按当前登录用户过滤。
    int userId;             // 所属用户
    // subject/chapter 用于分类、筛选、统计；question/answer/reason 是错题本主体内容。
    std::string subject;        // 科目
    std::string chapter;        // 章节
    std::string question;       // 题目内容
    std::string correctAnswer;  // 正确答案
    std::string wrongAnswer;    // 你当时写错的答案
    std::string reason;         // 错因分析（自由文本）
    std::string errorType;      // V1.1 新增：错因类型（概念不清/记忆错误/粗心/审题失误/计算错误/方法不会）
    int linkedCardId;       //  关联的知识卡片 ID（把错题和知识点连起来）。默认 -1，V1.1 正式启用

    // 错题复用与 Card 相同的复习调度字段，便于 review.cpp 用统一流程处理两类材料。
    // 这些字段含义与 Card 中同名字段一致；以后改复习算法时两边要一起考虑。
    int mastery;
    int reviewCount;
    int correctStreak;
    int intervalDays;
    std::string createDate;
    std::string lastReviewDate;
    std::string nextReviewDate;
    bool active;
};

// [导读] ReviewLog 表示一次复习动作的审计记录；日志只追加，不反向驱动卡片/错题当前状态。
struct ReviewLog {
    // logId 是日志主键，通常只新增不修改。
    int logId;              // 日志唯一 ID
    // userId + itemType + itemId 可以定位“谁复习了哪类材料里的哪一项”。
    int userId;             // 谁复习的
    int itemId;             // 复习的是哪个东西（卡片 ID 或 错题 ID）
    std::string itemType;   // "card" 或 "wrong"
    std::string reviewDate; // 复习日期
    // 当前 review.cpp 使用 1=忘记、2=模糊、3=记牢；测试脚本也按这个范围输入。
    int result;             // 复习结果 1=忘记  2=模糊  3=记牢
    // old/new 成对保存，用来在历史记录里看出一次复习前后发生了什么变化。
    int oldInterval;        // 复习前的间隔天数
    int newInterval;        // 复习后的间隔天数（SM-2 算出来的）
    int oldMastery;         // 复习前的掌握度
    int newMastery;         // 复习后的掌握度
};

// [输入输出] ReviewTask 是“卡片/错题 -> 今日复习列表”的中间表示，不保存到文件。
// [易错点] itemType + itemId 才能唯一定位对象，因为卡片和错题使用不同 ID 空间。
struct ReviewTask {
    // itemId 的含义由 itemType 决定：itemType="card" 时对应 Card.cardId，"wrong" 时对应 WrongQuestion.wrongId。
    int itemId;         // 卡片/错题的 ID
    std::string itemType; // "card" 或 "wrong"
    // subject/title/dueDate/priority 主要为复习列表展示和排序服务，不是原始数据的完整副本。
    std::string subject; // 科目
    std::string title;   // 标题（显示用）
    std::string dueDate; // 到期日期
    int priority;       // 优先级，数值越大越优先
};

#endif
