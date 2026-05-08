#ifndef ALGO_RECOMMEND_H
#define ALGO_RECOMMEND_H

// string 保存学科和章节名称。
#include <string>
// vector 保存输入材料列表和 Top N 输出列表。
#include <vector>

/*
[导读]
- 本头文件声明薄弱章节推荐算法，主菜单推荐和专项练习都会使用它。

[输入输出]
- 输入：业务层整理出的 subject/chapter/mastery 列表。
- 输出：平均掌握度最低的 Top N 章节。

[易错点]
- 输入应由业务层提前过滤到当前用户、active=true 的数据；本算法不读取登录状态。
- 空学科或空章节会被忽略，避免推荐结果出现无法定位的分类。
*/

// ================================================================
// 纯数据结构：薄弱点推荐
// ================================================================

struct RecommendInputItem {
    // subject/chapter 共同组成推荐分组键；任一为空都会被实现层跳过。
    std::string subject;
    std::string chapter;
    // mastery 是业务层摘录出来的掌握度，算法只聚合不校正范围。
    int mastery;
};

struct RecommendResult {
    // 推荐结果对应的学科。
    std::string subject;
    // 推荐结果对应的章节。
    std::string chapter;
    // 该章节所有输入项的平均掌握度，越低越薄弱。
    int avgMastery;
    // 参与平均值计算的卡片/错题数量，用于判断样本规模。
    int itemCount;
};

// ================================================================
// 纯函数算法：计算 Top N 薄弱章节
// ================================================================
// 传入用户的卡片/错题摘录，聚合计算每个（学科+章节）的平均掌握度。
// 返回平均掌握度最低的 Top N 个章节。
// ================================================================
// 排序：平均掌握度低者优先；同分时 itemCount 多者优先，避免小样本章节抢占推荐。
// 你以后可改：排序权重、是否合并大小写、是否把错题权重设得更高。
// 不建议随手改：输入过滤职责；当前约定是业务层负责 currentUserId/active 过滤。
std::vector<RecommendResult> calculateWeakestChapters(const std::vector<RecommendInputItem>& items, int topN);

#endif
