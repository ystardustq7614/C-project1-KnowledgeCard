# 测试说明与失败排查指南

本文档对应 `project1` 的当前测试体系。目标是让项目测试不仅能运行，还能被快速理解、定位和维护。

## 1. 推荐运行方式

优先运行总入口：

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File .\tools\run_all_tests.ps1
```

全部通过时，脚本返回退出码 `0`，并输出：

```text
[v1.4.0] All tests passed
```

如果总入口失败，再按下面的测试分层单独运行对应脚本。

## 2. 测试分层

| 层级 | 脚本 | 主要用途 | 是否读写真实 `data/` |
|---|---|---|---|
| 总入口 | `tools/run_all_tests.ps1` | 编译主程序，并串联 CLI、算法、E2E 汇总回归测试 | 否 |
| CLI 数据检查 | `tools/run_cli_checks.ps1` | 验证 `--check-data`、`--fix`、`--user-id`、`--data-dir` 和退出码 | 否 |
| 算法与日期单元测试 | `tools/run_algorithm_tests.ps1` | 验证 `date_utils`、`algo_sm2`、`algo_decay`、`algo_recommend` 纯逻辑 | 否 |
| E2E 总入口 | `tools/run_e2e_tests.ps1` | 串联核心业务、中文输入、用户、卡片、错题、复习/练习/统计/维护分支 | 否 |
| 业务 E2E | `tests/test_e2e_flow.ps1` | 覆盖注册、登录、新增卡片、新增错题、错题转卡片、数据复查 | 否 |
| 中文输入回归 | `tests/test_chinese_input_flow.ps1` | 读取 UTF-8 fixture，验证中文注册、卡片、错题和转卡片内容 | 否 |

## 3. 脚本说明

### 3.1 总入口

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File .\tools\run_all_tests.ps1
```

执行顺序：

1. 清理 `.test_tmp`。
2. 编译主程序。
3. 运行 CLI 数据检查。
4. 运行日期与算法单元测试。
5. 运行 E2E 汇总套件，包含核心业务、中文输入和主要菜单分支。
6. 清理 `.test_tmp`。

常用参数：

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File .\tools\run_all_tests.ps1 -KeepTemp
```

`-KeepTemp` 会保留 `.test_tmp`，用于查看测试数据或输出日志。

### 3.2 CLI 数据检查

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File .\tools\run_cli_checks.ps1
```

覆盖内容：

- 干净数据检查返回 `0`。
- 指定用户检查返回 `0`。
- `PROJECT1_DATA_DIR` 环境变量可指定数据目录。
- 参数错误返回 `2`。
- 不存在用户返回 `2`。
- 失效 `linkedCardId` 检测返回 `1`。
- 失效 `linkedCardId` 自动修复返回 `0`。
- 非法字段检测返回 `1`。
- 非法字段自动修复返回 `0`。

### 3.3 算法与日期单元测试

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File .\tools\run_algorithm_tests.ps1
```

覆盖内容：

- `calculateNextReview()`：忘记、模糊、记牢、掌握度边界、复习间隔递增。
- `date_utils`：日期解析、合法性校验、跨月、跨年、闰年、日期加减和非法日期。
- `calculateDecay()`：未逾期、宽限期、逾期衰减、跨月 / 闰年逾期、掌握度下限、间隔下限。
- `calculateWeakestChapters()`：空输入、章节聚合、Top N、平均掌握度排序、同分题量优先。

该测试会分别编译 `tests/test_date_utils.cpp` 和 `tests/test_algorithms.cpp`，不链接 `main.cpp`，不读写数据文件。

### 3.4 业务 E2E

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File .\tests\test_e2e_flow.ps1
```

覆盖流程：

1. 创建临时空数据目录。
2. 注册 `e2e_user`。
3. 登录 `e2e_user`。
4. 新增一张知识卡片。
5. 新增一道错题。
6. 将错题转换为知识卡片。
7. 查看全部卡片。
8. 退出程序。
9. 检查 `wrongs.txt` 的 `linkedCardId` 指向真实有效卡片。
10. 检查卡片和错题文本字段中的 `|`、换行能正确转义和解码。
11. 执行 `--check-data`，要求返回 `0`。

E2E 主链路仍使用 ASCII 文本，目的是避免 Windows PowerShell 5.1 对无 BOM 脚本中的中文字符串解析不稳定。中文输入回归由独立 fixture 和脚本维护。

### 3.5 中文输入回归

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File .\tests\test_chinese_input_flow.ps1
```

覆盖流程：

1. 从 `tests/fixtures/e2e_inputs/chinese_business_flow.txt` 读取 UTF-8 输入。
2. 注册中文用户名。
3. 登录中文用户。
4. 新增中文知识卡片。
5. 新增中文错题。
6. 将中文错题转换为知识卡片。
7. 从 `tests/fixtures/e2e_inputs/chinese_expected_fields.txt` 读取 UTF-8 预期字段。
8. 读取 `users.txt`、`cards.txt`、`wrongs.txt` 断言中文内容没有乱码。
9. 执行 `--check-data`，要求返回 `0`。

该脚本不在 `.ps1` 源码中硬编码中文断言，中文测试内容全部来自 UTF-8 fixture。

### 3.6 E2E 汇总入口

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File .\tools\run_e2e_tests.ps1
```

