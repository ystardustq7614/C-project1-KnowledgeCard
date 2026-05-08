# 知识卡片与错题复习管理系统 v1.3.8

基于 C++17 控制台的学习管理系统，围绕“录入知识 / 错题 -> 复习调度 -> 自测练习 -> 统计反馈 -> 数据维护”形成完整学习闭环。

v1.3.8 在 v1.3.7 测试注释规范落地基础上，移除源码和 C++ 测试中的 `using namespace std;`，头文件不再向全局命名空间引入任何 `std` 名称，`.cpp` 文件改用窄范围 `using std::...` 或显式 `std::`。

---

## 1. 项目定位

这是一个适合 C++ 综合练习的小型单体项目，重点覆盖：

- 结构体建模与多文件模块化
- `vector` 容器管理
- 文本文件持久化
- 用户登录状态与数据隔离
- 控制台菜单交互
- 简化间隔复习算法
- 统计分析与数据维护

项目不依赖数据库、网络服务或第三方库，优先保证可编译、可运行、可迭代。

---

## 2. 当前版本功能概览

| 模块 | v1.3.8 已实现能力 |
| --- | --- |
| 用户管理 | 注册、登录、修改密码、退出登录 |
| 知识卡片 | 新增、修改、逻辑删除、详情查看、关键字查询、分类查看、排序查看、多条件组合查询、全部查看；正面 / 背面支持 `|` 和多行 |
| 错题管理 | 新增、修改、逻辑删除、详情查看、关键字查询、分类查看、多条件组合查询、错题转知识卡片、全部查看；题目 / 答案 / 错因分析支持 `|` 和多行 |
| 今日复习 | 生成今日待复习列表、按优先级排序、卡片 / 错题复习、写入复习日志、查看复习历史 |
| 自测练习 | 随机抽查测试、基于薄弱点推荐的专项突破 |
| 智能推荐 | 按“学科 + 章节”聚合平均掌握度，推荐 Top 3 薄弱知识点 |
| 记忆衰减 | 登录后扫描逾期记录，使用统一日期工具计算逾期天数，自动降低掌握度并缩短复习间隔 |
| 统计分析 | 卡片数、错题数、学科分布、今日待复习、掌握度分布、复习频率、错因分类 |
| 数据维护 | 查看已删除卡片 / 错题、恢复记录、彻底清空当前用户回收站、数据一致性检查 |
| 日期工具 | 统一解析、校验、日期差和日期加减，覆盖跨月、跨年和闰年场景 |
| 终端显示 | 使用 ANSI 颜色与高亮增强主菜单、提示信息和关键状态显示 |
| 持久化 | 启动加载 `data/*.txt`，操作后即时保存，退出前统一保存；文本字段使用百分号转义兼容 `|` 和换行 |
| 中文回归 | 使用 UTF-8 fixture 自动验证中文注册、登录、卡片、错题和错题转卡片内容 |
| E2E 分支覆盖 | 使用 `tools/run_e2e_tests.ps1` 汇总核心业务、中文输入、用户、卡片、错题、复习/练习/统计/维护分支 |

---

## 3. 运行环境

### 基本要求

- C++17 编译器
- Windows PowerShell / CMD，或 Linux / macOS 终端
- Windows 推荐使用 MinGW g++

本项目不需要安装额外依赖。

### 编译

在项目目录执行：

```powershell
cd "e:\VS project\code_c\project1"
g++ -std=c++17 -o project1.exe src/*.cpp
```

如果你使用的是 VS Code，也可以在终端中进入 `code_c/project1` 后执行同样命令。

### 运行

```powershell
.\project1.exe
```

Linux / macOS 可使用：

```bash
g++ -std=c++17 -o project1 src/*.cpp
./project1
```

默认情况下，程序的数据路径是相对运行目录的 `data/`。从 v1.2.7 开始，可以通过环境变量或命令行参数指定数据目录，普通运行不传参数时仍使用默认 `data/`。

