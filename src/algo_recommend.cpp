#include "algo_recommend.h"
#include <map>
#include <algorithm>

using namespace std;

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
