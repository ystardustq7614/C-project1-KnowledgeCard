// 引入本算法对应的头文件：RecommendInputItem、RecommendResult 和 calculateWeakestChapters 声明都在这里。
#include "algo_recommend.h"

// map 用来按 subject -> chapter 聚合数据，并提供稳定的键顺序。
#include <map>

// algorithm 提供 std::sort，用于对推荐结果排序。
#include <algorithm>

// 只引入本文件实际使用的标准库名字，避免命名空间整体引入污染。
using std::map;
using std::sort;
using std::string;
using std::vector;

/*
[导读]
- 本文件只做薄弱章节推荐的纯算法计算。
- 它不依赖 Card/WrongQuestion 的完整结构，只依赖调用方整理出的 subject/chapter/mastery。
- main.cpp 主菜单推荐和 practice.cpp 薄弱点专项练习都会复用这里。

[输入输出]
- 输入 items：每项包含 subject、chapter、mastery。
- 输入 topN：最多返回多少个薄弱章节；当前调用方应传入非负数。
- 输出 RecommendResult 列表：按薄弱程度排序后的 Top N。

[核心规则]
1. 跳过空 subject 或空 chapter 的数据。
2. 按“学科 + 章节”分组。
3. 每组计算平均掌握度 avgMastery。
4. avgMastery 越低越靠前。
5. avgMastery 相同，则 itemCount 越多越靠前。

[你以后最常改的地方]
- 过滤规则：例如是否过滤 mastery 异常值、是否只推荐 active 卡片，这通常应先在业务层处理。
- 聚合指标：现在用平均掌握度；以后可以改为最低分、加权平均、最近表现等。
- 排序规则：现在先按 avgMastery 升序，再按 itemCount 降序。

[不建议随便改的地方]
- 空 subject/chapter 过滤：否则 UI 可能出现无法定位的“空分类”推荐。
- 同分时 itemCount 多者优先：这让推荐更稳定，避免单条异常低分抢占推荐。
- map 的稳定遍历顺序：它让同分同数量时的输出更容易测试和复现。

[简化说明]
- 算法层不读取登录状态，也不知道当前用户是谁。
- 调用方应先把当前用户、active=true、需要参与推荐的数据整理成 RecommendInputItem。
*/

// 聚合时只保留总掌握度和条目数，避免算法层依赖完整 Card/WrongQuestion 模型。
struct ChapterStat {
    // 当前章节所有样本的 mastery 总和。
    long long totalMastery = 0;

    // 当前章节参与统计的样本数量。
    int count = 0;
};

// 计算最薄弱的 Top N 章节。
vector<RecommendResult> calculateWeakestChapters(const vector<RecommendInputItem>& items, int topN) {
    // stats 的结构是：学科 -> 章节 -> 聚合统计。
    // 使用 map 保持输出在同分同数量时仍具备稳定顺序，便于测试和回归定位。
    map<string, map<string, ChapterStat>> stats;
    
    // 遍历调用方传入的每条卡片/错题摘录。
    for (const auto& item : items) {
        // 空学科或空章节无法在 UI 中准确定位，所以直接跳过。
        if (item.subject.empty() || item.chapter.empty()) continue;

        // 把同一个 subject + chapter 的掌握度累加起来。
        stats[item.subject][item.chapter].totalMastery += item.mastery;

        // 同时记录该章节有多少条样本，后面用于计算平均值和并列排序。
        stats[item.subject][item.chapter].count += 1;
    }
    
    // 保存最终推荐结果。
    vector<RecommendResult> results;

    // 第一层遍历学科。
    for (const auto& subPair : stats) {
        // 第二层遍历该学科下的章节。
        for (const auto& chapPair : subPair.second) {
            // 创建一条推荐结果。
            RecommendResult res;

            // subPair.first 是学科名。
            res.subject = subPair.first;

            // chapPair.first 是章节名。
            res.chapter = chapPair.first;

            // chapPair.second.count 是该章节聚合到的样本数量。
            res.itemCount = chapPair.second.count;

            // 平均掌握度 = 总掌握度 / 样本数量。
            // 这里转成 int，表示直接向下取整；测试会保护当前行为。
            res.avgMastery = static_cast<int>(chapPair.second.totalMastery / chapPair.second.count);

            // 把结果放进列表，稍后统一排序。
            results.push_back(res);
        }
    }
    
    // 对推荐结果排序。
    // 同掌握度下优先推荐样本更多的章节，避免单条低分记录过度影响推荐。
    sort(results.begin(), results.end(), [](const RecommendResult& a, const RecommendResult& b) {
        // 第一排序条件：平均掌握度越低，越应该优先推荐。
        if (a.avgMastery != b.avgMastery) return a.avgMastery < b.avgMastery;

        // 第二排序条件：平均掌握度相同时，样本数量越多，统计越稳定，越优先推荐。
        return a.itemCount > b.itemCount;
    });
    
    // 如果结果数量超过 topN，就截断为前 topN 条。
    // [谨慎改] 当前调用方应保证 topN >= 0；如果未来允许负数输入，应先在这里加保护。
    if (static_cast<int>(results.size()) > topN) {
        results.resize(topN);
    }
    
    // 返回已经排序并截断后的推荐结果。
    return results;
}