### 数据目录配置

默认数据目录：

```powershell
.\project1.exe
```

通过环境变量指定：

```powershell
$env:PROJECT1_DATA_DIR = ".\.test_tmp\manual_data"
.\project1.exe
```

通过命令行参数指定：

```powershell
.\project1.exe --data-dir .\.test_tmp\manual_data
.\project1.exe --data-dir .\.test_tmp\manual_data --check-data
```

命令行参数优先级高于环境变量。该能力主要用于测试数据隔离，避免自动化测试读写真实 `data/`。

### 自动化输入与中文用户名

Windows PowerShell 通过管道向原生 `.exe` 传递中文时，必须显式设置 `$OutputEncoding` 为 UTF-8，否则中文用户名可能在程序内变成乱码，导致登录失败。

项目提供了一个小型运行脚本：

```powershell
.\tools\run_utf8_automation.ps1 `
  -ExePath .\project1.exe `
  -InputFile .\tools\sample_login_zh_input.txt
```

如果系统执行策略禁止直接运行 `.ps1`，使用：

```powershell
powershell -NoProfile -ExecutionPolicy Bypass `
  -File .\tools\run_utf8_automation.ps1 `
  -ExePath .\project1.exe `
  -InputFile .\tools\sample_login_zh_input.txt
```

脚本会设置 `InputEncoding / OutputEncoding / $OutputEncoding` 为 UTF-8，再把 UTF-8 输入文件送入程序。自动化输入文件需要为每个“按回车继续”保留空行。

### 非交互数据检查

自动化测试可以直接调用命令行参数，不需要登录和手动选择菜单：

```powershell
.\project1.exe --check-data
.\project1.exe --check-data --fix
.\project1.exe --check-data --user-id 1
.\project1.exe --data-dir .\.test_tmp\case_data --check-data
```

默认检查全部用户；`--user-id <id>` 可限定单个用户；`--fix` 会自动修复失效错题关联、掌握度 / 难度 / 日期等可归正字段。退出码约定：

- `0`：检查通过，或 `--fix` 后已无剩余问题。
- `1`：发现问题，或仍有需人工处理的问题。
- `2`：命令行参数错误。

### CLI 检查脚本

v1.2.4 新增隔离式 CLI 检查脚本：

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File .\tools\run_cli_checks.ps1
```

脚本会：

- 编译主程序。
- 在 `.test_tmp/cli_checks` 下创建临时运行目录。
- 复制 `tests/fixtures` 中的测试数据。
- 通过 `--data-dir` 显式指定每个 case 的测试数据目录。
- 验证 `PROJECT1_DATA_DIR` 环境变量可被程序读取。
- 验证 `--check-data`、`--check-data --fix`、`--user-id` 和参数错误退出码。
- 验证失效 `linkedCardId` 能被修复为 `-1`。
- 测试结束后清理 `.test_tmp`，不读写真实 `data/`。

### 算法与日期单元测试

当前算法测试入口同时覆盖日期工具和算法模块：

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File .\tools\run_algorithm_tests.ps1
```

脚本会：

- 编译 `tests/test_algorithms.cpp` 和 `src/algo_*.cpp`。
- 验证 `date_utils` 的日期解析、日期合法性、跨月、跨年、闰年、日期加减和非法输入。
- 验证 `algo_sm2` 的掌握度、复习间隔和连续答对次数更新。
- 验证 `algo_decay` 的宽限期、逾期衰减、跨月 / 闰年逾期、掌握度下限和间隔下限。
- 验证 `algo_recommend` 的章节聚合、Top N 截断、空输入和同分排序。
- 在 `.test_tmp/algorithm_tests` 下生成临时测试程序，结束后自动清理。
- 不读取或写入 `data/`。

### 业务流程回归测试

v1.2.6 新增隔离式 E2E 业务流程测试：

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File .\tests\test_e2e_flow.ps1
```

脚本会：

