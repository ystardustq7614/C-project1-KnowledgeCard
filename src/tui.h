#ifndef TUI_H
#define TUI_H

#include <string>
#include <vector>

/*
模块职责：
- 提供轻量级终端界面渲染能力，统一欢迎页、主菜单工作台、提示信息和菜单输入提示的视觉风格。

关键约束：
- tui 模块只负责把已准备好的视图数据渲染到控制台，不读取业务全局状态，也不修改数据文件。
- 所有交互仍然保留原有数字菜单输入，避免破坏自动化 E2E 脚本。
- terminal_sprites_demo 只作为设计素材来源；运行时不依赖 Python 或 JSON 解析。
*/

struct TuiRecommendation {
    std::string subject;
    std::string chapter;
    int avgMastery;
    int itemCount;
};

struct TuiMainMenuView {
    std::string username;
    std::string today;
    std::string dataDirectory;
    int activeCardCount;
    int activeWrongCount;
    int todayReviewCount;
    std::vector<TuiRecommendation> recommendations;
};

struct TuiMenuItem {
    std::string key;
    std::string label;
    std::string hint;
};

enum class TuiNoticeLevel {
    Info,
    Success,
    Warning,
    Error
};

void renderWelcomeMenu();
void renderMainMenu(const TuiMainMenuView& view);
void renderSubMenu(const std::string& title,
                   const std::string& subtitle,
                   const std::vector<TuiMenuItem>& items);
void renderPageHeader(const std::string& title, const std::string& subtitle);
void printTuiSection(const std::string& title);
void printTuiNotice(TuiNoticeLevel level, const std::string& message);
void printTuiPrompt(const std::string& label);

#endif
