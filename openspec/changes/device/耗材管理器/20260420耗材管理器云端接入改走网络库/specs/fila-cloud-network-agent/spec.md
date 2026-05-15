## ADDED Requirements

### Requirement: networking SHALL 暴露耗材云端标准接口

系统 SHALL 在 `bambu_network` 中提供耗材管理器云端访问的标准导出接口，供 Studio 通过 `NetworkAgent` 调用。

#### Scenario: 5 个导出接口完整可用

- **WHEN** Studio 动态加载 `bambu_networking` 模块
- **THEN** SHALL 能解析以下符号：
  - `bambu_network_get_filament_spools`
  - `bambu_network_create_filament_spool`
  - `bambu_network_update_filament_spool`
  - `bambu_network_delete_filament_spools`
  - `bambu_network_get_filament_config`

#### Scenario: 认证端点统一复用网络库登录态

- **WHEN** 调用耗材列表 / 新增 / 更新 / 删除接口
- **THEN** `AccountManager` SHALL 通过 `get_token_str()` 注入认证头，而不是要求 Studio 传入 token

#### Scenario: 配置接口无需登录

- **WHEN** 调用 `get_filament_config`
- **THEN** SHALL 允许未登录调用，并使用 `get_api_server_host()` 构造请求地址

### Requirement: Studio SHALL 通过 NetworkAgent 接入耗材云端

系统 SHALL 让耗材管理器云端请求走 `NetworkAgent -> bambu_network -> AccountManager` 链路，而不是 `wgtFilaManagerCloudClient` 直接发 `Http`。

#### Scenario: Studio 不再自己拼鉴权

- **WHEN** `wgtFilaManagerCloudClient` 发起任何耗材云端请求
- **THEN** SHALL 调用 `NetworkAgent` 对应 wrapper；SHALL NOT 再从 `build_login_info()` 解析 token；SHALL NOT 自己拼 `Authorization` header

#### Scenario: 上层业务调用关系保持不变

- **WHEN** `wgtFilaManagerCloudSync` 或 `wgtFilaManagerCloudDispatcher` 调用 `wgtFilaManagerCloudClient`
- **THEN** SHALL 仍然使用既有 `create_spool` / `update_spool` / `batch_delete` / `list_spools` / `get_filament_config` 这组方法，不感知底层已切换到网络库

### Requirement: CloudClient SHALL 保持异步回调语义

系统 SHALL 在 Studio 侧保留 `wgtFilaManagerCloudClient` 的异步行为，不因网络库接口是同步风格而阻塞 UI 线程。

#### Scenario: 后台线程执行同步接口

- **WHEN** `wgtFilaManagerCloudClient` 收到请求
- **THEN** SHALL 在后台线程里调用 `NetworkAgent`

#### Scenario: 成功回调回到 UI 线程

- **WHEN** 网络库调用成功并返回响应 body
- **THEN** SHALL 解析 JSON，并通过 `CallAfter` 在 UI 线程回调 `SuccessFn`

#### Scenario: 失败回调回到 UI 线程

- **WHEN** 网络库返回非成功错误码或错误响应 body
- **THEN** SHALL 通过 `CallAfter` 在 UI 线程回调 `ErrorFn`

### Requirement: 设备域文档 SHALL 记录新的实现归档

系统 SHALL 在设备域软件代码文档中记录这次“耗材云端接入改走网络库”的实现背景、调用链和源码入口。

#### Scenario: 从耗材管理器主题进入

- **WHEN** 读者从 `openspec/specs/device/软件代码文档/耗材管理器/README.md` 进入
- **THEN** SHALL 能看到 `20260420耗材管理器云端接入改走网络库/` 条目，以及它与 `fila-cloud-sync` / `fila-cloud-integration` 的承接关系

#### Scenario: 从协议与网络入口进入

- **WHEN** 读者从 `openspec/specs/device/主题索引.md` 的“发送协议、任务与回执”问题域进入
- **THEN** SHALL 能定位到这条归档，理解耗材管理器为何需要改走 `NetworkAgent`

#### Scenario: 当前工作区缺少网络库源码

- **WHEN** 分析继续下钻到 `bambu_network` / `AccountManager` 实现，而当前工作区没有可读的 networking 源码
- **THEN** SHALL 先基于本归档与索引说明已有结论；SHALL 提醒向用户索要 networking 仓库链接或源码路径；SHALL NOT 在无源码时臆测网络库内部实现