- 编译主程序。
- 在 `.test_tmp/e2e_flow` 下创建空数据目录，从零启动业务流程。
- 通过 `--data-dir` 显式指定本轮 E2E 测试数据目录。
- 自动完成注册、登录、新增知识卡片、新增错题、错题转知识卡片、查看全部卡片、退出。
- 检查 `cards.txt` 中存在手动新增卡片和转换生成的卡片。
- 检查 `wrongs.txt` 中的 `linkedCardId` 指向真实有效卡片。
- 执行 `--check-data` 并要求退出码为 `0`。
- 测试结束后清理 `.test_tmp`，不读写真实 `data/`。

v1.3.5 新增 E2E 汇总入口：

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File .\tools\run_e2e_tests.ps1
```

该入口会串联核心业务主链路、中文输入回归、用户分支、卡片分支、错题分支、复习/练习/统计/维护分支。每个 case 使用独立 `--data-dir`，并通过数据文件断言验证结果。

### 一键测试总入口

v1.2.8 新增总测试入口：

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File .\tools\run_all_tests.ps1
```

脚本会：

- 清理 `.test_tmp`。
- 编译主程序。
- 运行 CLI 数据一致性检查脚本。
- 运行算法单元测试脚本。
- 运行 E2E 汇总回归测试脚本。
- 任一步失败时返回非 `0`，全部通过时返回 `0`。
- 默认在结束后清理 `.test_tmp`。

v1.2.9 新增测试文档与失败排查指南：

```text
docs/testing.md
```

建议先运行总入口；如果失败，再按 `docs/testing.md` 中的分层说明单独排查 CLI、算法或 E2E 测试。

---

## 4. 快速体验流程

1. 启动程序。
2. 注册一个用户并登录。
3. 新增几张知识卡片。
4. 新增几条错题，并选择错因类型。
5. 在“错题管理”中将某条错题转换为知识卡片。
6. 进入“今日复习”，查看待复习列表并完成复习。
7. 进入“自测练习中心”，进行随机抽查或薄弱点专项练习。
8. 进入“统计分析”，查看学科分布、掌握度和错因统计。
9. 删除一条记录后进入“数据维护”，测试恢复或彻底清空回收站。
10. 退出后重新运行程序，验证数据仍然存在。

---

## 5. 主菜单结构

登录后主菜单：

```text
1. 知识卡片管理
2. 错题管理
3. 今日复习
4. 自测练习中心
5. 统计分析
6. 数据维护
7. 修改密码
0. 退出登录
```

### 知识卡片管理

```text
1. 新增卡片
2. 修改卡片
3. 删除卡片
4. 查看最近列表详情
5. 按关键字查询
6. 分类查看
7. 排序查看卡片
8. 多条件组合查询
9. 查看全部卡片
0. 返回主菜单
```

### 错题管理

```text
1. 记录错题
2. 修改错题
3. 删除错题
4. 查看最近列表详情
5. 按关键字查询
6. 分类查看
7. 多条件组合查询
8. 错题转知识卡片
9. 查看全部错题
0. 返回主菜单
```

### 今日复习

```text
1. 查看今日待复习列表
2. 开始复习
3. 查看复习历史
0. 返回主菜单
```

### 自测练习中心

```text
1. 随机抽查测试
2. 薄弱点专项突破
0. 返回主菜单
```

### 统计分析

```text
1. 卡片总数统计
2. 错题总数统计
3. 各学科分布统计
4. 今日待复习统计
5. 掌握情况统计
6. 复习频率统计
7. 错因分类统计
0. 返回主菜单
```

### 数据维护

```text
1. 查看已删除卡片
2. 恢复知识卡片
3. 查看已删除错题
4. 恢复错题
5. 彻底清空回收站
6. 数据一致性检查
0. 返回主菜单
```

---

## 6. 项目结构

