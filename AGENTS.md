# AGENTS.md

本文件是**给在本仓库工作的编码助手看的工作约定**。开始改动前先读一遍。

---

## 1. 项目速览

Windows 任务栏网速悬浮窗（Qt6 / C++17 / Win32）。**仅支持 Windows**。

```
src/         13 个模块（net_monitor / overlay / tray / settings_window / config ...）
tests/       QtTest 单元测试（speed_format / config / theme）
cmake/       version.h.in、app.rc.in（版本号单一来源：project(VERSION)）
docs/        审查与整改记录
dist/        本地部署产物（已 gitignore）
```

关键设计（改动前务必理解，避免把已修好的问题改回去）：

- **网速只有一个数据源**：`GetIfTable2`，按 `InterfaceLuid` 逐接口差分，速率 = 增量 ÷ `QElapsedTimer` 实测耗时。不要再引入 PDH 或第二份基线。
- **必须按 `HardwareInterface` 去重**：`GetIfTable2` 会把每张网卡上的 NDIS 筛选器
  （WFP Native MAC / WFP 802.3 MAC / QoS Packet Scheduler）也各列成一个接口，字节计数与物理网卡完全相同，
  全部累加会让网速虚高 3~5 倍（v1.3.1 修的就是这个）。虚拟/隧道接口（Wintun 等，type=53）也要排除。
  排查时用 `--net-diag` 打印每张网卡的增量。
- **悬浮窗"永不被任务栏盖住"靠的是 owner 机制**：`attachToTaskbarOwner()` 把 `Shell_TrayWnd` 设为
  `GWLP_HWNDPARENT`（MS 文档：owned window 永远在 owner 之上）。窗口仍是顶层窗口，Qt 的透明渲染不受影响。
  `keepOnTop()`（200ms）只做三件事：恢复被隐藏/最小化、补挂 owner、topmost 位丢失时兜底。
- **owner 的代价**：任务栏被销毁（Explorer 重启）时本窗口会被连带销毁。`App::checkOverlayAlive()`
  每 2s 用 `nativeHandle()` 检查 `IsWindow`，失效就重建 `Overlay`。`WM_NCCREATE` 里刷新该句柄缓存。
- **不要试图用 z-order 判断"我是不是被盖住了"**：`GetWindow(GW_HWNDPREV)` 只在"紧邻上方就是任务栏"时成立，
  托盘面板、IME、ForegroundStaging 插在中间时就失效。实测结论：判断不可靠，无条件保活才可靠。
- **任务栏子窗口方案已被证伪**：`SetParent(overlay, Shell_TrayWnd)` 之后 Qt 的 `UpdateLayeredWindow`
  对子窗口不生效，窗口"可见但画不出来"（截图验证过），与"内部透明"不可兼得。不要再走这条路。
- **启动首帧**：隐藏状态下定尺寸/定位 → DWM cloak 遮蔽 → `show()` → `repaint()` → 揭幕。不要在构造里直接 `show()`。
- **任务栏颜色**必须用**屏幕 DC**采样（窗口 DC 取不到 DWM 合成结果），且只采任务栏上下边缘的空白条带。

---

## 2. 构建 / 测试 / 部署

本机 Qt：`D:\Develop\SERVE\Qt6\6.8.3\msvc2022_64`（无 Qt 时先按 README 安装）。

```powershell
cmake -S . -B build -G "Visual Studio 17 2022" -A x64 `
      -DCMAKE_PREFIX_PATH="D:\Develop\SERVE\Qt6\6.8.3\msvc2022_64" -DBUILD_TESTING=ON
cmake --build build --config Release --parallel
$env:PATH = "D:\Develop\SERVE\Qt6\6.8.3\msvc2022_64\bin;$env:PATH"   # ctest 需要 Qt DLL
ctest --test-dir build -C Release --output-on-failure
cmake --install build --prefix dist --config Release
```

交付标准：**编译零错误零警告**（`/W4 /permissive-`）、`ctest` 全绿、`dist/NetSpeedFlux.exe` 能启动且悬浮窗可见。

---

## 3. 提交与推送约定（重要）

**每次会话结束都必须提交并推送**，格式固定：

```
v<MM.DD>-<序号> <一句话说明改了什么>

- 具体改动点 1
- 具体改动点 2

细节见 docs/xxx.md 第 N 节
```

规则：

1. 标题前缀 `v11.19-1`、`v11.19-2`…（月.日 + 当天第几次），**同一天多次提交递增序号**。
2. 正文用 `-` 列出可核对的改动点，不要写"优化代码"这类空话。
3. `git add -A` 后提交，然后 `git push origin master`。
4. 推送后必须复核并**把结果回报给用户**：
   ```powershell
   git log --oneline origin/master -1
   git status -sb
   ```
5. **禁止** `push --force` / `--force-with-lease`，禁止改写已推送的历史。
6. 推送失败（凭据/网络）要**明确报错**并给出可执行命令，不得静默跳过。
7. 提交身份用仓库本地配置（`bobby <dearflc@163.com>`），不要用 `-c` 临时覆盖成别的名字。

---

## 4. 代码约定

- 每个 `.cpp` 若用到 Win32，**先 `#define NOMINMAX` / `WIN32_LEAN_AND_MEAN` 再包含 `<windows.h>`**；`netioapi.h` 前必须已有 `ws2ipdef.h`。
- Qt 的 `isVisible()` **不反映**系统隐藏/最小化，涉及窗口可见性一律用 `IsWindowVisible` + `IsIconic`。
- 颜色/主题改动必须在**浅色和深色任务栏**下都能看清；不要在无日志的情况下引入"智能修正"。
- 纯逻辑（格式化、配置解析、主题）改动要同步加 `tests/` 用例。
- 用户可见行为变化要更新 `README.md` / `README.en.md`，并在 `docs/` 留记录。
- 单文件不要超过 ~600 行；新增功能优先加模块，不要继续堆进 `overlay.cpp`。

---

## 5. 不要做的事

- 不要把 `build/`、`dist/`、`config/`、`logs/` 提交进仓库。
- 不要把 `keepOnTop()` 的 200ms 保活删掉或改成"检测到被遮挡才置顶"——后者已在托盘箭头、
  Win+D 等场景被证伪。也不要回到 50ms / 20Hz 的老频率。
- 不要在 `paintEvent` 里做注册表/磁盘 IO；采样类操作最多 1Hz，且要缓存。
- 不要删除 `docs/code-review-2026-11-19.md` 里的历史结论，追加而非覆盖。
