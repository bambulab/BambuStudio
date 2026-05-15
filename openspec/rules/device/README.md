# 设备目录规则

本目录用来定义 `openspec/` 下设备域目录（`rules/device/`、`skills/device/`、`specs/device/`、`changes/device/`、`docs/device/`）的使用边界、归档口径和维护方式。

## 当前规则文件

- `01-目录与命名规则.md`
  - 定义目录职责、文件命名方式和追溯要求
- `02-docs-device-management迁移规则.md`
  - 定义 `docs/device-management` 如何迁入设备目录
- `03-openspec规则.md`
  - 定义设备域 `OpenSpec` 的入口链路、纳入范围、归档规则和状态口径
- `04-技能与规则生成规则.md`
  - 定义 `openspec/skills/device/` 和 `openspec/rules/device/` 的标准化格式、生成流程和质量要求
- `10-web-panels开发约束.md`
  - 与设备目录并存的专项前端约束，单独收纳避免散落在根层
- `11-设备上下文接入规则.md`
  - 定义设备 profile 开关、加载顺序和进入 `/openspec/docs/device/` 的统一规则
- `12-commit-msg语言规则.md`
  - 定义涉及开源代码的 commit 必须全英文，设备域纯文档 / 工具链改动可用中文
- `13-设备研发本地编译规则.md`（已迁移为技能，stub）
  - 内容已整体迁移至 `/openspec/skills/device/09-设备研发本地编译技能.md`
  - 保留 stub 仅为避免历史 tasks.md 中对旧路径的引用断链

## 什么时候先看 `.rules`

- 准备新增设备专题文档时
- 准备迁移旧文档时
- 不确定某份资料应归到哪里时
- 需要调整目录结构时

## 核心原则

- `openspec/` 下的设备目录是设备知识入口，不是所有 AI 资产的总仓
- 功能结构类内容进入 `openspec/specs/device/功能架构文档/`
- 源码入口、实现线索、OpenSpec 变更进入 `openspec/specs/device/软件代码文档/` 与 `openspec/changes/device/`
- 新增资料前，先更新 `openspec/specs/device/主题索引.md` 和 `openspec/specs/device/来源映射表.md`
