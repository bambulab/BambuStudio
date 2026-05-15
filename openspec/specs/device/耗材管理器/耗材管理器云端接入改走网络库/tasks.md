## 1. networking 侧接口面

- [x] 1.1 在 `include/bambu_networking.hpp` 新增 `FilamentQueryParams` / `FilamentDeleteParams`
- [x] 1.2 新增耗材云端错误码定义
- [x] 1.3 在 `include/bambu_networking_api.hpp` 暴露 5 个耗材导出接口
- [x] 1.4 在 `src/bambu_networking.cpp` 增加导出转发与错误码映射

## 2. networking 侧业务实现

- [x] 2.1 在 `AccountManager.hpp` 增加 5 个耗材方法声明
- [x] 2.2 在 `AccountManager.cpp` 落地列表 / 新增 / 更新 / 删除 / 配置读取
- [x] 2.3 认证端点统一复用 `get_token_str()`
- [x] 2.4 `BambuNetworkAgent.hpp/.cpp` 增加薄包装转发
- [x] 2.5 `networking` 仓库本地提交完成：`7e19457`

## 3. Studio 侧适配

- [x] 3.1 在 `src/slic3r/Utils/bambu_networking.hpp` 对齐新增结构和错误码
- [x] 3.2 在 `NetworkAgent.hpp/.cpp` 增加动态库函数指针与 wrapper
- [x] 3.3 `wgtFilaManagerCloudClient` 改为后台线程调用 `NetworkAgent`
- [x] 3.4 删除 Studio 侧直接 `Http` / token 解析逻辑
- [x] 3.5 保留 `SuccessFn` / `ErrorFn` 的异步接口外观

## 4. 设备域归档

- [x] 4.1 新增 `20260420耗材管理器云端接入改走网络库/` 归档目录
- [x] 4.2 补充 `proposal.md` / `design.md` / `tasks.md` / `specs/`
- [x] 4.3 更新 `耗材管理器/README.md`
- [x] 4.4 更新 `openspec/specs/device/来源映射表.md`
- [x] 4.5 更新 `openspec/specs/device/主题索引.md`
- [x] 4.6 更新 `openspec/specs/device/软件代码文档/README.md`
- [x] 4.7 更新 `openspec/rules/device/03-openspec规则.md` 中的纳入条目

## 5. 待验证项

- [ ] 5.1 `D:/dev/networking` 与 `D:/dev/bamboo_slicer` 做跨仓完整编译验证
- [ ] 5.2 运行耗材管理器页面验证 `pull / push / config fetch` 三条路径
- [ ] 5.3 确认动态库版本与 Studio 侧导出符号完全一致