覆盖范围：

1. `tests/test_e2e_flow.ps1`：核心业务主链路。
2. `tests/test_chinese_input_flow.ps1`：中文输入回归。
3. `tests/e2e/test_user_flow.ps1`：用户注册、登录、改密、退出与错误登录。
4. `tests/e2e/test_card_flow.ps1`：卡片新增、查看、编辑、查询、分类、排序、多条件和删除。
5. `tests/e2e/test_wrong_flow.ps1`：错题新增、查看、编辑、查询、分类、多条件、转卡片、重复转卡保护和删除。
6. `tests/e2e/test_review_practice_stats_maintenance_flow.ps1`：今日复习、自测练习、统计分析和数据维护分支。

每个 E2E case 都使用独立 `.test_tmp` 子目录和独立 `--data-dir`，并优先通过 `users.txt`、`cards.txt`、`wrongs.txt`、`review_logs.txt` 做结果断言。

## 4. 数据目录隔离

默认数据目录是运行目录下的 `data/`。测试脚本不直接读写真实 `data/`，而是使用 `.test_tmp` 下的临时目录。

程序支持两种方式指定数据目录：

```powershell
$env:PROJECT1_DATA_DIR = ".\.test_tmp\manual_data"
.\project1.exe --check-data
```

```powershell
.\project1.exe --data-dir .\.test_tmp\manual_data --check-data
```

优先级：

1. `--data-dir <path>`
2. `PROJECT1_DATA_DIR`
3. 默认 `data/`

## 5. 退出码

### 主程序 CLI

| 退出码 | 含义 |
|---:|---|
| `0` | 检查通过，或 `--fix` 后已无剩余问题 |
| `1` | 检查发现问题，或仍有需人工处理的问题 |
| `2` | 命令行参数错误 |

### 测试脚本

| 退出码 | 含义 |
|---:|---|
| `0` | 测试通过 |
| 非 `0` | 测试失败，查看最后一个 `START` 阶段或异常信息 |

## 6. 常见失败与排查

### 编译失败

现象：

```text
Build failed with exit code ...
```

排查：

- 确认当前目录是 `project1`。
- 确认 `g++` 可用。
- 手动运行：

```powershell
g++ -std=c++17 -o project1.exe src/*.cpp
```

### PowerShell 禁止运行脚本

现象：

```text
running scripts is disabled on this system
```

处理：

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File .\tools\run_all_tests.ps1
```

### CLI 检查失败

排查：

- 使用 `-KeepTemp` 保留 `.test_tmp`。
- 查看 `.test_tmp\cli_checks\<case>\data` 下的数据文件。
- 单独运行：

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File .\tools\run_cli_checks.ps1 -KeepTemp
```

### 算法测试失败

排查：

- 查看失败断言前缀 `[FAIL]`。
- 对照 `tests/test_algorithms.cpp` 中对应测试函数。
- 如果算法规则被有意调整，需要同步更新测试断言和 README 中的算法说明。

### E2E 流程失败

排查：

- 使用 `-KeepTemp` 保留 `.test_tmp`。
- 查看 `.test_tmp\e2e_flow\e2e_output.log`。
- 查看 `.test_tmp\e2e_flow\case\data\cards.txt` 和 `wrongs.txt`。

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File .\tests\test_e2e_flow.ps1 -KeepTemp
```

### `.test_tmp` 未清理

通常是使用了 `-KeepTemp`，或脚本在清理前异常中断。确认其中没有需要保留的排查数据后，可删除：

```powershell
Remove-Item -LiteralPath .\.test_tmp -Recurse -Force
```

## 7. 当前测试边界

- E2E 已覆盖核心业务、中文输入和主要菜单分支，但不覆盖所有非法输入排列组合。
- 中文自动化输入已进入 E2E 汇总入口，但仍作为独立 fixture 回归脚本维护。
- E2E 已覆盖卡片 / 错题业务文本字段中的 `|` 和多行存储，并补齐卡片 / 错题的主要编辑与删除分支。
- 日期工具已覆盖跨月、跨年和闰年，但 E2E 尚未专门覆盖真实登录后的跨日期衰减数据。

## 8. 维护原则

- 改算法时，先更新 `tests/test_algorithms.cpp`。
- 改数据一致性检查时，先更新 `tests/fixtures/*` 和 `tools/run_cli_checks.ps1`。
- 改菜单流程或输入提示时，检查 `tests/test_e2e_flow.ps1` 和 `tests/e2e/*.ps1` 的输入序列是否仍匹配。
- 新增 E2E 脚本后，先接入 `tools/run_e2e_tests.ps1`，再由 `tools/run_all_tests.ps1` 统一执行。