```text
project1/
├── README.md
├── compile_flags.txt
├── data/
│   ├── users.txt
│   ├── cards.txt
│   ├── wrongs.txt
│   └── review_logs.txt
├── docs/
│   ├── testing.md
│   ├── Development Log/
│   │   ├── v1.1/              # 阶段 C~F 开发日志
│   │   ├── v1.2业务端/         # 阶段 A~E 业务端开发日志
│   │   ├── v1.2测试端/         # v1.2.1~v1.2.9 测试端开发日志
│   │   └── v1.3/              # v1.3.0~v1.3.8 迭代报告
│   └── Project Proposal/
│       ├── 知识卡片与错题复习管理系统方案.md
│       ├── v1.0版本总方案.md
│       ├── v1.0计划.md
│       ├── 技术实现相关(v1.0).md
│       ├── v1.1迭代方案.md
│       ├── V1.1AI迭代方案审阅报告.md
│       ├── v1.2业务端后续迭代方案.md
│       ├── v1.2演示脚本.md
│       ├── v1.3后续迭代方案.md
│       └── 问题日志.md
├── tests/
│   ├── test_algorithms.cpp
│   ├── test_date_utils.cpp
│   ├── test_e2e_flow.ps1
│   ├── test_chinese_input_flow.ps1
│   ├── e2e/
│   │   ├── common.ps1
│   │   ├── test_card_flow.ps1
│   │   ├── test_review_practice_stats_maintenance_flow.ps1
│   │   ├── test_user_flow.ps1
│   │   └── test_wrong_flow.ps1
│   └── fixtures/
│       ├── broken_link_data/
│       ├── clean_data/
│       ├── e2e_inputs/
│       └── invalid_field_data/
├── tools/
│   ├── run_all_tests.ps1
│   ├── run_algorithm_tests.ps1
│   ├── run_cli_checks.ps1
│   ├── run_e2e_tests.ps1
│   ├── run_utf8_automation.ps1
│   └── sample_login_zh_input.txt
└── src/
    ├── main.cpp              # 程序入口、一级菜单、登录后主菜单、衰减扫描与推荐展示
    ├── models.h              # User、Card、WrongQuestion、ReviewLog、ReviewTask 数据结构
    ├── globals.h             # 全局容器与当前登录用户状态
    ├── utils.h / utils.cpp   # 字符串、输入解析、清屏、暂停、ANSI 支持和日期兼容包装
    ├── date_utils.h / date_utils.cpp
    │                         # 统一日期解析、校验、日期差和日期加减
    ├── storage.h / storage.cpp
    │                         # data 目录初始化、文本文件读写、编号生成
    ├── user.h / user.cpp     # 注册、登录、改密、退出登录
    ├── card.h / card.cpp     # 知识卡片 CRUD、查询、筛选、排序、组合查询
    ├── wrong.h / wrong.cpp   # 错题 CRUD、错因分类、组合查询、错题转卡片
    ├── review.h / review.cpp # 今日复习任务、复习流程、复习日志
    ├── stats.h / stats.cpp   # 统计分析
    ├── maintenance.h / maintenance.cpp
    │                         # 已删除记录查看、恢复、物理删除
    ├── practice.h / practice.cpp
    │                         # 随机自测与薄弱点专项练习
    ├── algo_sm2.h / algo_sm2.cpp
    │                         # 简化 SM-2 复习间隔算法
    ├── algo_decay.h / algo_decay.cpp
    │                         # 记忆衰减算法
    └── algo_recommend.h / algo_recommend.cpp
                              # 薄弱知识点推荐算法
```

说明：v1.2 计划中曾提出独立 `tui.cpp`，当前实际实现把 ANSI 颜色宏和 `initTUI()` 放在 `utils.h / utils.cpp` 中，没有单独拆出 `tui` 模块。方案与计划文档位于 `docs/Project Proposal/`，开发日志与迭代报告位于 `docs/Development Log/`。

---

## 7. 核心数据模型

### User

```text
userId | username | password | createDate
```

### Card

