#ifndef CARD_H
#define CARD_H

#include <vector>
#include <string>

/*
[导读]
- 本头文件声明知识卡片模块的公开入口：CRUD、查询、筛选、排序、打印和子菜单。

[输入输出]
- 输入：当前用户、控制台输入、cards 全局容器。
- 输出：cards 容器变化、cards.txt 和控制台展示。

[易错点]
- 所有面向用户的操作只处理 currentUserId 对应且 active=true 的卡片。
- UI 展示使用连续序号，底层仍使用 cards 容器下标和 cardId；不要把展示序号当作真实 ID。
- 修改、删除、新增等写操作会立即保存 cards.txt。
*/

// ========== 卡片业务操作 ==========
// 功能：新增当前用户的知识卡片。
// 副作用：写入全局 cards 容器并保存 cards.txt。
void addCard();

// 功能：修改最近展示列表或自动加载列表中的卡片。
// 前置条件：用户已登录；若没有最近列表，将退化为当前用户全部有效卡片。
// 副作用：保存 cards.txt。
void editCard();

// 功能：逻辑删除卡片，将 active 置为 false。
// 说明：不物理删除记录，便于数据维护模块恢复。
void deleteCard();

// ========== 卡片查询 ==========
// 功能：按最近展示列表的序号查看详情；这里的“Id”保留历史命名，用户输入的是展示序号。
void queryCardById();
void queryCardByKeyword();
void viewCardsByCategory();
void viewAllCards();

// 功能：清空卡片模块的展示序号映射。
// 说明：跨模块生成新卡片后调用，避免用户继续操作旧列表导致序号错位。
void resetCardDisplayCache();

// ========== 卡片打印 ==========
void printCardBrief(int displayIdx, int realIdx);
void printCardDetail(int index);

// ========== 筛选与排序 ==========
// 返回：cards 全局容器下标，不是 cardId；调用方可继续排序或映射到展示序号。
std::vector<int> filterCardsBySubject(const std::string& subject);
std::vector<int> filterCardsByChapter(const std::string& chapter);
std::vector<int> filterCardsByTag(const std::string& tag);

// 参数：indexes 必须来自当前 cards 容器，函数会原地重排。
void sortCardsByCreateDate(std::vector<int>& indexes, bool ascending = true);
void sortCardsByNextReviewDate(std::vector<int>& indexes, bool ascending = true);
void sortCardsByMastery(std::vector<int>& indexes, bool ascending = true);

// ========== 卡片子菜单 ==========
void showCardMenu();
void viewCardsBySorting();

#endif
