from __future__ import annotations

import argparse
import shutil
import sys
import zipfile
from dataclasses import dataclass
from datetime import datetime
from pathlib import Path

from PIL import Image, ImageChops, ImageDraw, ImageFont, ImageOps

SCRIPT_DIR = Path(__file__).resolve().parent
sys.path.insert(0, str(SCRIPT_DIR))

from markdown_to_docx_with_mermaid import convert  # noqa: E402


@dataclass(frozen=True)
class Panel:
    image_name: str
    label: str


@dataclass(frozen=True)
class FigureSpec:
    file_name: str
    caption: str
    intro: str
    note: str
    panels: tuple[Panel, ...]


FIGURES: tuple[FigureSpec, ...] = (
    FigureSpec(
        file_name="figure_3_1_system_menus.png",
        caption="图 3-1 系统入口与主要菜单界面",
        intro="图 3-1 将系统入口、登录后的主工作台以及常用子菜单合并展示，用于说明系统整体导航结构和控制台界面风格。",
        note="从图中可以看到，系统入口先完成登录或注册；登录后主菜单集中展示日期、数据目录、到期复习数量、卡片/错题数量和薄弱点提示；各业务入口再通过编号进入对应子菜单，保持了统一的窗口式标题、功能说明和操作提示。",
        panels=(
            Panel("image11.png", "（a）系统入口"),
            Panel("image12.png", "（b）登录后主菜单"),
            Panel("image13.png", "（c）知识卡片子菜单"),
            Panel("image24.png", "（d）数据维护子菜单"),
        ),
    ),
    FigureSpec(
        file_name="figure_3_2_card_management.png",
        caption="图 3-2 知识卡片管理功能示意",
        intro="图 3-2 按“新增—查询—查看结果”的顺序组织知识卡片管理界面，突出卡片数据从录入到检索确认的完整链路。",
        note="新增卡片时需要填写学科、章节、正面问题、背面答案、标签和难度，创建成功后立即进入当前用户的卡片列表；多条件查询支持空条件跳过，便于按学科、章节、关键词组合筛选；结果详情同时展示复习次数、掌握度、当前间隔、创建日期、最近复习和下次复习等调度字段。",
        panels=(
            Panel("image14.png", "（a）新增知识卡片"),
            Panel("image16.png", "（b）多条件组合查询"),
            Panel("image15.png", "（c）卡片详情结果"),
        ),
    ),
    FigureSpec(
        file_name="figure_3_3_wrong_to_card.png",
        caption="图 3-3 错题管理与错题转卡片示意",
        intro="图 3-3 将错题录入、错题详情、错题转卡片以及转换后的卡片列表放在同一组中，展示错题沉淀为可复习知识卡片的闭环。",
        note="错题录入阶段记录题目、正确答案、用户错误答案、错因分析和错因类型；详情页用于复核错题的掌握度、复习次数和下次复习时间；执行转卡片后，系统生成新的知识卡片并回写关联关系；最终卡片列表中出现由错题转换得到的条目，说明错题管理已接入复习调度体系。",
        panels=(
            Panel("image17.png", "（a）新增错题"),
            Panel("image18.png", "（b）错题详情"),
            Panel("image19.png", "（c）错题转知识卡片"),
            Panel("image20.png", "（d）转换后的卡片列表"),
        ),
    ),
    FigureSpec(
        file_name="figure_3_4_review_and_stats.png",
        caption="图 3-4 今日复习与统计反馈流程示意",
        intro="图 3-4 将待复习列表、单次复习过程和掌握度统计放在一起，说明系统如何从计划调度进入复习执行，并通过统计结果反馈学习状态。",
        note="今日复习列表按优先级列出到期卡片和错题，优先级综合逾期天数、错题加权和掌握度；开始复习后，系统展示题面或错题信息，用户查看答案后输入掌握程度；统计界面再按知识卡片和错题分别汇总薄弱、一般、熟练区间，帮助判断后续练习重点。",
        panels=(
            Panel("image21.png", "（a）今日待复习列表"),
            Panel("image22.png", "（b）单次复习评分"),
            Panel("image23.png", "（c）掌握情况统计"),
        ),
    ),
)


