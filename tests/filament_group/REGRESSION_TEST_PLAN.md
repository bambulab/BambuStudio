# FilamentGroup 回归测试方案

## 目标

验证 `FilamentGroup` 算法在重构/优化后不发生性能退化，约束始终满足。

---

## 测试架构：两层结构

### Layer 1: Golden 回归（27 case，存文件）

从预存的 golden 输入文件中加载 `FilamentGroupContext`，运行算法，对比 `base_result` 中记录的基准分数。

**断言**：
- `new_full_score <= base_full_score + tolerance`（tolerance = max(50, score * 3%)）
- `REQUIRE(constraints_ok)` — 所有 golden case 约束均可行
- `elapsed_ms < 10000` — 超时检测

**分布**（27 case）：

| Config | 数量 | 构成 |
|--------|------|------|
| A (2ext × 2noz) | 10 | basic×3, stress×2, constraint×2, mode_match×1, mode_bestfit×1, mode_time×1 |
| B (2ext × 1+Knoz) | 12 | basic×3, stress×3, constraint×3, mode_match×1, mode_bestfit×1, mode_time×1 |
| C (1ext × Knoz) | 5 | basic×2, stress×2, constraint×1 |

**文件格式**：每个 golden 文件为单个 `.json`，包含：
```json
{
  "metadata": {"id": "...", "config_type": "A", "seed": 10017},
  "context": { /* FilamentGroupContext 完整序列化 */ },
  "base_result": {"full_score": 12345.6, "flush_cost": 8000, "constraints_ok": true}
}
```

**更新机制**：通过 `[update-golden]` tag 运行测试，自动覆写所有文件的 `base_result` 字段。

### Layer 2: 属性检查（60 case，种子重生成）

运行时通过 `build_test_case(seed, ...)` 重新生成输入，做通用属性断言：
- `elapsed_ms < 10000`
- `flush_cost >= 0`
- 约束可行时 `REQUIRE(constraints_ok)`；不可行时仅 WARN

**可行性判断**：`sum(max_group_size) >= num_used_filaments`

**分布**：

| Config | 数量 | 重点 |
|--------|------|------|
| A | 20 | basic×6, stress×4, constraint×4, edge×3, mode×3 |
| B | 25 | basic×6, stress×6, constraint×7, edge×3, mode×3 |
| C | 15 | basic×5, stress×3, constraint×3, edge×2, mode×2 |

---

## 评估逻辑

`full_evaluate_map()` 完整评估一个 filament_map 的质量：
1. 构建 `LayeredNozzleGroupResult`
2. 调用 `reorder_filaments_for_multi_nozzle_extruder` 计算 flush_cost
3. 调用 `simulate_filament_change_time` 计算 change_time
4. `full_score = flush_score + change_time`（flush 转时间后累加）
5. `check_constraints` 独立验证约束满足

---

## 三种机器配置

**Config A (2-extruder, 2-nozzle)**：经典双喷头，每个 extruder 1 个 Standard 喷嘴。

**Config B (2-extruder, 1+K nozzle)**：ext0 有 1 个 Standard 喷嘴，ext1 有 K（2~6）个喷嘴，volume_type 混合。模拟选料器场景。

**Config C (1-extruder, K nozzle)**：单 extruder 多喷嘴（3~9），volume_type 轮转分配。

---

## 约束检查

独立于算法实现，验证输出满足以下硬约束：
1. **unprintable_filaments**：filament 不能分配到它被禁止的 extruder 的喷嘴上
2. **unprintable_volumes**：filament 不能分配到它被禁止的 volume_type 的喷嘴上
3. **max_group_size**：每个 extruder 分配的耗材数不超过上限（仅在总容量可行时检查）

---

## 运行方式

```bash
# 运行全部测试（golden + property）
filament_group_tests

# 仅 golden 回归
filament_group_tests "[golden]"

# 仅属性检查
filament_group_tests "[property]"

# 更新 golden baseline（手动触发）
filament_group_tests "[update-golden]"
```

---

## 关键依赖

| 文件 | 作用 |
|------|------|
| `src/libslic3r/FilamentGroup.hpp/cpp` | 被测算法 |
| `src/libslic3r/MultiNozzleUtils.hpp/cpp` | 喷嘴分组结果、换料时间模拟 |
| `src/libslic3r/GCode/ToolOrderUtils.hpp` | `reorder_filaments_for_multi_nozzle_extruder` |
| `src/libslic3r/FilamentGroupUtils.hpp` | FilamentInfo 等数据结构 |
| `src/libslic3r/PrintConfig.hpp` | NozzleVolumeType 等枚举 |
