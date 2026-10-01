# FilamentGroup 回归测试

验证 `FilamentGroup` 分组算法的正确性和性能不退化。

## 目录结构

```
tests/filament_group/
├── CMakeLists.txt                        构建配置
├── filament_group_regression_main.cpp    Catch2 测试主程序（golden + property 两层）
├── fg_test_evaluator.hpp                 评估器：运行算法、计算 flush/time/score、检查约束
├── fg_test_serialization.hpp             FilamentGroupContext 的 JSON 序列化/反序列化
├── fg_test_utils.hpp                     测试数据生成工具（随机 flush matrix、layer filaments、机器配置等）
├── test_data_generator.cpp              独立可执行程序：批量生成 500 个测试用例到 data/
├── golden/                               精选的 27 个 golden case（入 git）
│   ├── config_a/                         2-extruder × 2-nozzle
│   ├── config_b/                         2-extruder × 1+K-nozzle（选料器）
│   └── config_c/                         1-extruder × K-nozzle
└── REGRESSION_TEST_PLAN.md              完整测试方案文档
```

## 文件说明

| 文件 | 作用 |
|------|------|
| `filament_group_regression_main.cpp` | 定义三个 TEST_CASE：`[golden]` 对比基准分数、`[property]` 属性断言、`[update-golden]` 刷新基准 |
| `fg_test_evaluator.hpp` | `run_and_evaluate()` 运行分组算法；`full_evaluate_map()` 完整计算 flush + change_time + score；`check_constraints()` 独立约束验证 |
| `fg_test_serialization.hpp` | 所有数据结构的 nlohmann::json 序列化（FilamentGroupContext、BaseResult、TestMetadata 等）及文件 I/O |
| `fg_test_utils.hpp` | `TestRng` 确定性随机数生成器；`build_test_case()` 从 seed 构建完整 case；三种机器配置模板（A/B/C）；约束注入 |
| `test_data_generator.cpp` | 独立工具，生成大规模测试数据到 `data/` 目录（用于开发阶段对比分析，不入 git） |

## 构建目标

| Target | 说明 |
|--------|------|
| `filament_group_tests` | 回归测试可执行程序 |
| `fg_test_data_generator` | 数据生成工具（独立可执行，非测试） |

## 快速使用

```bash
# 编译
cmake --build build --target filament_group_tests

# 运行全部
./build/tests/filament_group_tests

# 仅 golden（27 case，验证不退化）
./build/tests/filament_group_tests "[golden]"

# 仅属性检查（60 case，种子生成）
./build/tests/filament_group_tests "[property]"

# 更新 golden baseline（算法改进后手动执行）
./build/tests/filament_group_tests "[update-golden]"
```