def get_font(size: int, bold: bool = False) -> ImageFont.FreeTypeFont | ImageFont.ImageFont:
    candidates = [
        "C:/Windows/Fonts/msyhbd.ttc" if bold else "C:/Windows/Fonts/msyh.ttc",
        "C:/Windows/Fonts/simhei.ttf",
        "C:/Windows/Fonts/arial.ttf",
    ]
    for candidate in candidates:
        try:
            return ImageFont.truetype(candidate, size=size)
        except OSError:
            continue
    return ImageFont.load_default()


FONT_PANEL = get_font(30, bold=True)
FONT_CAPTION = get_font(28, bold=True)


def extract_screenshots(docx_path: Path, output_dir: Path) -> None:
    output_dir.mkdir(parents=True, exist_ok=True)
    with zipfile.ZipFile(docx_path) as archive:
        for index in range(11, 25):
            media_name = f"word/media/image{index}.png"
            target = output_dir / f"image{index}.png"
            with archive.open(media_name) as source, target.open("wb") as destination:
                shutil.copyfileobj(source, destination)


def crop_white_margin(image: Image.Image) -> Image.Image:
    rgb = image.convert("RGB")
    background = Image.new("RGB", rgb.size, (255, 255, 255))
    diff = ImageChops.difference(rgb, background)
    bbox = diff.getbbox()
    if not bbox:
        return rgb
    left, top, right, bottom = bbox
    padding = 10
    left = max(0, left - padding)
    top = max(0, top - padding)
    right = min(rgb.width, right + padding)
    bottom = min(rgb.height, bottom + padding)
    return rgb.crop((left, top, right, bottom))


def fit_image(image: Image.Image, max_width: int, max_height: int) -> Image.Image:
    fitted = image.copy()
    fitted.thumbnail((max_width, max_height), Image.Resampling.LANCZOS)
    return fitted


