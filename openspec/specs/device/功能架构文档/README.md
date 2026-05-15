# 功能架构文档

本区承接设备领域中相对稳定、面向研发阅读的知识沉淀，首批来源为 `docs/device-management/`，也允许补充少量具有长期参考价值的专题架构总览。

这里主要回答：

- 设备系统由哪些模块组成
- 页面与功能如何分层
- 设备数据如何流向页面和任务

## 来源

- 主入口：`docs/device-management/README.md`
- 总览：`docs/device-management/00-设备管理功能架构总览.md`
- 文档数量：`41` 个 Markdown 文件
- 补充专题：`src/slic3r/GUI/fila_manager/filament_manager_spec.md`

## 主题结构

### 1. 设备数据管理

对应来源：

- `docs/device-management/01-设备数据管理/README.md`
- `docs/device-management/01-设备数据管理/01-设备对象模型-MachineObject与DeviceManager.md`
- `docs/device-management/01-设备数据管理/02-设备列表与选中设备管理.md`
- `docs/device-management/01-设备数据管理/03-设备状态同步与消息分发.md`
- `docs/device-management/01-设备数据管理/04-设备订阅与刷新机制.md`
- `docs/device-management/01-设备数据管理/05-设备配置资源与机型信息加载.md`
- `docs/device-management/01-设备数据管理/06-设备数据对页面与任务的供给关系.md`

### 2. 设备页面

对应来源：

- `docs/device-management/02-设备页面/README.md`
- `docs/device-management/02-设备页面/01-设备页总览-Monitor与MultiMachinePage.md`
- `docs/device-management/02-设备页面/02-设备状态页-StatusPanel.md`
- `docs/device-management/02-设备页面/03-多设备列表页-MultiMachineManagerPage.md`
- `docs/device-management/02-设备页面/04-设备存储页-MediaFilePanel.md`
- `docs/device-management/02-设备页面/05-设备升级与HMS页面.md`
- `docs/device-management/02-设备页面/06-设备子控件-DeviceTab.md`
- `docs/device-management/02-设备页面/07-多设备主页面与任务页.md`

### 3. 发送打印页面

对应来源：

- `docs/device-management/03-发送打印页面/README.md`
- `docs/device-management/03-发送打印页面/01-发送打印页面总览-SelectMachine与SendToPrinter.md`
- `docs/device-management/03-发送打印页面/02-发送前校验-PrePrintChecker.md`
- `docs/device-management/03-发送打印页面/03-发送入口与页面跳转.md`
- `docs/device-management/03-发送打印页面/04-选机打印页面-SelectMachineDialog详解.md`
- `docs/device-management/03-发送打印页面/05-发送到打印机页面-SendToPrinterDialog详解.md`
- `docs/device-management/03-发送打印页面/06-多设备发送页面-SendMultiMachinePage详解.md`
- `docs/device-management/03-发送打印页面/07-AMS映射与喷嘴映射.md`

### 4. 发送打印协议与任务

对应来源：

- `docs/device-management/04-发送打印协议/README.md`
- `docs/device-management/04-发送打印协议/01-发送打印协议总览-NetworkAgent与PrintJob.md`
- `docs/device-management/04-发送打印协议/02-打印任务-PrintJob与SendJob.md`
- `docs/device-management/04-发送打印协议/03-多设备任务调度-TaskManager.md`
- `docs/device-management/04-发送打印协议/04-打印参数模型-PrintParams.md`
- `docs/device-management/04-发送打印协议/05-发送阶段回执与等待机制.md`
- `docs/device-management/04-发送打印协议/06-错误码与异常处理.md`

### 5. 附录与索引

对应来源：

- `docs/device-management/05-附录/README.md`
- `docs/device-management/05-附录/01-关键文件索引.md`
- `docs/device-management/05-附录/02-核心类关系图.md`
- `docs/device-management/05-附录/03-发送打印时序图.md`
- `docs/device-management/05-附录/04-设备状态同步时序图.md`
- `docs/device-management/05-附录/05-术语表.md`
- `docs/device-management/05-附录/06-关键源码入口索引-精确版.md`
- `docs/device-management/05-附录/07-设备管理源码阅读路线图.md`
- `docs/device-management/05-附录/08-关键调用链表.md`

### 6. 耗材管理器专题

补充来源：

- `src/slic3r/GUI/fila_manager/filament_manager_spec.md`
- `openspec/specs/device/软件代码文档/耗材管理器/`

当前归档：

- `filament_manager_spec.md` — 耗材管理器主 spec 全文归档，保留系统级设计原文
- `耗材管理器整体架构.md` — 耗材管理器在 Studio 中的位置、稳定分层、关键数据流与主线架构演进

## 迁移建议

- 优先迁移总览和各主题 `README.md`
- 第二批迁移设备数据管理与发送打印协议
- 第三批迁移设备页面和发送打印页面
- 最后一批迁移附录与索引类文档

详细规则见：`../../.rules/02-docs-device-management迁移规则.md`
