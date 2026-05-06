#ifndef PRACTICE_H
#define PRACTICE_H

/*
模块职责：
- 提供不影响复习计划的主动练习入口：随机抽查和薄弱点专项练习。

关键约束：
- 自测只做即时正确率统计，不修改 mastery、nextReviewDate，也不写 ReviewLog。
- 薄弱点专项复用 algo_recommend 的聚合结果，题目来源仍限定为当前用户有效卡片。
*/

void showPracticeMenu();
void startRandomPractice();
void startWeaknessPractice();

#endif