```text
cardId | userId | subject | chapter | title | front | back | tags |
difficulty | mastery | reviewCount | correctStreak | intervalDays |
createDate | lastReviewDate | nextReviewDate | active
```

### WrongQuestion

```text
wrongId | userId | subject | chapter | question | correctAnswer |
wrongAnswer | reason | errorType | linkedCardId | mastery |
reviewCount | correctStreak | intervalDays | createDate |
lastReviewDate | nextReviewDate | active
```

`errorType` 是 v1.1 引入的错因类型字段，常见值包括：

- 概念不清
- 记忆错误
- 粗心
- 审题失误
- 计算错误
- 方法不会

`linkedCardId` 用于记录“错题转知识卡片”后生成的卡片编号，默认值为 `-1`。

### ReviewLog

```text
logId | userId | itemId | itemType | reviewDate | result |
oldInterval | newInterval | oldMastery | newMastery
```

---

## 8. 持久化规则

默认情况下，所有数据存储在 `data/` 目录下；如果设置了 `PROJECT1_DATA_DIR` 或传入 `--data-dir`，则读写指定目录下的同名数据文件：

| 文件 | 用途 |
| --- | --- |
| `users.txt` | 用户账号数据 |
| `cards.txt` | 知识卡片数据 |
| `wrongs.txt` | 错题数据 |
| `review_logs.txt` | 复习日志 |

存储格式规则：

- 一行一条记录。
- 字段使用 `|` 分隔。
- 字符串字段写入前会做百分号转义，读取时自动解码。
- 新数据可以保存 `|`、`\r`、`\n` 和 `%`。
- 旧数据没有转义时仍按原样读取。
- 卡片正面 / 背面、错题题目 / 正确答案 / 错误答案 / 错因分析支持多行文本。
- 多行输入方式：在对应字段先输入 `.multi`，随后输入多行内容，最后单独输入 `.end` 结束。
- `active = 1` 表示有效，`active = 0` 表示逻辑删除。
- `wrongs.txt` 兼容 v1 旧格式：如果旧数据缺少 `errorType`，读取时会默认置空。

转义规则：

| 原字符 | 存储形式 |
| --- | --- |
| `%` | `%25` |
| `\|` | `%7C` |
| `\r` | `%0D` |
| `\n` | `%0A` |

---

## 9. 关键设计决策

### 9.1 多用户数据隔离

系统使用 `currentUserId` 判断当前登录用户。卡片、错题、日志等业务数据都通过 `userId` 过滤，避免不同用户的数据混在一起。

### 9.2 逻辑删除 + 物理删除

普通删除不会立即移除文件记录，而是把 `active` 置为 `false`。这样可以在“数据维护”中恢复。

v1.2 新增“彻底清空回收站”，会把当前用户已逻辑删除的卡片和错题从内存与文件中真正移除。

v1.2.2 新增“数据一致性检查”，用于检查当前用户错题关联的卡片是否真实存在且可见，并检查掌握度、难度、复习间隔和日期字段是否在合理范围内。失效的错题关联会被重置为 `-1`，这样用户可以重新执行“错题转知识卡片”生成可见卡片。

### 9.3 表现层序号映射

卡片和错题列表对用户显示连续序号 `[1] [2] [3]`，用户按序号操作。底层真实 `cardId / wrongId` 仍保留在文件中，但不要求用户直接输入全局编号。

### 9.4 算法层解耦

v1.2 把复习算法、衰减算法、推荐算法拆到 `algo_` 文件中：

- `algo_sm2`：只计算一次复习后的新掌握度、新间隔、连续答对次数。
- `algo_decay`：只计算逾期记录是否衰减，以及衰减后的掌握度 / 间隔。
- `algo_recommend`：只按输入数据聚合薄弱章节。
- `date_utils`：统一提供日期解析、日期合法性、日期差和日期加减；`utils` 只保留旧函数名包装，避免业务层大面积改动。

算法层不直接保存文件，也不直接维护登录状态。

---

