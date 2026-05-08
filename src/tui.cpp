#include "tui.h"
#include "utils.h"

#include <algorithm>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

using std::cout;
using std::max;
using std::ostringstream;
using std::string;
using std::vector;

namespace {

const int kFrameWidth = 76;
const int kSpriteWidth = 12;

string rgb(int r, int g, int b) {
    ostringstream out;
    out << "\033[38;2;" << r << ";" << g << ";" << b << "m";
    return out.str();
}

const string kReset = ANSI_RESET;
const string kBold = ANSI_BOLD;
const string kBorder = rgb(115, 129, 145);
const string kMuted = rgb(151, 162, 174);
const string kText = rgb(221, 226, 232);
const string kAccent = rgb(214, 121, 86);
const string kAccentSoft = rgb(232, 159, 117);
const string kBlue = rgb(116, 166, 205);
const string kGreen = rgb(119, 190, 147);
const string kYellow = rgb(223, 184, 104);
const string kRed = rgb(226, 111, 103);

bool isWideCodePoint(unsigned int cp) {
    return (cp >= 0x1100 && cp <= 0x115F) ||
           (cp >= 0x2E80 && cp <= 0xA4CF) ||
           (cp >= 0xAC00 && cp <= 0xD7A3) ||
           (cp >= 0xF900 && cp <= 0xFAFF) ||
           (cp >= 0xFE10 && cp <= 0xFE19) ||
           (cp >= 0xFE30 && cp <= 0xFE6F) ||
           (cp >= 0xFF00 && cp <= 0xFF60) ||
           (cp >= 0xFFE0 && cp <= 0xFFE6);
}

size_t utf8SequenceLength(unsigned char c) {
    if ((c & 0x80) == 0) return 1;
    if ((c & 0xE0) == 0xC0) return 2;
    if ((c & 0xF0) == 0xE0) return 3;
    if ((c & 0xF8) == 0xF0) return 4;
    return 1;
}

unsigned int decodeCodePoint(const string& text, size_t pos, size_t len) {
    const unsigned char c0 = static_cast<unsigned char>(text[pos]);
    if (len == 1) return c0;
    if (pos + len > text.size()) return c0;

    if (len == 2) {
        return ((c0 & 0x1F) << 6) |
               (static_cast<unsigned char>(text[pos + 1]) & 0x3F);
    }
    if (len == 3) {
        return ((c0 & 0x0F) << 12) |
               ((static_cast<unsigned char>(text[pos + 1]) & 0x3F) << 6) |
               (static_cast<unsigned char>(text[pos + 2]) & 0x3F);
    }
    if (len == 4) {
        return ((c0 & 0x07) << 18) |
               ((static_cast<unsigned char>(text[pos + 1]) & 0x3F) << 12) |
               ((static_cast<unsigned char>(text[pos + 2]) & 0x3F) << 6) |
               (static_cast<unsigned char>(text[pos + 3]) & 0x3F);
    }
    return c0;
}

int displayWidth(const string& text) {
    int width = 0;
    for (size_t i = 0; i < text.size();) {
        if (text[i] == '\033' && i + 1 < text.size() && text[i + 1] == '[') {
            i += 2;
            while (i < text.size() && (text[i] < '@' || text[i] > '~')) {
                ++i;
            }
            if (i < text.size()) ++i;
            continue;
        }

        const unsigned char c = static_cast<unsigned char>(text[i]);
        const size_t len = utf8SequenceLength(c);
        const unsigned int cp = decodeCodePoint(text, i, len);
        width += isWideCodePoint(cp) ? 2 : 1;
        i += max<size_t>(1, len);
    }
    return width;
}

string repeatText(const string& text, int count) {
    string result;
    for (int i = 0; i < count; ++i) {
        result += text;
    }
    return result;
}

string padRight(const string& text, int width) {
    const int pad = max(0, width - displayWidth(text));
    return text + string(static_cast<size_t>(pad), ' ');
}

string centerText(const string& text, int width) {
    const int pad = max(0, width - displayWidth(text));
    const int left = pad / 2;
    const int right = pad - left;
    return string(static_cast<size_t>(left), ' ') + text + string(static_cast<size_t>(right), ' ');
}

string colorText(const string& color, const string& text) {
    return color + text + kReset;
}

void printTopBorder() {
    cout << kBorder << "╭" << repeatText("─", kFrameWidth) << "╮\n" << kReset;
}

void printSeparator() {
    cout << kBorder << "├" << repeatText("─", kFrameWidth) << "┤\n" << kReset;
}

void printBottomBorder() {
    cout << kBorder << "╰" << repeatText("─", kFrameWidth) << "╯\n" << kReset;
}

void printFrameLine(const string& content = "") {
    cout << kBorder << "│" << kReset
         << padRight(content, kFrameWidth)
         << kBorder << "│\n" << kReset;
}

vector<string> makeSprite(const string& letter, const string& label, const string& accent) {
    vector<string> lines;
    lines.push_back(centerText(colorText(kBold + accent, letter), kSpriteWidth));
    lines.push_back(colorText(accent, "   .----.   "));
    lines.push_back(colorText(accent, "  /      \\  "));
    lines.push_back(colorText(accent, "  \\______/  "));
    lines.push_back(colorText(kText, "    /||\\    "));
    lines.push_back(colorText(kText, "   /_||_\\   "));
    lines.push_back(colorText(kMuted, "    /  \\    "));
    lines.push_back(centerText(colorText(kBold + accent, label), kSpriteWidth));
    return lines;
}

vector<string> buildSpriteGallery() {
    const vector<vector<string>> sprites = {
        makeSprite("C", "CARD", kAccent),
        makeSprite("R", "REVIEW", kYellow),
        makeSprite("S", "STUDY", kBlue),
        makeSprite("L", "LOOP", kGreen)
    };
    vector<string> lines;
    for (size_t row = 0; row < sprites[0].size(); ++row) {
        string line = "   ";
        for (size_t i = 0; i < sprites.size(); ++i) {
            if (i > 0) {
                line += "    ";
            }
            line += sprites[i][row];
        }
        lines.push_back(line);
    }
    return lines;
}

string truncateAsciiMiddle(const string& text, size_t maxChars) {
    if (text.size() <= maxChars) {
        return text;
    }
    if (maxChars <= 3) {
        return text.substr(0, maxChars);
    }
    return text.substr(0, maxChars - 3) + "...";
}

string menuItem(const string& key, const string& label, const string& hint = "") {
    string result = colorText(kAccentSoft, "[" + key + "]") + " " + colorText(kText, label);
    if (!hint.empty()) {
        result += " " + colorText(kMuted, hint);
    }
    return result;
}

string noticePrefix(TuiNoticeLevel level) {
    if (level == TuiNoticeLevel::Success) return colorText(kGreen, "✦");
    if (level == TuiNoticeLevel::Warning) return colorText(kYellow, "!");
    if (level == TuiNoticeLevel::Error) return colorText(kRed, "!");
    return colorText(kBlue, "*");
}

} // namespace

