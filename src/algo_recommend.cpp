#include "algo_recommend.h"
#include <map>
#include <algorithm>

using namespace std;

/*
[导读]
- 本文件只做薄弱章节推荐的纯算法计算，不依赖 Card/WrongQuestion 的完整结构。

[输入输出]
- 输入：RecommendInputItem 列表，每项只包含 subject、chapter、mastery。
- 输出：按平均掌握度从低到高排序后的 RecommendResult Top N。

[对应流程图]
- main.cpp 主菜单推荐和 practice.cpp 薄弱点专项练习都会复用这里。

[易错点]
- 空 subject/chapter 会被跳过，避免无分类数据污染推荐分组。
- 同分时优先推荐样本更多的章节，降低单条异常低分的影响。

[实验]
- 改变同分排序规则，再看 tests/test_algorithms.cpp 中推荐顺序断言如何变化。
*/

// 聚合时只保留总掌握度和条目数，避免算法层依赖完整 Card/WrongQuestion 模型。
struct ChapterStat {
    long long totalMastery = 0;
    int count = 0;
};

vector<RecommendResult> calculateWeakestChapters(const vector<RecommendInputItem>& items, int topN) {
    // 使用 map 保持输出在同分同数量时仍具备稳定顺序，便于测试和回归定位。
    map<string, map<string, ChapterStat>> stats;
    
    for (const auto& item : items) {
        if (item.subject.empty() || item.chapter.empty()) continue;
        stats[item.subject][item.chapter].totalMastery += item.mastery;
        stats[item.subject][item.chapter].count += 1;
    }
    
    vector<RecommendResult> results;
    for (const auto& subPair : stats) {
        for (const auto& chapPair : subPair.second) {
            RecommendResult res;
            res.subject = subPair.first;
            res.chapter = chapPair.first;
            res.itemCount = chapPair.second.count;
            res.avgMastery = static_cast<int>(chapPair.second.totalMastery / chapPair.second.count);
            results.push_back(res);
        }
    }
    
    // 同掌握度下优先推荐样本更多的章节，避免单条低分记录过度影响推荐。
    sort(results.begin(), results.end(), [](const RecommendResult& a, const RecommendResult& b) {
        if (a.avgMastery != b.avgMastery) return a.avgMastery < b.avgMastery;
        return a.itemCount > b.itemCount;
    });
    
    if (static_cast<int>(results.size()) > topN) {
        results.resize(topN);
    }
    
    return results;
}
