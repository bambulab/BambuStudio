# Device Profile Skills Index

本目录不定义工具技能实现，只提供设备工作方法入口索引。

请优先回到以下真实来源：

- `/openspec/skills/device/README.md`
- `/openspec/skills/device/01-设备知识检索技能.md`
- `/openspec/skills/device/02-设备问题分析技能.md`
- `/openspec/skills/device/03-文档沉淀技能.md`

推荐场景：

- 快速定位设备资料
- 从问题现象追到设计与源码
- 将开发过程沉淀回设备知识目录

注意：`device-local-build`（`/openspec/skills/device/09-设备研发本地编译技能.md`）
和 `device-local-debug`（`/openspec/skills/device/10-设备研发本地调试技能.md`）
是例外技能，不跟随设备 profile 自动激活，必须由用户手动调用或明确说
"按设备域本地编译技能进行编译" / "按设备域本地调试技能进行调试"才触发，
激活时桥接必须先向用户告知。`device-local-debug` 默认使用 Visual Studio，
可切换到 Visual Studio Code。
