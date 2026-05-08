from __future__ import annotations

import argparse
import math
import re
import subprocess
from collections import OrderedDict, defaultdict, deque
from dataclasses import dataclass, field
from pathlib import Path

from PIL import Image, ImageDraw, ImageFont


NODE_RE = re.compile(r"^([A-Za-z_][A-Za-z0-9_]*)(.*)$")
EDGE_RE = re.compile(r"(.+?)\s*-->\s*(?:\|(.+?)\|\s*)?(.+)$")
MERMAID_RE = re.compile(r"```mermaid\s*\n(.*?)\n```", re.S)


@dataclass
class Node:
    node_id: str
    label: str
    shape: str = "rect"
    group: str | None = None
    order: int = 0
    box: tuple[float, float, float, float] = (0, 0, 0, 0)
    text_lines: list[str] = field(default_factory=list)


@dataclass
class Edge:
    source: str
    target: str
    label: str = ""


@dataclass
class Group:
    group_id: str
    label: str
    nodes: list[str] = field(default_factory=list)
    box: tuple[float, float, float, float] = (0, 0, 0, 0)


@dataclass
class Diagram:
    nodes: "OrderedDict[str, Node]" = field(default_factory=OrderedDict)
    edges: list[Edge] = field(default_factory=list)
    groups: "OrderedDict[str, Group]" = field(default_factory=OrderedDict)


def font(size: int, bold: bool = False) -> ImageFont.FreeTypeFont | ImageFont.ImageFont:
    candidates = [
        "C:/Windows/Fonts/msyhbd.ttc" if bold else "C:/Windows/Fonts/msyh.ttc",
        "C:/Windows/Fonts/simhei.ttf",
        "C:/Windows/Fonts/simsun.ttc",
        "C:/Windows/Fonts/arial.ttf",
    ]
    for candidate in candidates:
        try:
            return ImageFont.truetype(candidate, size=size)
        except OSError:
            continue
    return ImageFont.load_default()


FONT_TITLE = font(28, bold=True)
FONT_GROUP = font(22, bold=True)
FONT_NODE = font(19)
FONT_EDGE = font(16)


def clean_label(value: str | None) -> str:
    if not value:
        return ""
    text = value.strip()
    if len(text) >= 2 and text[0] == text[-1] == '"':
        text = text[1:-1]
    text = re.sub(r"<br\s*/?>", "\n", text, flags=re.I)
    text = text.replace("&nbsp;", " ").replace("&amp;", "&")
    return text.strip()


def parse_node_expr(expr: str) -> tuple[str, str | None, str]:
    expr = expr.strip().rstrip(";")
    match = NODE_RE.match(expr)
    if not match:
        return expr, None, "rect"

    node_id, rest = match.group(1), match.group(2).strip()
    if not rest:
        return node_id, None, "rect"

    if rest.startswith("([") and rest.endswith("])"):
        return node_id, clean_label(rest[2:-2]), "round"
    if rest.startswith("{") and rest.endswith("}"):
        return node_id, clean_label(rest[1:-1]), "diamond"
    if rest.startswith("[") and rest.endswith("]"):
        return node_id, clean_label(rest[1:-1]), "rect"
    if rest.startswith("(") and rest.endswith(")"):
        return node_id, clean_label(rest[1:-1]), "round"
    return node_id, None, "rect"


def ensure_node(diagram: Diagram, node_id: str, label: str | None, shape: str, group: str | None) -> None:
    if node_id not in diagram.nodes:
        diagram.nodes[node_id] = Node(
            node_id=node_id,
            label=label or node_id,
            shape=shape,
            group=group,
            order=len(diagram.nodes),
        )
        if group and group in diagram.groups and node_id not in diagram.groups[group].nodes:
            diagram.groups[group].nodes.append(node_id)
        return

    node = diagram.nodes[node_id]
    if label:
        node.label = label
    if shape:
        node.shape = shape
    if group and not node.group:
        node.group = group
        if group in diagram.groups and node_id not in diagram.groups[group].nodes:
            diagram.groups[group].nodes.append(node_id)