## 10. 复习与推荐算法

### 10.1 今日复习任务生成

进入今日复习时，系统扫描当前用户所有有效卡片和错题：

```text
nextReviewDate <= 今天
```

符合条件的记录会进入待复习列表，并按优先级排序。

优先级公式：

```text
priority = overdueDays * 10 + wrongBonus + (100 - mastery)
```

规则：

- 逾期越久，优先级越高。
- 掌握度越低，优先级越高。
- 错题固定获得 `wrongBonus = 20`。
- 优先级相同则到期日期更早的排在前面。

### 10.2 v1.2 简化 SM-2

复习反馈从 v1 的 `0 / 1 / 2` 调整为更直观的三档：

```text
1 = 忘记
2 = 模糊
3 = 记牢
```

更新规则：

| 结果 | 掌握度 | 间隔 | 连续答对 |
| --- | --- | --- | --- |
| 忘记 | `-20`，最低 0 | 重置为 1 天 | 清零 |
| 模糊 | `+5`，最高 100 | 保持原间隔，至少 1 天 | 清零 |
| 记牢 | `+20`，最高 100 | `1 -> 3 -> 7 -> 15 -> 后续翻倍` | `+1` |

复习完成后还会更新：

- `reviewCount += 1`
- `lastReviewDate = 今天`
- `nextReviewDate = 今天 + intervalDays`
- 写入 `review_logs.txt`

### 10.3 记忆衰减

登录成功后，系统会扫描当前用户的有效卡片和错题。如果记录距离 `nextReviewDate` 已经逾期至少 3 天，则触发衰减。

当前实现规则：

- 每逾期 1 天扣 2 点掌握度。
- 掌握度最低降到 30。
- 间隔缩短为原来的一半，最低 1 天。
- 衰减后立即保存 `cards.txt` 和 `wrongs.txt`。
- 逾期天数通过 `date_utils` 按真实公历日期差计算，覆盖跨月、跨年和闰年。

### 10.4 薄弱知识点推荐

系统把当前用户的有效卡片和错题按：

```text
学科 + 章节
```

聚合，计算平均掌握度，并在主菜单显示平均掌握度最低的 Top 3 章节。

自测练习中心的“薄弱点专项突破”也复用这套推荐算法。

---

## 11. 自测练习中心

自测练习与“今日复习”不同，它不改变掌握度和复习计划，主要用于主动测试。

### 随机抽查测试

- 从当前用户有效知识卡片中随机抽取 N 张。
- 默认抽取 10 张。
- 先显示题目，按回车后显示答案。
- 用户自评是否答对。
- 最后输出正确率。
- 不写入复习日志，不修改 `mastery`。

### 薄弱点专项突破

- 自动找出当前最薄弱的“学科 + 章节”。
- 从该章节下抽取有效知识卡片。
- 最多练习 15 张。
- 输出专项练习正确率。
- 如果薄弱点下没有卡片，会提示先将相关错题转成知识卡片。

---

## 12. 查询与排序能力

### 知识卡片

支持：

- 按关键字查询
- 按学科 / 章节 / 标签分类查看
- 按创建时间排序
- 按下次复习时间排序
- 按掌握度排序
- 多条件组合查询
- 查看全部卡片

### 错题

支持：

- 按关键字查询
- 按学科 / 章节分类查看
- 多条件组合查询
- 查看全部错题
- 错题转知识卡片

多条件组合查询采用“回车跳过”的方式输入条件，只返回同时满足所有已填写条件的记录。

---

## 13. 版本演进摘要

### v1.0：最小可用闭环

确立项目骨架，跑通"录入 → 复习 → 持久化"全链路：