def draw_panel(draw: ImageDraw.ImageDraw, box: tuple[int, int, int, int], panel: Panel, image: Image.Image) -> None:
    left, top, right, bottom = box
    draw.rounded_rectangle((left, top, right, bottom), radius=18, fill="#FFFFFF", outline="#CBD5E1", width=2)
    draw.text((left + 20, top + 16), panel.label, fill="#111827", font=FONT_PANEL)

    img_x = left + (right - left - image.width) // 2
    img_y = top + 62 + max(0, (bottom - top - 82 - image.height) // 2)
    draw.rounded_rectangle(
        (img_x - 3, img_y - 3, img_x + image.width + 3, img_y + image.height + 3),
        radius=8,
        fill="#F8FAFC",
        outline="#E5E7EB",
    )


def compose_figure(spec: FigureSpec, source_dir: Path, output_path: Path) -> None:
    columns = 2
    cell_width = 830
    cell_height = 500
    gap = 34
    margin = 44
    title_height = 58
    rows = (len(spec.panels) + columns - 1) // columns
    width = margin * 2 + columns * cell_width + (columns - 1) * gap
    height = margin * 2 + title_height + rows * cell_height + (rows - 1) * gap

    canvas = Image.new("RGB", (width, height), "#F8FAFC")
    draw = ImageDraw.Draw(canvas)
    draw.text((margin, 26), spec.caption, fill="#0F172A", font=FONT_CAPTION)

    for index, panel in enumerate(spec.panels):
        row = index // columns
        col = index % columns
        items_in_last_row = len(spec.panels) - row * columns
        x_offset = 0
        if row == rows - 1 and items_in_last_row == 1:
            x_offset = (cell_width + gap) // 2

        left = margin + col * (cell_width + gap) + x_offset
        top = margin + title_height + row * (cell_height + gap)
        right = left + cell_width
        bottom = top + cell_height

        raw = Image.open(source_dir / panel.image_name).convert("RGB")
        cropped = crop_white_margin(raw)
        fitted = fit_image(cropped, cell_width - 44, cell_height - 96)
        draw_panel(draw, (left, top, right, bottom), panel, fitted)
        img_x = left + (cell_width - fitted.width) // 2
        img_y = top + 76 + max(0, (cell_height - 102 - fitted.height) // 2)
        canvas.paste(fitted, (img_x, img_y))

    output_path.parent.mkdir(parents=True, exist_ok=True)
    canvas.save(output_path, dpi=(300, 300))


def prepare_display_screenshots(source_dir: Path, output_dir: Path) -> None:
    output_dir.mkdir(parents=True, exist_ok=True)
    for spec in FIGURES:
        for panel in spec.panels:
            source = source_dir / panel.image_name
            target = output_dir / panel.image_name
            image = Image.open(source).convert("RGB")
            # Keep the original pixel ratio and content; only set DPI so Word shows
            # console screenshots close to the readable text width.
            image.save(target, dpi=(96, 96))


def build_screenshot_section(screenshots_dir: Path) -> str:
    lines = [
        "---",
        "",
        "### 3.4 系统运行截图与界面演示",
        "",
        "本节将系统运行截图按功能链路分组展示。每组图只保留一个图号，组内使用（a）（b）（c）标识不同界面，既避免单张截图零散堆叠，也保证截图按原始比例接近正文宽度显示，便于查看界面文字和交互细节。",
        "",
    ]
    for spec in FIGURES:
        lines.extend(
            [
                f"#### {spec.caption}",
                "",
                spec.intro,
                "",
            ]
        )
        for panel in spec.panels:
            image_path = (screenshots_dir / panel.image_name).as_posix()
            lines.extend(
                [
                    f"**{panel.label}**",
                    "",
                    f"![]({image_path})",
                    "",
                ]
            )
        lines.extend([spec.note, ""])
    return "\n".join(lines).rstrip() + "\n\n"


def inject_screenshot_section(markdown_path: Path, output_path: Path, screenshots_dir: Path) -> None:
    source = markdown_path.read_text(encoding="utf-8")
    section = build_screenshot_section(screenshots_dir)
    marker = "---\n\n*本报告记录了项目在封版时刻的整体设计。"
    if marker not in source:
        raise RuntimeError("Could not find final report note marker for screenshot section insertion.")
    updated = source.replace(marker, section + marker, 1)
    output_path.parent.mkdir(parents=True, exist_ok=True)
    output_path.write_text(updated, encoding="utf-8", newline="\n")


def build_report(markdown_path: Path, source_docx: Path, output_docx: Path, build_dir: Path) -> None:
    extracted_dir = build_dir / "extracted_screenshots"
    screenshots_dir = build_dir / "screenshot_original_ratio"
    prepared_markdown = build_dir / "项目总结报告_截图演示完善版.md"

    extract_screenshots(source_docx, extracted_dir)
    prepare_display_screenshots(extracted_dir, screenshots_dir)
    inject_screenshot_section(markdown_path, prepared_markdown, screenshots_dir)
    convert(prepared_markdown, output_docx, build_dir / "docx")


def backup_file(path: Path, backup_dir: Path) -> Path:
    backup_dir.mkdir(parents=True, exist_ok=True)
    timestamp = datetime.now().strftime("%Y%m%d_%H%M%S")
    backup_path = backup_dir / f"{path.stem}_截图整理前备份_{timestamp}{path.suffix}"
    shutil.copy2(path, backup_path)
    return backup_path


def main() -> None:
    parser = argparse.ArgumentParser(description="Build a report DOCX with grouped system screenshots.")
    parser.add_argument("--markdown", type=Path, default=Path("项目总结报告.md"))
    parser.add_argument("--source-docx", type=Path, default=Path("项目总结报告.docx"))
    parser.add_argument("--output-docx", type=Path, default=Path("项目总结报告.docx"))
    parser.add_argument("--build-dir", type=Path, default=Path("build") / "screenshot_demo")
    parser.add_argument("--backup", action="store_true")
    args = parser.parse_args()

    backup_path = None
    if args.backup and args.output_docx.exists():
        backup_path = backup_file(args.output_docx, args.build_dir / "backup")

    temp_output = args.build_dir / "项目总结报告_截图演示完善版.docx"
    build_report(args.markdown, args.source_docx, temp_output, args.build_dir)
    args.output_docx.parent.mkdir(parents=True, exist_ok=True)
    shutil.copy2(temp_output, args.output_docx)

    print(f"Generated {args.output_docx}")
    if backup_path:
        print(f"Backup saved to {backup_path}")


if __name__ == "__main__":
    main()
