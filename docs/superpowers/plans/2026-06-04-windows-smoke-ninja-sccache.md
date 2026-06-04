# Windows Smoke Ninja Sccache Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 将 Windows 的 `cpp_ut_smoke.yml` 从 Visual Studio 生成器切换到 `Ninja + sccache`，让 smoke UT 在本地和 GitHub Actions 上都能获得可观的编译缓存收益。

**Architecture:** 保留现有 deps 产物与 smoke 测试目标不变，只替换 smoke lane 的生成器、MSVC 环境初始化和 sccache 配置。通过本地冷/热构建对比与 `sccache --show-stats` 验证缓存命中，而不是只看 workflow 配置表面是否接入。

**Tech Stack:** GitHub Actions, CMake, Ninja, MSVC, sccache, PowerShell

---

### Task 1: 准备本地验证环境

**Files:**
- Modify: `D:\workspace\BambuStudio\.worktrees\ci-quality-gate-pr\docs\superpowers\plans\2026-06-04-windows-smoke-ninja-sccache.md`
- Test: 本地 `cmake` / `ninja` / `sccache` 命令行

- [ ] **Step 1: 确认本地工具链现状**

Run: `Get-Command cmake,ninja,sccache -ErrorAction SilentlyContinue`
Expected: 至少已有 `cmake`、`ninja`，若缺 `sccache` 则补装。

- [ ] **Step 2: 安装或补齐 sccache**

Run: `choco install sccache --no-progress -y`
Expected: `sccache.exe` 可从命令行调用。

- [ ] **Step 3: 记录本地验证命令**

```powershell
cmd /c '"D:\Program Files\Microsoft Visual Studio\2022\Community\Common7\Tools\VsDevCmd.bat" -arch=x64 -host_arch=x64 && sccache --zero-stats && cmake -S . -B build-local-smoke-ninja-sccache -G Ninja -DCMAKE_BUILD_TYPE=Release -DSLIC3R_GUI=OFF -DSLIC3R_STATIC=ON -DSLIC3R_BUILD_TESTS=ON -DSLIC3R_MSVC_COMPILE_PARALLEL=OFF -DSLIC3R_MSVC_PDB=OFF -DCMAKE_C_COMPILER_LAUNCHER=sccache -DCMAKE_CXX_COMPILER_LAUNCHER=sccache -DCMAKE_PREFIX_PATH=D:/workspace/BambuStudio-deps/usr/local -DPKG_CONFIG_EXECUTABLE=C:/Strawberry/perl/bin/pkg-config.bat -DWIN10SDK_PATH=C:/Program Files (x86)/Windows Kits/10/Include/10.0.26100.0'
```

- [ ] **Step 4: 冷构建并查看 stats**

Run: `cmake --build build-local-smoke-ninja-sccache --target libslic3r_config_smoke fff_print_smoke -v`
Expected: 首轮多为 miss，`sccache --show-stats` 出现 compile requests。

### Task 2: 修改 smoke workflow

**Files:**
- Modify: `D:\workspace\BambuStudio\.worktrees\ci-quality-gate-pr\.github\workflows\cpp_ut_smoke.yml`
- Test: workflow YAML 静态检查 + 本地同参数构建

- [ ] **Step 1: 切换 workflow 到 Ninja 单配置**

将 configure 从：

```yaml
-G "Visual Studio 17 2022"
-A x64
```

改为：

```yaml
-G Ninja
-DCMAKE_BUILD_TYPE=Release
```

- [ ] **Step 2: 初始化 MSVC 编译环境**

加入：

```yaml
- name: Setup MSVC developer command prompt
  uses: ilammy/msvc-dev-cmd@v1
  with:
    arch: x64
```

确保 Ninja 下 `cl.exe`、`link.exe` 可直接调用。

- [ ] **Step 3: 保持 smoke 目标与运行命令不变**

仅调整单配置输出路径，例如：

```yaml
${{ env.BUILD_DIR }}\tests\libslic3r\libslic3r_config_smoke.exe
${{ env.BUILD_DIR }}\tests\fff_print\fff_print_smoke.exe
```

- [ ] **Step 4: 保留并增强 sccache 统计**

继续输出：

```yaml
$env:SCCACHE_PATH --zero-stats
$env:SCCACHE_PATH --show-stats
```

必要时补充版本输出，便于判断 runner 上到底在用哪个二进制。

### Task 3: 做本地冷/热构建验证

**Files:**
- Test: `build-local-smoke-ninja-sccache`

- [ ] **Step 1: 删除验证目录并做冷构建**

Run: `Remove-Item -Recurse -Force build-local-smoke-ninja-sccache`
Expected: 全量编译，stats 以 miss 为主。

- [ ] **Step 2: 不改代码直接再构建一次**

Run: `cmake --build build-local-smoke-ninja-sccache --target libslic3r_config_smoke fff_print_smoke -v`
Expected: 第二次构建几乎无编译动作，若有则 `sccache` hit 明显增加。

- [ ] **Step 3: 只触碰一个深层 cpp 再构建**

对单个 smoke 相关 cpp 做时间戳触发或最小改动，再重新 build。
Expected: 只有受影响 translation units 重新编译，其余对象复用，`sccache` 命中明显高于冷构建。

### Task 4: 同步到远端并观察 Actions

**Files:**
- Modify: `D:\workspace\BambuStudio\.worktrees\ci-quality-gate-pr\.github\workflows\cpp_ut_smoke.yml`

- [ ] **Step 1: 查看 diff 并确认只改 smoke lane**

Run: `git diff -- .github/workflows/cpp_ut_smoke.yml docs/superpowers/plans/2026-06-04-windows-smoke-ninja-sccache.md`
Expected: 只包含计划文件和 workflow 变更。

- [ ] **Step 2: 提交**

```bash
git add .github/workflows/cpp_ut_smoke.yml docs/superpowers/plans/2026-06-04-windows-smoke-ninja-sccache.md
git commit -m "ci: switch windows smoke lane to ninja and sccache"
```

- [ ] **Step 3: 推送并观察 run**

Run: `git push` 或当前分支既有的 `gh api` 更新方式
Expected: `Cpp UT Smoke` 新 run 出现，并在 summary 中看到 `sccache` stats。
