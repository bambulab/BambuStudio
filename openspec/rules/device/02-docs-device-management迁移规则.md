# `docs/device-management` 迁移规则

本文件定义现有设备研发文档迁入 `openspec/specs/device/功能架构文档/` 的顺序和兼容策略。

## 迁移目标

- 让 `openspec/specs/device/` 成为设备研发文档的统一阅读入口
- 在迁移过程中尽量不打断现有文件名和阅读顺序
- 先搬总览和主干，再搬细节与附录

## 分批迁移顺序

### 第一批：入口与总览

- `docs/device-management/README.md`
- `docs/device-management/00-设备管理功能架构总览.md`
- 各主题目录下的 `README.md`

### 第二批：设备数据主干

- `docs/device-management/01-设备数据管理/`
- `docs/device-management/04-发送打印协议/`

### 第三批：页面与发送流程

- `docs/device-management/02-设备页面/`
- `docs/device-management/03-发送打印页面/`

### 第四批：附录与索引

- `docs/device-management/05-附录/`

## 归档目标目录

- `docs/device-management/00-设备管理功能架构总览.md`
  - `openspec/specs/device/功能架构文档/`
- `docs/device-management/01-设备数据管理/`
  - `openspec/specs/device/功能架构文档/01-设备数据管理/`
- `docs/device-management/02-设备页面/`
  - `openspec/specs/device/功能架构文档/02-设备页面/`
- `docs/device-management/03-发送打印页面/`
  - `openspec/specs/device/功能架构文档/03-发送打印页面/`
- `docs/device-management/04-发送打印协议/`
  - `openspec/specs/device/功能架构文档/04-发送打印协议/`
- `docs/device-management/05-附录/`
  - `openspec/specs/device/功能架构文档/05-附录/`

## 命名兼容策略

- 正文文件名默认保持原样
- 只在目录层级上做归并，不人为改写原文标题
- 迁移前允许先建立 README 与主题入口，不要求一次性搬完正文

## 链接兼容策略

- 正文未迁入前，由归档入口直接链接原始路径
- 正文迁入后，在归档版文首标注来源路径
- 如果后续需要清理旧路径，必须先补齐归档区内链

## 当前状态

- 已完成目录骨架
- 已完成来源映射与主题索引
- 原始 `docs/device-management` 文件暂保留，保证现有引用不受影响