void renderWelcomeMenu() {
    printTopBorder();
    printFrameLine(" " + colorText(kRed, "●") + " " + colorText(kYellow, "●") + " " +
                   colorText(kGreen, "●") + "  " +
                   colorText(kBold + kText, "Knowledge Review Console") + "  " +
                   colorText(kMuted, "v1.4.4"));
    printSeparator();
    printFrameLine(" " + colorText(kAccentSoft, "*") + " " +
                   colorText(kText, "Welcome back to the learning console"));
    printFrameLine();

    for (const string& line : buildSpriteGallery()) {
        printFrameLine(line);
    }

    printFrameLine();
    printFrameLine(" " + colorText(kGreen, "✦") + " " +
                   colorText(kMuted, "CARD -> REVIEW -> STUDY -> LOOP"));
    printFrameLine(" " + menuItem("1", "用户登录") + "     " +
                   menuItem("2", "用户注册") + "     " +
                   menuItem("0", "退出系统"));
    printBottomBorder();
    printTuiPrompt("请选择");
}

void renderMainMenu(const TuiMainMenuView& view) {
    const string shortDataDir = truncateAsciiMiddle(view.dataDirectory, 26);

    printTopBorder();
    printFrameLine(" " + colorText(kRed, "●") + " " + colorText(kYellow, "●") + " " +
                   colorText(kGreen, "●") + "  " +
                   colorText(kBold + kText, "Knowledge Review Console") + "  " +
                   colorText(kMuted, "user: ") + colorText(kYellow, view.username));
    printSeparator();
    printFrameLine(" " + colorText(kAccentSoft, "Today"));
    printFrameLine("   date: " + colorText(kText, view.today) +
                   "    data: " + colorText(kMuted, shortDataDir));
    printFrameLine("   due reviews: " + colorText(kYellow, std::to_string(view.todayReviewCount)) +
                   "    cards: " + colorText(kGreen, std::to_string(view.activeCardCount)) +
                   "    wrongs: " + colorText(kRed, std::to_string(view.activeWrongCount)));
    printFrameLine();

    printFrameLine(" " + colorText(kAccentSoft, "Focus"));
    if (view.recommendations.empty()) {
        printFrameLine("   " + colorText(kMuted, "暂无薄弱点推荐，先录入卡片或错题后这里会显示重点。"));
    } else {
        for (size_t i = 0; i < view.recommendations.size(); ++i) {
            const TuiRecommendation& rec = view.recommendations[i];
            const string title = rec.subject + " / " + rec.chapter;
            printFrameLine("   " + colorText(kRed, std::to_string(i + 1) + ".") + " " +
                           colorText(kText, title) +
                           colorText(kMuted, "  mastery ") +
                           colorText(kYellow, std::to_string(rec.avgMastery)) +
                           colorText(kMuted, "  items ") +
                           colorText(kBlue, std::to_string(rec.itemCount)));
        }
    }
    printFrameLine();

    printFrameLine(" " + colorText(kAccentSoft, "Actions"));
    printFrameLine("   " + padRight(menuItem("1", "知识卡片管理"), 32) + menuItem("2", "错题管理"));
    printFrameLine("   " + padRight(menuItem("3", "今日复习", "priority"), 32) + menuItem("4", "自测练习"));
    printFrameLine("   " + padRight(menuItem("5", "统计分析"), 32) + menuItem("6", "数据维护"));
    printFrameLine("   " + padRight(menuItem("7", "修改密码"), 32) + menuItem("0", "退出登录"));
    printBottomBorder();
    printTuiPrompt("请选择");
}

