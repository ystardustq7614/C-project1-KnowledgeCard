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

[你以后可以改的地方]
- 可以扩展练习模式，例如按科目练习、按标签练习、错题混合练习。
- 可以调整随机抽题数量或薄弱章节选择策略，但要保持“不影响正式复习计划”的边界。

[不建议随手改的地方]
- 不要在练习流程里追加 ReviewLog，否则统计会把自测误算成正式复习。
- 不要把 active=false 或其他用户的数据放入练习池。
*/

// 功能：练习模块的交互入口，由 main.cpp 登录后主菜单调用。
void showPracticeMenu();
// 功能：从当前用户有效卡片中随机抽题，只统计本轮正确率。
void startRandomPractice();
// 功能：根据薄弱章节推荐结果选题练习，只统计本轮正确率。
void startWeaknessPractice();

#endif
