# libslic3r config/profile Fast Gate 设计

日期：2026-06-05

## 1. 背景

当前 `BambuStudio` 的 Windows PR 门禁已经具备基础 smoke 能力，但 `libslic3r` 仍然是一个超大单体库。现有 `tests/libslic3r/CMakeLists.txt` 将多类测试编译进同一个 `libslic3r_tests` 可执行文件，再统一链接 `libslic3r`。结果是：

- `test_config.cpp` 这类逻辑较轻的测试，也要为重型构建图付费。
- PR 门禁难以做到“只为改动模块付费”。
- 社区贡献者提交 PR 后，反馈时延容易被大型 Windows C++ 构建拖长。

本设计聚焦 `profile/config` 这条线，先做一个可量化、可复制的试点。

## 2. 目标

本试点有两个并行目标：

1. 证明“小测试 target”在 `profile/config` 场景下能明显降低 PR 门禁耗时。
2. 沉淀一套可复制的 `libslic3r` 可测试性拆分方法，后续扩展到 `placeholder parser`、`geometry`、`gcodewriter` 等模块。

## 3. 非目标

本阶段不做以下事情：

- 不重写 `DynamicPrintConfig`、`PrintConfig` 的功能行为。
- 不一次性重构整个 `libslic3r` 的库边界。
- 不把重型集成回归挪进 PR Fast Gate。
- 不依赖 `self-hosted runner` 作为社区 PR 的前提条件。

## 4. 当前问题定义

### 4.1 构建问题

现有 `libslic3r_tests` 将 `test_config.cpp`、几何、mesh、3mf、placeholder parser 等测试放在同一 target 中。即使某次 PR 只改动 `PrintConfig` 相关逻辑，也可能触发重型依赖的构建路径。

### 4.2 门禁问题

当前门禁更像“整组 smoke 构建”，而不是“按影响范围选择测试 target”。这不利于社区协作场景中的快速反馈。

### 4.3 架构问题

`libslic3r` 并非不可测试，而是当前可测试边界过厚。很多纯配置逻辑没有被放在一个可独立构建、可独立运行、可单独触发的轻量边界内。

## 5. 现有证据

基于已完成的 Windows GitHub Actions 试验：

- `PCH=ON` 时，`Build libslic3r config smoke target` 约 47 分钟。
- `PCH=OFF` 时，同一步骤约 91 分钟。
- 结论：不能通过关闭 PCH 换取更高 `sccache` 命中来优化这条 PR lane。

这说明当前更重要的方向不是继续抠编译器参数，而是：

1. 让轻逻辑脱离重 target。
2. 让 PR 只跑与改动模块匹配的 target。

## 6. 候选方案与选型

### 方案 A：只拆测试 target

- 新建 `libslic3r_config_tests`
- 仍链接完整 `libslic3r`

优点：改动最小。  
缺点：编译收益可能有限。

### 方案 B：直接抽 `libslic3r_config_core`

- 抽出 `PrintConfig` / `Config` 相关轻量子库
- `libslic3r_config_tests` 只链接该子库

优点：最有机会获得明显构建收益。  
缺点：第一阶段工程改动偏大。

### 方案 C：白名单轻量 target 过渡方案

- 不立刻正式重构 `libslic3r`
- 先按源文件白名单拼出 `config/profile` 的轻量构建目标
- 用真实构建数据验证边界是否足够轻

优点：最快得到证据；风险低于直接抽核心子库。  
缺点：属于过渡结构，验证成功后应收敛到正式子库。

### 选型

采用 **方案 C 作为第一阶段**，验证通过后收敛到 **方案 B**。

## 7. 选定设计

### 7.1 新增目标

新增一个轻量测试目标：

- `libslic3r_config_tests`

第一阶段它只承接 `tests/libslic3r/test_config.cpp`。

### 7.2 第一阶段纳入范围

轻量 target 优先纳入：

- `PrintConfig.cpp/.hpp`
- `Config.cpp/.hpp`
- `LocalesUtils.cpp/.hpp`
- `test_config.cpp`
- 为成功编译 `test_config.cpp` 所需的最小依赖集合

第一阶段暂不纳入：

- `GCode*`
- mesh / polygon / geometry 重型路径
- 3mf / amf / step 等格式链路
- GUI / slicing 主流程 glue

### 7.3 与现有测试目标的关系