def parse_mermaid(block: str) -> Diagram:
    diagram = Diagram()
    current_group: str | None = None

    for raw_line in block.splitlines():
        line = raw_line.strip()
        if not line or line.startswith("%%"):
            continue
        if line.startswith("graph ") or line.startswith("flowchart "):
            continue
        if line == "end":
            current_group = None
            continue
        if line.startswith("subgraph "):
            group_expr = line[len("subgraph ") :].strip()
            group_id, label, _ = parse_node_expr(group_expr)
            current_group = group_id
            diagram.groups[group_id] = Group(group_id=group_id, label=label or group_id)
            continue

        edge = EDGE_RE.match(line)
        if edge:
            source_expr, edge_label, target_expr = edge.groups()
            source_id, source_label, source_shape = parse_node_expr(source_expr)
            target_id, target_label, target_shape = parse_node_expr(target_expr)
            ensure_node(diagram, source_id, source_label, source_shape, current_group)
            ensure_node(diagram, target_id, target_label, target_shape, current_group)
            diagram.edges.append(Edge(source_id, target_id, clean_label(edge_label)))
            continue

        node_id, label, shape = parse_node_expr(line)
        ensure_node(diagram, node_id, label, shape, current_group)

    return diagram


def text_width(draw: ImageDraw.ImageDraw, text: str, used_font: ImageFont.ImageFont) -> float:
    return draw.textbbox((0, 0), text, font=used_font)[2]


def wrap_line(draw: ImageDraw.ImageDraw, text: str, max_width: int, used_font: ImageFont.ImageFont) -> list[str]:
    if text_width(draw, text, used_font) <= max_width:
        return [text]

    lines: list[str] = []
    current = ""
    for char in text:
        candidate = current + char
        if current and text_width(draw, candidate, used_font) > max_width:
            lines.append(current)
            current = char
        else:
            current = candidate
    if current:
        lines.append(current)
    return lines


def prepare_node_sizes(diagram: Diagram, draw: ImageDraw.ImageDraw) -> None:
    for node in diagram.nodes.values():
        raw_lines = clean_label(node.label).splitlines() or [node.node_id]
        wrapped: list[str] = []
        for raw in raw_lines:
            wrapped.extend(wrap_line(draw, raw, 250, FONT_NODE))
        node.text_lines = wrapped

        max_text = max((text_width(draw, line, FONT_NODE) for line in wrapped), default=120)
        width = min(max(max_text + 52, 190), 330)
        line_height = 25
        height = max(72, len(wrapped) * line_height + 34)
        if node.shape == "diamond":
            width = max(width, 210)
            height = max(height + 20, 100)
        node.box = (0, 0, width, height)


def chunks(items: list[str], size: int) -> list[list[str]]:
    return [items[index : index + size] for index in range(0, len(items), size)]


def assign_flow_layout(diagram: Diagram) -> tuple[int, int]:
    outgoing: dict[str, list[str]] = defaultdict(list)
    indegree: dict[str, int] = defaultdict(int)
    for edge in diagram.edges:
        outgoing[edge.source].append(edge.target)
        indegree[edge.target] += 1
        indegree.setdefault(edge.source, 0)

    roots = [node_id for node_id in diagram.nodes if indegree.get(node_id, 0) == 0]
    if not roots and diagram.nodes:
        roots = [next(iter(diagram.nodes))]

    level: dict[str, int] = {}
    queue: deque[str] = deque()
    for root in roots:
        level[root] = 0
        queue.append(root)

    while queue:
        source = queue.popleft()
        for target in outgoing.get(source, []):
            if target not in level:
                level[target] = level[source] + 1
                queue.append(target)

    next_level = max(level.values(), default=-1) + 1
    for node_id in diagram.nodes:
        if node_id not in level:
            level[node_id] = next_level
            next_level += 1

    rows: list[list[str]] = []
    for depth in sorted(set(level.values())):
        nodes = [node_id for node_id in diagram.nodes if level[node_id] == depth]
        rows.extend(chunks(nodes, 4))

    margin_x, margin_y = 90, 70
    row_gap, cell_gap = 74, 52
    max_width = max((node.box[2] for node in diagram.nodes.values()), default=220)
    cell_width = max_width + cell_gap
    canvas_width = max(margin_x * 2 + min(max(len(row) for row in rows), 4) * cell_width - cell_gap, 760)

    y = margin_y
    for row in rows:
        row_height = max(diagram.nodes[node_id].box[3] for node_id in row)
        row_width = len(row) * cell_width - cell_gap
        x = (canvas_width - row_width) / 2
        for index, node_id in enumerate(row):
            node = diagram.nodes[node_id]
            width, height = node.box[2], node.box[3]
            left = x + index * cell_width + (cell_width - cell_gap - width) / 2
            top = y + (row_height - height) / 2
            node.box = (left, top, left + width, top + height)
        y += row_height + row_gap

    return int(canvas_width), int(y + margin_y - row_gap)


