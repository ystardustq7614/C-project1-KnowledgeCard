#ifndef WRONG_H
#define WRONG_H

// vector 用于返回 wrongs 全局容器下标列表。
#include <vector>
// string 用于错题文本、分类和错因类型。
#include <string>
// 头文件不使用 using std::string；接口中直接写 std::string，避免影响包含方。

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

[你以后可以改的地方]
- 可以扩展 errorType 的可选值，但要同步 isValidErrorType、输入提示和测试数据。
- 可以新增错题筛选条件，保持返回 wrongs 下标这一约定即可。

[不建议随手改的地方]
- linkedCardId=-1 的未关联语义；转换、维护和测试都依赖它。
- convertWrongToCard 的去重逻辑；它负责避免同一道错题反复生成卡片。
*/

// ========== 错题业务操作 ==========
// 副作用：新增或修改错题后立即保存 wrongs.txt。
// 功能：新增当前用户的错题记录。
void addWrong();
// 功能：修改最近展示列表或自动加载列表中的错题。
void editWrong();
// 功能：逻辑删除错题，将 active 置为 false。
void deleteWrong();

// ========== 错题联动 ==========
// 功能：把错题生成一张知识卡片，并把新 cardId 写回 linkedCardId。
// 说明：如果历史关联指向已删除或不存在的卡片，本流程会重新生成卡片修复关联。
void convertWrongToCard();

// ========== 错题查询 ==========
// 功能：按最近展示列表的序号查看详情；这里的“Id”保留历史命名，用户输入的是展示序号。
void queryWrongById();
// 功能：按关键字搜索题目、答案、错因等文本字段。
void queryWrongByKeyword();
// 功能：按科目/章节/错因类型进入分类浏览。
void viewWrongsByCategory();
// 功能：展示当前用户所有 active=true 的错题，并建立展示序号映射。
void viewAllWrongs();

// ========== 错题打印 ==========
// 参数：displayIdx 是用户看到的序号，realIdx 是 wrongs 容器下标。
void printWrongBrief(int displayIdx, int realIdx);
// 参数：index 是 wrongs 容器下标，调用方要先完成用户归属和 active 校验。
void printWrongDetail(int index);

// ========== 错因分类 ==========
// 返回：空字符串表示用户主动跳过分类，不视为错误。
std::string inputErrorType();
bool isValidErrorType(const std::string& errorType);

// ========== 错题筛选 ==========
// 返回：wrongs 全局容器下标，不是 wrongId。
std::vector<int> filterWrongsBySubject(const std::string& subject);
std::vector<int> filterWrongsByChapter(const std::string& chapter);

// ========== 错题子菜单 ==========
// 功能：错题模块的交互入口，由 main.cpp 登录后主菜单调用。
void showWrongMenu();

#endif
