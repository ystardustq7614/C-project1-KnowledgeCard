#ifndef WRONG_H
#define WRONG_H

#include <vector>
#include <string>
using std::string;

/*
[导读]
- 本头文件声明错题模块的公开入口：CRUD、查询、错因分类和“错题转知识卡片”。

[输入输出]
- 输入：当前用户、控制台输入、wrongs/cards 全局容器。
- 输出：wrongs.txt，转换时还会输出 cards.txt。

[易错点]
- 所有面向用户的操作只处理 currentUserId 对应且 active=true 的错题。
- linkedCardId 用于防止同一道错题重复转换；失效关联由数据维护模块重置为 -1。
- 普通删除是逻辑删除，物理清理只在 maintenance.cpp 的回收站流程中执行。
*/

// ========== 错题业务操作 ==========
// 副作用：新增或修改错题后立即保存 wrongs.txt。
void addWrong();
void editWrong();
void deleteWrong();

// ========== 错题联动 ==========
// 功能：把错题生成一张知识卡片，并把新 cardId 写回 linkedCardId。
// 说明：如果历史关联指向已删除或不存在的卡片，本流程会重新生成卡片修复关联。
void convertWrongToCard();

// ========== 错题查询 ==========
// 功能：按最近展示列表的序号查看详情；这里的“Id”保留历史命名，用户输入的是展示序号。
void queryWrongById();
void queryWrongByKeyword();
void viewWrongsByCategory();
void viewAllWrongs();

// ========== 错题打印 ==========
void printWrongBrief(int displayIdx, int realIdx);
void printWrongDetail(int index);

// ========== 错因分类 ==========
// 返回：空字符串表示用户主动跳过分类，不视为错误。
string inputErrorType();
bool isValidErrorType(const string& errorType);

// ========== 错题筛选 ==========
// 返回：wrongs 全局容器下标，不是 wrongId。
std::vector<int> filterWrongsBySubject(const string& subject);
std::vector<int> filterWrongsByChapter(const string& chapter);

// ========== 错题子菜单 ==========
void showWrongMenu();

#endif
