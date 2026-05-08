#ifndef MODELS_H
#define MODELS_H

#include <string>

/*
模块职责：
- 定义项目跨模块共享的数据模型，保持“数据结构”和“业务操作”分离。

不负责：
- 不在模型层做文件读写、输入校验、复习算法或菜单交互。

关键约束：
- 这些结构会被 storage.cpp 直接序列化到文本文件，字段顺序变化必须同步修改加载/保存逻辑和测试 fixture。
- 日期字段统一使用 YYYY-MM-DD 字符串，日期合法性和加减由 date_utils/utils 负责。
*/

// 表示：本地账号。
// 注意：当前是教学/MVP 项目，密码明文存储；不适合作为真实账号系统复用。
struct User {
    int userId;             // 用户内部 ID（注册时分配）
    std::string username;   // 登录名
    std::string password;   // 明文密码
    std::string createDate; // "YYYY-MM-DD" 格式的创建日期
};

// 表示：一个可复习的知识点卡片。
// 用途：同时支撑卡片管理、今日复习、薄弱点推荐、统计分析和数据维护。
struct Card {

    //身份与分类
    int cardId;             // 卡片内部 ID
    int userId;             // 所属用户内部 ID（关联到 User.userId）
    std::string subject;    // 科目
    std::string chapter;    // 章节
    std::string title;      // 卡片标题
    std::string tags;       // 标签（可选）

    //卡片内容（卡的正反面）
    std::string front;      // 正面内容（问题）
    std::string back;       // 背面内容（答案）
   
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

// 表示：一个独立错题记录，也可通过 linkedCardId 关联到由错题生成的知识卡片。
// 注意：linkedCardId = -1 表示未关联；关联有效性由 maintenance.cpp 检查和修复。
struct WrongQuestion {
    int wrongId;            // 错题内部 ID
    int userId;             // 所属用户内部 ID
    std::string subject;        // 科目
    std::string chapter;        // 章节
    std::string question;       // 题目内容
    std::string correctAnswer;  // 正确答案
    std::string wrongAnswer;    // 你当时写错的答案
    std::string reason;         // 错因分析（自由文本）
    std::string errorType;      // V1.1 新增：错因类型（概念不清/记忆错误/粗心/审题失误/计算错误/方法不会）
    int linkedCardId;       // 关联的知识卡片内部 ID。默认 -1，V1.1 正式启用

    // 错题复用与 Card 相同的复习调度字段，便于 review.cpp 用统一流程处理两类材料。
    int mastery;
    int reviewCount;
    int correctStreak;
    int intervalDays;
    std::string createDate;
    std::string lastReviewDate;
    std::string nextReviewDate;
    bool active;
};

// 表示：一次复习动作的审计记录。
// 注意：日志只追加，不反向驱动业务状态；卡片/错题的当前复习状态仍以各自记录为准。
struct ReviewLog {
    int logId;              // 日志内部 ID
    int userId;             // 复习者内部 ID
    int itemId;             // 被复习对象的内部 ID
    std::string itemType;   // "card" 或 "wrong"
    std::string reviewDate; // 复习日期
    int result;             // 复习结果 0=不会  1=模糊  2=会
    int oldInterval;        // 复习前的间隔天数
    int newInterval;        // 复习后的间隔天数（SM-2 算出来的）
    int oldMastery;         // 复习前的掌握度
    int newMastery;         // 复习后的掌握度
};

// 表示：今日复习列表中的运行时任务，不落盘。
// 说明：itemType + itemId 才能唯一定位对象，因为卡片和错题使用不同内部 ID 空间。
struct ReviewTask {
    int itemId;         // 卡片/错题的内部 ID
    std::string itemType; // "card" 或 "wrong"
    std::string subject;  // 科目
    std::string title;    // 标题（显示用）
    std::string dueDate;  // 到期日期
    int priority;       // 优先级，数值越大越优先
};

#endif