void renderSubMenu(const string& title, const string& subtitle, const vector<TuiMenuItem>& items) {
    printTopBorder();
    printFrameLine(" " + colorText(kRed, "●") + " " + colorText(kYellow, "●") + " " +
                   colorText(kGreen, "●") + "  " +
                   colorText(kBold + kText, "Knowledge Review Console") + "  " +
                   colorText(kMuted, "module"));
    printSeparator();
    printFrameLine(" " + colorText(kAccentSoft, title));
    if (!subtitle.empty()) {
        printFrameLine("   " + colorText(kMuted, subtitle));
    }
    printFrameLine();
    printFrameLine(" " + colorText(kAccentSoft, "Actions"));

    for (size_t i = 0; i < items.size(); i += 2) {
        const TuiMenuItem& left = items[i];
        string line = "   " + padRight(menuItem(left.key, left.label, left.hint), 34);
        if (i + 1 < items.size()) {
            const TuiMenuItem& right = items[i + 1];
            line += menuItem(right.key, right.label, right.hint);
        }
        printFrameLine(line);
    }

    printBottomBorder();
    printTuiPrompt("请选择");
}

void renderPageHeader(const string& title, const string& subtitle) {
    printTopBorder();
    printFrameLine(" " + colorText(kRed, "●") + " " + colorText(kYellow, "●") + " " +
                   colorText(kGreen, "●") + "  " +
                   colorText(kBold + kText, title));
    if (!subtitle.empty()) {
        printSeparator();
        printFrameLine(" " + colorText(kMuted, subtitle));
    }
    printBottomBorder();
    cout << "\n";
}

void printTuiSection(const string& title) {
    cout << colorText(kAccentSoft, "▸ ") << colorText(kBold + kText, title) << "\n";
}

void printTuiNotice(TuiNoticeLevel level, const string& message) {
    string color = kBlue;
    if (level == TuiNoticeLevel::Success) color = kGreen;
    if (level == TuiNoticeLevel::Warning) color = kYellow;
    if (level == TuiNoticeLevel::Error) color = kRed;

    cout << noticePrefix(level) << " " << colorText(color, message) << "\n";
}

void printTuiPrompt(const string& label) {
    cout << colorText(kAccentSoft, ">") << " " << colorText(kText, label + "：");
}