- 保留现有 `libslic3r_tests`
- 不在第一阶段删除旧 target
- 新目标先作为独立试点存在

这样可以避免一次性破坏现有测试面，并允许直接对比新旧目标的构建成本。

### 7.4 行为边界

第一阶段只做“构建边界拆薄”，不做功能改写：

- 不改变 `DynamicPrintConfig` 的外部行为
- 不修改现有配置语义
- 不变更现有测试断言语义

## 8. PR 门禁设计

### 8.1 新增 Fast Gate

在 Windows PR lane 中新增 `config/profile fast gate`，执行：

1. configure `libslic3r_config_tests`
2. build `libslic3r_config_tests`
3. run `libslic3r_config_tests "[Config]"`

### 8.2 触发策略

采用 **路径匹配 + 公共依赖兜底** 的触发策略，而不是纯路径硬匹配。

#### 直接触发路径

当 PR 改动以下内容时，触发 `config/profile fast gate`：

- `src/libslic3r/PrintConfig.*`
- `src/libslic3r/Config.*`
- `src/libslic3r/LocalesUtils.*`
- `tests/libslic3r/test_config.cpp`
- 该轻量 target 的 CMake 文件

#### 公共依赖兜底

当 PR 改动以下公共边界时，也触发该 gate：

- `src/libslic3r/libslic3r.h`
- `src/libslic3r/pchheader.*`
- `src/libslic3r/CommonDefs.hpp`
- `src/libslic3r` 下被 `PrintConfig` 直接包含的共享头文件
- 顶层或相关目录 `CMakeLists.txt`

目的不是做到 100% 精准静态影响分析，而是先避免因公共头文件改动造成漏测。

## 9. 量化指标

试点必须输出以下指标：

- configure duration
- build duration
- test duration
- total gate duration
- target binary size 或 compiled object count

并与当前 Windows smoke 基线对比：

- `libslic3r_config_smoke`
- 整个 `Cpp UT Smoke` job 总时长

## 10. 验收标准

试点通过的最低标准：

1. `libslic3r_config_tests` 可单独 configure / build / run。
2. `[Config]` 用例可稳定执行，不改变既有行为。
3. Windows CI 中可单独观测其耗时。
4. 相比当前 `libslic3r_config_smoke`，构建与执行成本有明确下降。
5. 形成一份后续模块拆分清单。

建议的收益判断：

- 若 `build + test` 可压到 10~15 分钟，证明方案强烈值得继续扩展。
- 若仅压到 20~30 分钟，说明还需要继续切薄依赖边界。
- 若几乎无下降，说明 `PrintConfig` 仍深度绑定于重路径，下一阶段应先继续解耦而不是盲目扩 UT。

## 11. 风险与应对

### 风险 1：`PrintConfig` 的隐式依赖比预期大

应对：

- 先通过白名单 target 暴露真实依赖图
- 以“最小能编过”为准逐步补依赖，不先做大重构

### 风险 2：第一版目标结构偏临时

应对：

- 明确方案 C 只是过渡方案
- 一旦收益明确，进入第二阶段收敛为 `libslic3r_config_core`

### 风险 3：路径规则过窄导致漏测

应对：

- 第一版采用“路径匹配 + 公共依赖兜底”
- 后续根据实际 PR 样本再微调触发规则

## 12. 后续扩展路线

若 `config/profile` 试点成功，后续按同一模式扩展：

- `placeholder parser` -> `libslic3r_placeholder_tests`
- `geometry small` -> `libslic3r_geometry_tests`
- `gcodewriter` -> `fff_gcodewriter_tests`

扩展原则：

1. 先抽出轻逻辑边界
2. 再建立独立测试 target
3. 最后接入按改动范围选择性触发

## 13. 实施顺序

1. 新建 `libslic3r_config_tests` 试点 target
2. 使其在本地和 Windows CI 可单独运行
3. 记录对比数据
4. 加入按路径选择性触发
5. 根据依赖图收敛为正式 `libslic3r_config_core`

## 14. 结论

本设计不是单纯“加一个测试”，而是建立一套社区友好的 PR 质量门禁策略：

- 轻逻辑拆成轻 target
- PR 只为改动模块付费
- 重型回归留在更高层级

`config/profile` 是这套策略的第一个试点模块，因为它最容易同时验证：

- 速度收益
- 门禁可行性
- 可测试架构拆分方法