- 多用户注册 / 登录 / 改密，通过 `userId` 实现数据隔离
- 知识卡片与错题的完整 CRUD（逻辑删除，`active` 置 0）
- 今日复习模块：按逾期天数 + 掌握度 + 错题加权的优先级排序调度
- 复习反馈（0/1/2）驱动固定间隔更新掌握度与下次复习日期
- 复习日志持久化到 `review_logs.txt`
- 基础统计：卡片/错题总数、掌握度分布、学科分布、近 7 天复习频率
- 管道分隔的文本文件持久化（`users.txt`、`cards.txt`、`wrongs.txt`、`review_logs.txt`）
- 模块骨架确立：`models.h` → `globals.h` → `storage` → `user` → `card` → `wrong` → `review` → `stats` → `main`

### v1.1：连通错题与卡片

打通错题与卡片之间的联动，建立逻辑删除的配套管理：

- 错因分类字段 `errorType`（概念不清 / 记忆错误 / 粗心 / 审题失误 / 计算错误 / 方法不会）及错因分布统计
- 错题转知识卡片：一键将错题生成为新卡片，`linkedCardId` 防重复转换
- 卡片按学科/章节/标签筛选，按创建时间/下次复习时间/掌握度排序
- 数据维护模块 `maintenance`：查看已逻辑删除的记录并恢复
- UI 表现层序号映射：用户操作 `[1] [2] [3]` 序号，不再直接接触底层 `cardId` / `wrongId`
- `wrongs.txt` 格式升级：新增第 9 字段 `errorType`，兼容旧格式读取

### v1.2：算法解耦与练习闭环

从"电子记录本"升级为"有智能调度能力的学习引擎"：

- 算法层解耦：抽出 `algo_sm2`（简化为 1=忘记 / 2=模糊 / 3=记牢）、`algo_decay`（逾期衰减：每天扣 2 分，下限 30，间隔减半，下限 1 天）、`algo_recommend`（按学科+章节聚合薄弱 Top 3），纯函数无副作用
- 登录时自动衰减扫描，逾期 ≥3 天的记录自动调整掌握度与间隔
- 自测练习中心：随机抽查（不影响掌握度）+ 薄弱点专项突破（复用推荐算法）
- 多条件组合查询（向导式，回车跳过）
- 回收站物理清空（`erase-remove_if` + 覆写文件）
- ANSI 颜色与高亮增强终端显示
- 测试基础设施起步：`--data-dir` 数据隔离、CLI 数据一致性检查（`--check-data` / `--fix`）、算法单元测试、核心业务 E2E 回归、一键总测试入口 `run_all_tests.ps1`

### v1.3：质量基础与测试覆盖

不新增业务功能，专注夯实质量根基：

- 命名标准化：编译产物统一为 `project1.exe`，所有脚本和文档同步
- 日期工具收敛：新增 `date_utils` 模块，以公历日期序号算法替换 `algo_decay` 中的近似日期差，覆盖跨月/跨年/闰年/非法日期
- 文本存储增强：百分号编码（`%25` / `%7C` / `%0D` / `%0A`）兼容 `|` 和换行，支持 `.multi` / `.end` 多行文本输入，读取时兼容旧未编码数据
- 中文输入回归：UTF-8 fixture 驱动的中文注册/登录/卡片/错题/转卡片自动化测试，避免 `.ps1` 硬编码中文
- E2E 分支覆盖扩展：从单一主链路扩展为 `e2e/` 目录下用户/卡片/错题/复习练习统计维护五个分支脚本，`run_e2e_tests.ps1` 统一汇总入口
- 所有测试通过 `--data-dir` 隔离，不读写真实 `data/`
- 注释规范落地：按“职责、约束、边界、副作用”补齐头文件和关键实现注释，修正头文件中失效/不一致的公共声明
- 测试注释规范落地：测试入口、E2E helper、业务分支脚本和 C++ 单元测试均说明测试职责、隔离边界、输入脚本约束和断言口径
- 命名空间治理：源码和 C++ 测试不再使用 `using namespace std;`，头文件只暴露 `std::` 限定类型，降低全局命名污染和符号冲突风险

---

## 14. 当前验收标准

当前版本达到以下条件即可认为 v1.3.8 命名空间治理迭代完成：