def assign_group_layout(diagram: Diagram) -> tuple[int, int]:
    margin_x, margin_y = 80, 60
    group_pad_x, group_pad_y = 36, 54
    group_gap = 36
    row_gap, cell_gap = 34, 52
    max_width = max((node.box[2] for node in diagram.nodes.values()), default=220)
    cell_width = max_width + cell_gap
    max_cols = 4
    canvas_width = int(margin_x * 2 + max_cols * cell_width - cell_gap + group_pad_x * 2)

    y = margin_y
    grouped_nodes = set()
    for group in diagram.groups.values():
        group_nodes = [node_id for node_id in group.nodes if node_id in diagram.nodes]
        grouped_nodes.update(group_nodes)
        group_rows = chunks(group_nodes, max_cols) or [[]]
        row_heights = [max((diagram.nodes[node_id].box[3] for node_id in row), default=0) for row in group_rows]
        group_height = group_pad_y + sum(row_heights) + row_gap * max(0, len(group_rows) - 1) + group_pad_y / 2
        group.box = (margin_x, y, canvas_width - margin_x, y + group_height)

        row_y = y + group_pad_y
        for row, row_height in zip(group_rows, row_heights):
            row_width = len(row) * cell_width - cell_gap
            x = (canvas_width - row_width) / 2
            for index, node_id in enumerate(row):
                node = diagram.nodes[node_id]
                width, height = node.box[2], node.box[3]
                left = x + index * cell_width + (cell_width - cell_gap - width) / 2
                top = row_y + (row_height - height) / 2
                node.box = (left, top, left + width, top + height)
            row_y += row_height + row_gap
        y += group_height + group_gap

    loose_nodes = [node_id for node_id in diagram.nodes if node_id not in grouped_nodes]
    if loose_nodes:
        for row in chunks(loose_nodes, max_cols):
            row_height = max(diagram.nodes[node_id].box[3] for node_id in row)
            row_width = len(row) * cell_width - cell_gap
            x = (canvas_width - row_width) / 2
            for index, node_id in enumerate(row):
                node = diagram.nodes[node_id]
                width, height = node.box[2], node.box[3]
                left = x + index * cell_width + (cell_width - cell_gap - width) / 2
                top = y + (row_height - height) / 2
                node.box = (left, top, left + width, top + height)
            y += row_height + row_gap

    return canvas_width, int(y + margin_y - group_gap)


def draw_centered_text(
    draw: ImageDraw.ImageDraw,
    box: tuple[float, float, float, float],
    lines: list[str],
    used_font: ImageFont.ImageFont,
    fill: str,
) -> None:
    left, top, right, bottom = box
    line_height = 25
    total_height = len(lines) * line_height
    y = top + (bottom - top - total_height) / 2
    for line in lines:
        bbox = draw.textbbox((0, 0), line, font=used_font)
        x = left + (right - left - (bbox[2] - bbox[0])) / 2
        draw.text((x, y), line, fill=fill, font=used_font)
        y += line_height


def draw_node(draw: ImageDraw.ImageDraw, node: Node) -> None:
    box = node.box
    if node.shape == "diamond":
        left, top, right, bottom = box
        points = [
            ((left + right) / 2, top),
            (right, (top + bottom) / 2),
            ((left + right) / 2, bottom),
            (left, (top + bottom) / 2),
        ]
        draw.polygon(points, fill="#FFF7E6", outline="#D97706")
        draw.line(points + [points[0]], fill="#D97706", width=3)
    elif node.shape == "round":
        draw.rounded_rectangle(box, radius=28, fill="#EAF7F0", outline="#15803D", width=3)
    else:
        draw.rounded_rectangle(box, radius=16, fill="#EFF6FF", outline="#2563EB", width=3)
    draw_centered_text(draw, box, node.text_lines, FONT_NODE, "#111827")


def edge_points(source: Node, target: Node) -> list[tuple[float, float]]:
    sx1, sy1, sx2, sy2 = source.box
    tx1, ty1, tx2, ty2 = target.box
    scx, scy = (sx1 + sx2) / 2, (sy1 + sy2) / 2
    tcx, tcy = (tx1 + tx2) / 2, (ty1 + ty2) / 2

    if abs(tcy - scy) >= abs(tcx - scx):
        if tcy >= scy:
            start = (scx, sy2)
            end = (tcx, ty1)
        else:
            start = (scx, sy1)
            end = (tcx, ty2)
        mid_y = (start[1] + end[1]) / 2
        return [start, (start[0], mid_y), (end[0], mid_y), end]

    if tcx >= scx:
        start = (sx2, scy)
        end = (tx1, tcy)
    else:
        start = (sx1, scy)
        end = (tx2, tcy)
    mid_x = (start[0] + end[0]) / 2
    return [start, (mid_x, start[1]), (mid_x, end[1]), end]


