#ifndef ALGO_RECOMMEND_H
#define ALGO_RECOMMEND_H

#include <string>
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
    std::string subject;
    std::string chapter;
    int mastery;
};

struct RecommendResult {
    std::string subject;
    std::string chapter;
    int avgMastery;
    int itemCount;
};

// ================================================================
// 纯函数算法：计算 Top N 薄弱章节
// ================================================================
// 传入用户的卡片/错题摘录，聚合计算每个（学科+章节）的平均掌握度。
// 返回平均掌握度最低的 Top N 个章节。
// ================================================================
// 排序：平均掌握度低者优先；同分时 itemCount 多者优先，避免小样本章节抢占推荐。
std::vector<RecommendResult> calculateWeakestChapters(const std::vector<RecommendInputItem>& items, int topN);

#endif
