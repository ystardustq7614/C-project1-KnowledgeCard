#ifndef MODELS_H
#define MODELS_H

#include <string>
using namespace std;

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
struct User {
    int userId;             // 自增整数 ID（注册时分配）
    string username;        // 登录名
    string password;        // 明文密码
    string createDate;      // "YYYY-MM-DD" 格式的创建日期
};

/*
[学习重点]
- Card 是“知识卡片”在代码里的核心表示。
- 它既包含正反面内容，也包含复习调度所需的 mastery、intervalDays、nextReviewDate。
*/
struct Card {

    //身份与分类
    int cardId;             // 卡片唯一 ID
    int userId;             // 属于哪个用户（外键，关联到 User.userId）
    string subject;         // 科目
    string chapter;         // 章节
    string title;           // 卡片标题
    string tags;            // 标签（可选）

    //卡片内容（卡的正反面）
    string front;           // 正面内容（问题）
    string back;            // 背面内容（答案）
   
    //SM-2 间隔重复参数
    int difficulty;         // 难度等级
    int mastery;            // 掌握度（0-100 打分，SM-2/衰减算法的核心指标）
    int reviewCount;        // 被复习过的总次数
    int correctStreak;      // 连续正确次数
    int intervalDays;       // 当前复习间隔（天）
    string createDate;      // 创建日期
    string lastReviewDate;  // 上次复习日期
    string nextReviewDate;  // 下次复习预定日期

    bool active;            // 是否启用（false = 停用，相当于软删除————不想删记录但也不再使用，设为 false 即可）
};

/*
[学习重点]
- WrongQuestion 是错题记录，也可通过 linkedCardId 关联到“错题转卡片”生成的 Card。

[易错点]
- linkedCardId = -1 表示未关联；非 -1 也不一定有效，关联有效性由 maintenance.cpp 检查和修复。
*/
struct WrongQuestion {
    int wrongId;            // 错题唯一 ID
    int userId;             // 所属用户
    string subject;         // 科目
    string chapter;         // 章节
    string question;        // 题目内容
    string correctAnswer;   // 正确答案
    string wrongAnswer;     // 你当时写错的答案
    string reason;          // 错因分析（自由文本）
    string errorType;       // V1.1 新增：错因类型（概念不清/记忆错误/粗心/审题失误/计算错误/方法不会）
    int linkedCardId;       //  关联的知识卡片 ID（把错题和知识点连起来）。默认 -1，V1.1 正式启用

    // 错题复用与 Card 相同的复习调度字段，便于 review.cpp 用统一流程处理两类材料。
    int mastery;
    int reviewCount;
    int correctStreak;
    int intervalDays;
    string createDate;
    string lastReviewDate;
    string nextReviewDate;
    bool active;
};

// [导读] ReviewLog 表示一次复习动作的审计记录；日志只追加，不反向驱动卡片/错题当前状态。
struct ReviewLog {
    int logId;              // 日志唯一 ID
    int userId;             // 谁复习的
    int itemId;             // 复习的是哪个东西（卡片 ID 或 错题 ID）
    string itemType;        // "card" 或 "wrong"
    string reviewDate;      // 复习日期
    int result;             // 复习结果 0=不会  1=模糊  2=会
    int oldInterval;        // 复习前的间隔天数
    int newInterval;        // 复习后的间隔天数（SM-2 算出来的）
    int oldMastery;         // 复习前的掌握度
    int newMastery;         // 复习后的掌握度
};

// [输入输出] ReviewTask 是“卡片/错题 -> 今日复习列表”的中间表示，不保存到文件。
// [易错点] itemType + itemId 才能唯一定位对象，因为卡片和错题使用不同 ID 空间。
struct ReviewTask {
    int itemId;         // 卡片/错题的 ID
    string itemType;    // "card" 或 "wrong"
    string subject;     // 科目
    string title;       // 标题（显示用）
    string dueDate;     // 到期日期
    int priority;       // 优先级，数值越大越优先
};

#endif
