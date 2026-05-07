#ifndef PRACTICE_H
#define PRACTICE_H

/*
[导读]
- 本头文件声明主动练习入口：随机抽查和薄弱点专项练习。

[输入输出]
- 输入：当前用户有效卡片/错题和控制台答题反馈。
- 输出：本次练习正确率；不改变正式复习计划。

[易错点]
- 自测只做即时正确率统计，不修改 mastery、nextReviewDate，也不写 ReviewLog。
- 薄弱点专项复用 algo_recommend 的聚合结果，题目来源仍限定为当前用户有效卡片。
*/

void showPracticeMenu();
void startRandomPractice();
void startWeaknessPractice();

#endif
