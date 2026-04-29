# Device Profile Load Order

## 默认加载顺序

1. `/openspec/docs/device/readme食用指南.md`
2. `/openspec/docs/device/开发流程.md`
3. 若任务涉及设备域 `OpenSpec`，必须先读 `/openspec/rules/device/03-openspec规则.md`
4. `/openspec/specs/device/README.md`
5. `/openspec/specs/device/主题索引.md`（需要快速定位主题时）

## 规则入口

以下情况优先补读 `/openspec/rules/device/README.md` 及对应规则文件：

- 新增设备专题文档
- 调整目录结构
- 迁移旧文档
- 不确定某份资料应该归档到哪里
- 处理设备域 Web Panel 约束

其中：

- 只要任务涉及设备域 `OpenSpec` change、spec、proposal、design、tasks、归档或基线规格，必须优先进入 `/openspec/rules/device/03-openspec规则.md`

## 技能入口

以下情况优先补读 `/openspec/skills/device/README.md` 及对应技能文档：

- 不知道从哪里查设备资料
- 需要把问题从现象一路追到设计和源码
- 准备把过程沉淀回设备目录

## 专题入口

以下情况优先进入 `/openspec/specs/device/`：

- 需要理解设备功能结构
- 需要追代码入口、协议和实现背景
- 需要查看设备相关 OpenSpec 变更
- 需要定位某个设备专题的历史设计取舍

但若入口问题本身属于设备域 `OpenSpec`，应先按上面的规则入口读取 `03-openspec规则.md`，再进入 `openspec/specs/device/`