- 可以注册并登录用户。
- 不同用户只能看到和操作自己的卡片、错题、日志。
- 可以新增、修改、删除、查询卡片和错题。
- 可以将错题转换为知识卡片，且不能重复转换同一错题。
- 可以生成今日待复习列表并完成复习。
- 复习后掌握度、间隔、下次复习日期和复习日志会更新。
- 登录后会触发记忆衰减扫描。
- 主菜单能显示薄弱知识点推荐。
- 可以进行随机自测和薄弱点专项练习。
- 可以查看统计分析。
- 可以恢复逻辑删除数据，也可以物理清空当前用户回收站。
- 退出并重新启动后，数据仍可正确加载。
- 可以通过 `tools/run_all_tests.ps1` 一键完成编译、CLI、算法、E2E 验证。
- 可以通过 `tools/run_e2e_tests.ps1` 单独运行核心业务、中文输入和主要菜单分支 E2E。
- 测试数据通过 `--data-dir` 与真实 `data/` 隔离。
- README、`docs/testing.md` 和 v1.3.0 闭环报告说明运行与验证方式。
- 当前推荐命令统一使用 `project1.exe`。
- 总测试入口默认编译并运行 `project1.exe`。
- `src/main.cpp --help` 不再输出历史 exe 名称。
- 日期工具可以正确处理同日、跨月、跨年、闰年和非法日期。
- `algo_decay` 不再使用近似日期差。
- 日期测试、算法测试和总测试入口均通过。
- 卡片正面 / 背面可以保存并恢复 `|` 和多行内容。
- 错题题目 / 答案 / 错因分析可以保存并恢复 `|` 和多行内容。
- 旧 fixture 数据仍能通过 `--check-data`。
- 中文注册 / 登录 / 新增卡片 / 新增错题 / 错题转卡片流程进入总测试入口。
- 中文用户名、卡片、错题和转卡片内容可从数据文件中正确读取和断言。
- 公共头文件说明模块职责、关键约束、输入输出和副作用。
- 存储、日期、算法、复习、维护等关键实现说明“为什么这样做”和误改风险，而不是逐行翻译代码。
- `tools/run_all_tests.ps1` 可以完整运行到结束，测试 helper 命名保持一致。
- 测试脚本说明临时目录、`--data-dir`、UTF-8 输入、fixture 和文件断言的契约。
- C++ 单元测试说明算法/日期测试目标、边界覆盖范围和不依赖全局状态的约束。
- `src` 与 C++ 测试中不存在 `using namespace std;`。
- 头文件中不存在 `using std::...` 或其他把 `std` 名称引入全局命名空间的写法。
- 主程序、算法测试、日期测试和 E2E 总入口均可通过编译与回归。

---

## 15. 已知限制

- 仅支持控制台交互，没有图形界面。
- 密码明文存储，不适合真实账号系统。
- 多行输入需要显式使用 `.multi` / `.end`，普通字段仍保持单行。
- 数据文件没有并发写入保护。
- 已有一键总测试入口和主要菜单分支 E2E，但仍不覆盖所有非法输入排列组合。
- 自测练习不影响掌握度和复习日志，只作为主动练习统计。
- 当前仍是本地单机文本存储，不支持云同步。
- ANSI 颜色在部分旧版 Windows 终端中可能显示异常。

---

## 16. 后续迭代建议

优先级从高到低：

1. 进入 v1.4.0：汇总 v1.3.1 ~ v1.3.8，形成稳定闭环版本。
2. 后续再评估数据备份 / 导入 / 导出能力。

---

## 17. 一句话总结

`project1` 已经从基础信息管理系统迭代为一个具备复习调度、错题联动、薄弱点推荐、自测练习、统计反馈、数据维护和自动化测试闭环的本地控制台学习系统。它仍保持轻量、单体、文本持久化的实现方式，适合作为 C++ 多文件项目和学习管理业务逻辑的综合练习。