def draw_arrow(draw: ImageDraw.ImageDraw, points: list[tuple[float, float]], fill: str = "#4B5563") -> None:
    draw.line(points, fill=fill, width=2, joint="curve")
    if len(points) < 2:
        return
    end = points[-1]
    previous = next((point for point in reversed(points[:-1]) if point != end), points[-2])
    angle = math.atan2(end[1] - previous[1], end[0] - previous[0])
    length = 13
    spread = math.pi / 7
    arrow = [
        end,
        (end[0] - length * math.cos(angle - spread), end[1] - length * math.sin(angle - spread)),
        (end[0] - length * math.cos(angle + spread), end[1] - length * math.sin(angle + spread)),
    ]
    draw.polygon(arrow, fill=fill)


def draw_edge_label(draw: ImageDraw.ImageDraw, points: list[tuple[float, float]], label: str) -> None:
    if not label:
        return
    x = sum(point[0] for point in points) / len(points)
    y = sum(point[1] for point in points) / len(points)
    bbox = draw.textbbox((0, 0), label, font=FONT_EDGE)
    width = bbox[2] - bbox[0]
    height = bbox[3] - bbox[1]
    pad = 6
    label_box = (x - width / 2 - pad, y - height / 2 - pad, x + width / 2 + pad, y + height / 2 + pad)
    draw.rounded_rectangle(label_box, radius=8, fill="#FFFFFF", outline="#E5E7EB")
    draw.text((x - width / 2, y - height / 2 - 1), label, fill="#374151", font=FONT_EDGE)


def render_diagram(block: str, output_path: Path) -> None:
    diagram = parse_mermaid(block)
    scratch = Image.new("RGB", (10, 10), "white")
    scratch_draw = ImageDraw.Draw(scratch)
    prepare_node_sizes(diagram, scratch_draw)

    if diagram.groups:
        width, height = assign_group_layout(diagram)
    else:
        width, height = assign_flow_layout(diagram)

    image = Image.new("RGB", (width, height), "#FFFFFF")
    draw = ImageDraw.Draw(image)

    for group in diagram.groups.values():
        draw.rounded_rectangle(group.box, radius=18, fill="#F9FAFB", outline="#CBD5E1", width=2)
        draw.text((group.box[0] + 22, group.box[1] + 16), group.label, fill="#111827", font=FONT_GROUP)

    for edge in diagram.edges:
        if edge.source not in diagram.nodes or edge.target not in diagram.nodes:
            continue
        points = edge_points(diagram.nodes[edge.source], diagram.nodes[edge.target])
        draw_arrow(draw, points)
        draw_edge_label(draw, points, edge.label)

    for node in diagram.nodes.values():
        draw_node(draw, node)

    output_path.parent.mkdir(parents=True, exist_ok=True)
    image.save(output_path, dpi=(190, 190))


def build_markdown_with_images(markdown_path: Path, build_dir: Path, assets_dir: Path) -> tuple[Path, int]:
    source = markdown_path.read_text(encoding="utf-8")
    count = 0

    def replace(match: re.Match[str]) -> str:
        nonlocal count
        count += 1
        image_path = assets_dir / f"mermaid_{count:02d}.png"
        render_diagram(match.group(1), image_path)
        relative_image = image_path.as_posix()
        return f"\n![Mermaid diagram {count}]({relative_image})\n"

    rendered = MERMAID_RE.sub(replace, source)
    build_dir.mkdir(parents=True, exist_ok=True)
    rendered_markdown = build_dir / f"{markdown_path.stem}.word.md"
    rendered_markdown.write_text(rendered, encoding="utf-8", newline="\n")
    return rendered_markdown, count


def convert(markdown_path: Path, docx_path: Path, build_dir: Path) -> int:
    assets_dir = build_dir / "word_assets" / markdown_path.stem
    rendered_markdown, diagram_count = build_markdown_with_images(markdown_path, build_dir, assets_dir)
    resource_path = f".{';'}{build_dir.as_posix()}{';'}{assets_dir.as_posix()}"
    subprocess.run(
        [
            "pandoc",
            "-f",
            "gfm",
            "-t",
            "docx",
            "--standalone",
            f"--resource-path={resource_path}",
            str(rendered_markdown),
            "-o",
            str(docx_path),
        ],
        check=True,
    )
    return diagram_count


def main() -> None:
    parser = argparse.ArgumentParser(description="Convert Markdown to DOCX and render Mermaid flowcharts as PNG images.")
    parser.add_argument("markdown", type=Path)
    parser.add_argument("docx", type=Path)
    parser.add_argument("--build-dir", type=Path, default=Path("build") / "word")
    args = parser.parse_args()

    diagram_count = convert(args.markdown, args.docx, args.build_dir)
    print(f"Rendered {diagram_count} Mermaid diagram(s) into {args.docx}")


if __name__ == "__main__":
    main()
