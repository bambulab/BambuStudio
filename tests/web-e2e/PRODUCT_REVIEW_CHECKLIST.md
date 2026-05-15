# 产品验收 Checklist (Web E2E)

> Source-of-truth for AI agent self-validation runs on the device-page
> webview.  Born from STUDIO-17977 multicolor regression — agent string
> dumps reported "all pass" while the AMS multicolor card visually
> rendered as a flat green block with the spool icon hidden under it.

## 核心原则

字符串 dump **不能** 替代视觉验收。每条新增/修改的视觉路径必须同时满足：

1. **数据正确**：DOM 属性 / `data-*` / textContent 与产品契约一致。
2. **结构正确**：DOM/SVG 子树形态合理（无意外的 overlay、clipPath、隐藏元素）。
3. **像素正确**：实际截图与"产品经理眼中的合格状态"一致。

任何一条不查就声称 "pass"，都属于自欺欺人。

## 视觉路径清单（device_page / 耗材管理器）

每条路径都至少有 **两种独立渲染** 在用，必须**全部**测过：

| 视觉对象 | 用到的渲染 | 测试动作 |
|---|---|---|
| AMS 卡片 chip (32×32) | `SpoolColorChip` SVG | dump SVG outerHTML + 截图卡片 |
| 列表行 chip (40×40) | `SpoolColorChip` SVG | dump SVG outerHTML + 截图行 |
| 详情弹窗 chip | `SpoolColorChip` SVG | dump SVG outerHTML + 截图 |
| Add/Edit 弹窗 preview-swatch (12×12) | CSS `linear-gradient` | dump `style` attr + 截图 |
| Add/Edit candidate panel (24×24) | CSS `linear-gradient` | dump `style` attr + 截图 |
| Add/Edit AMS 卡片 chip (32×32) | `SpoolColorChip` SVG | dump SVG + 截图 |

> ⚠️ SVG 路径 **不会** 因为 CSS 路径正确而被覆盖 —— 反之亦然。两条路径
> 共用同一份 `(colorCode, colors[], colorType)` 数据，但 **渲染逻辑独立**，
> 必须分别验证。

## 多色 chip 视觉 sanity

每个多色 chip（colors.length > 1）必须满足：

- [ ] 两/三/四种颜色都在视觉上**可分辨**
- [ ] spool **形状完整**（轮廓、料盘、料芯、料盘心都看得见）
- [ ] **黑色眼睛** + 白色高光在最上层、不被覆盖
- [ ] **没有任何一块**矩形/带状色覆盖到 spool 形状之外
- [ ] 渐变模式（type=0）：色与色之间是连续过渡
- [ ] 多拼模式（type=1）：色与色之间是 hard 边界，等宽
- [ ] opacity 层次感：spool 各部位有亮/暗差异（不应该是一整块平面色）

## 单色 chip 视觉 sanity

- [ ] 整个 spool 主体是同一个 hex
- [ ] 黑色眼睛 + 高光正常
- [ ] **不能** 出现意外的 `linearGradient` / `clipPath` 元素

## SVG 结构红线（出现这些立刻怀疑 bug）

```text
hasClipPath = true  + 多色 chip       → 99% 是 overlay-rect 旧实现的回归
foreign  <rect>     在 SVG 末尾       → 极可能覆盖了 spool 主体
distinctBodyFills > 1 (排除眼睛/高光) → 多色没走 gradient，回到了 overlay
linearGradient stops 数量 ≠ 期望      → 多拼应有 2N，渐变应有 N
```

## 交叉验证模板

```bash
# 每条多色 chip 验收必跑 3 步：

# 1. SVG 结构 dump（结构红线）
probe-chip-visual.mjs

# 2. 单元素截图（像素验收）
# probe-chip-visual.mjs 同时输出每张 chip 的局部 PNG 到 .evidence/chip-visual/
# 用 Read 工具直接打开比对

# 3. 多种渲染路径都要走一遍
# - probe-ams-slot-switch.mjs   (AMS 卡片 + preview-swatch)
# - probe-list-rows.mjs         (列表行 chip)
# - probe-manual-multicolor.mjs (manual tab candidate panel)
# - probe-edit-dialog.mjs       (edit 模式回填)
```

## 验收声明的下限

不允许说出："**测试通过 / 验证通过 / 修复完成**" 之类的字眼，除非：

- [ ] 数据 dump 全绿
- [ ] **关键 chip 的 PNG 截图已用 Read 工具实际看过**（不只看文件存在）
- [ ] 视觉清单（上面）逐项打勾

否则只能说："**字符串数据正确，视觉待人工确认**"。
