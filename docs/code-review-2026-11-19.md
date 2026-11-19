# NetSpeedMon 代码审查与整改方案

> 审查对象：`NetSpeedMon/`（commit `241c0c7`，master）
> 代码规模：`main.cpp` 1431 行 / 6 个类，`CMakeLists.txt` 57 行，`dist/` 二进制 82.3 MB
> 审查日期：2026-11-19
> 审查方式：全量源码走查 + 已发布二进制静态检查（未编译、未运行，见 §1.3）
> 状态：**已审核并实施**（逐条实施记录见 §7；本文档保留审查时的原始结论作为对照）

---

## 0. 摘要

**结论：这个项目"能跑"，但处于"靠补丁堆出来的可用状态"，存在 6 个必须修的功能性缺陷、9 个稳定性/兼容性隐患，以及一批工程债。** 其中 3 个问题会在常见场景下直接让用户认为"程序坏了"：

1. **托盘图标一旦关掉再打开，右键菜单和点击全部失效**（只能杀进程）——纯代码逻辑错误，100% 必现。
2. **配置和日志写在 exe 目录**，装到 `C:\Program Files\` 或任何非用户可写目录后，保存设置静默失败、开机自启静默失效，且没有任何提示。
3. **网速数字会失真**：PDH 与 GetIfTable 两个数据源每 tick 竞争、基线不共享，切换瞬间会把"过去几十秒累计的流量"当成"这一秒的速度"；GetIfTable 那条路还用了 32 位计数器求和 + 不除以真实耗时。README 第 269 行"差值减法不受 4GB 回绕影响"的技术说明本身是错的。

另外，**悬浮窗的"稳定性"是用 50ms 无条件 `SetWindowPos(HWND_TOPMOST)` 硬顶出来的**（20 次/秒），配合拦截 `WM_SHOWWINDOW`/`WM_WINDOWPOSCHANGING` 去对抗 Explorer，代价是 CPU 唤不醒深度睡眠、z-order 与其他置顶窗口互相踩、Qt 与 Win32 的窗口状态不一致。这块需要重新设计，不是打补丁。

**统计**：P0 6 条 / P1 9 条 / P2 9 条 / P3 5 条，共 29 条。

**建议先修的 5 条**：A1（托盘失效）、C1（配置路径）、B1（网速数据）、D1（高 DPI 坐标）、A2（退出路径）。
**改动最大、风险最高的一条**：A3（悬浮窗看门狗 → 事件驱动重写），需要真机验证。

---

## 1. 审查范围与方法

### 1.1 范围

| 对象 | 说明 |
|---|---|
| `main.cpp` | 全文件逐行走查（1431 行） |
| `CMakeLists.txt` | 构建配置、资源嵌入、链接参数 |
| `.gitignore` | 版本库卫生 |
| `README.md` / `README.en.md` | 与实现的一致性核对 |
| `dist/` | 已发布二进制：manifest、导入表、配置、日志 |
| Git 历史 | 提交内容与体积 |

### 1.2 方法

- 静态走查：控制流、资源生命周期、错误处理路径、跨 API 边界（Qt ↔ Win32）的语义差异。
- 二进制核对：确认 `dist/NetSpeedMon.exe` 与源码对应（导入 `GetIfTable`、`PdhAddCounterW`、`SetWindowPos` 均在），manifest 为 `asInvoker`、**不含 `dpiAware` 声明**（DPI 感知由 Qt 运行时设置）。
- 证据核对：`dist/logs/*.log` 与 `dist/config/settings.json` 作为真实运行产物，用于确认行为路径。

### 1.3 局限（请务必在看结论时打折的地方）

本机**没有** cmake / Qt / MinGW / MSVC（已确认 `Get-Command` 全部 NOT FOUND），因此：

- **未编译、未运行、未复现**。所有"会导致…"的判断都来自代码推理，不是实测。
- 涉及编译器/构建系统的结论（E1 的 RC 语言、`-static` 在 MSVC 下的行为）**必须在构建机上验证**，我标注了置信度。
- 涉及具体视觉表现的结论（D1 高 DPI、D5 裁切）标注了"推理结论，建议真机确认"。

---

## 2. 问题总览

| ID | 严重度 | 类别 | 位置 | 问题 | 建议动作 |
|---|---|---|---|---|---|
| A1 | **P0** | 功能 | `main.cpp:1296-1301`、`830-858`、`1339-1365` | 托盘图标重建后菜单/点击永久失效 | 图标内容与图标对象生命周期解耦 |
| A2 | **P0** | 生命周期 | `1149`、`1315-1322`、`1324-1327` | 两条退出路径不一致，主界面"退出"绕过清理（代码注释自承会卡死）；`cleanupAndQuit` 在自身事件中 `delete this` | 统一退出入口，延迟删除 |
| A3 | P1 | 稳定性 | `517-523`、`751-767`、`662-685` | 50ms 看门狗 + 无条件置顶 + 拦截隐藏，与 shell 对抗 | 改为事件驱动 + 1Hz 轻量校验 |
| A4 | P2 | 稳定性 | `1393-1401` | 单实例靠窗口标题查找，启动竞态时二次启动无反应 | 命名互斥 + 注册消息/QLocalServer |
| B1 | **P0** | 数据正确性 | `336-376`、`435-475` | 双数据源每 tick 竞争、基线不共享；GetIfTable 用 32 位计数、无耗时归一化 | 统一 `GetIfTable2`（64 位）+ 按接口差分 + `QElapsedTimer` |
| B2 | P1 | 数据正确性 | `314-327`、`379-433` | PDH 初始化后永不重建；接口增删/VPN 切换后计数器失效 | 校验失败即重建，或整体弃用 PDH |
| B3 | P2 | 数据正确性 | `396-409` | 用首个 `(`/`)` 解析实例名，重名实例（`#1`）被静默丢弃 | 改用通配符 + `PdhGetFormattedCounterArrayW` |
| B4 | P1 | 数据正确性 | `435-470` | 无耗时归一化；睡眠唤醒/暂停后单帧跳变 | 用实际 elapsed 计算速率 |
| C1 | **P0** | 配置 | `55-56`、`96`、`171-172` | 配置/日志写 exe 目录，只读目录下静默失败 | `QStandardPaths` + 可写性探测 + 回退 |
| C2 | P1 | 配置 | `127-177` | 非原子写、无校验、JSON 损坏静默重置、无失败反馈 | `QSaveFile` + 字段白名单/范围钳制 |
| C3 | **P0** | 配置 | `179-195`、`1406` | 自启命令行未加引号，含空格路径失效；每次启动都重写注册表 | 加引号、幂等、校验返回码 |
| C4 | P1 | 运维 | `90-104` | 日志无轮转/无上限，日积月累 | 保留 N 天 + 大小上限 |
| D1 | **P0** | 布局/DPI | `744-748`、`720`、`535-547` | 物理像素坐标与 Qt 逻辑坐标混用，dpr≠1 时尺寸/位置错乱 | 统一换算到逻辑坐标或全部物理坐标 |
| D2 | P1 | 逻辑 | `627-637` | 全屏判定用矩形包含，点桌面/最大化窗口误判 | 改用 `SHQueryUserNotificationState` |
| D3 | P1 | 兼容性 | `691-749`、`506-512` | 不支持竖排任务栏/副屏；Explorer 重启、DPI 变更、分辨率变更无处理 | 监听 `TaskbarCreated`/`WM_DISPLAYCHANGE`/`WM_DPICHANGED` |
| D4 | P2 | UI | `878-920` | 无最小尺寸，且尺寸被持久化 → 可永久拖坏界面 | `setMinimumSize` + 布局可压缩 |
| D5 | P2 | UI | `770-779`、`573-611` | 数值列宽硬编码 `" 888."`，KB/MB 模式高位数值被裁切 | 按实际文本测量最大宽 |
| D6 | P2 | UI/死代码 | `207-216`、`260-297` | "暗黑/简白"色值互换；`bg`/`accent` 未使用；`tbColor()` 完全未被调用；文字色不随任务栏明暗自适应 | 修正主题表 + 实现或删除自适应 |
| E1 | P1 | 构建 | `CMakeLists.txt:20`、`11-56` | `-static` 全局无条件（MSVC 直接链接失败）；未启用 RC 语言；无部署/安装规则；无警告开关 | 按编译器条件化 + `qt_generate_deploy_app_script` |
| E2 | P2 | 构建 | `main.cpp` 全文中文窄字符串 | 源文件无 BOM，MSVC 默认按 ANSI 码页解析 → 中文乱码 | 加 `/utf-8` |
| E3 | P2 | 维护 | `53`、`1155`、`1409`、`CMakeLists:30-46` | 版本号 4 处不一致（1.0.0.0 / v4 / 2026.5 / README v5） | 单一版本源 |
| E4 | P2 | 工程 | 全部 | 1431 行单文件、6 个类、无测试、无 CI、无格式化配置 | 分层拆分 + QTest + CI |
| E5 | P1 | 仓库 | `.gitignore`、`dist/` | `.gitignore` 是 Flash Builder 模板；82.3 MB 二进制 + 用户配置 + 日志已入库（.git 33.4 MB） | 重写 `.gitignore`，`dist/` 出库 |
| E6 | P3 | 代码质量 | `11-43`、`45-46`、`1048`、`535` | 重复 include、未用 include、`#pragma comment(lib)` 冗余、魔法数字 75/50 | 清理 |
| F1 | P2 | 文档 | `README.md` | 漂移：引用不存在的 `DEVELOPMENT.md`/`bin/`/`release/`；"4GB 回绕不影响"结论错误；主题表如实记录了反色；附录 A3 高 DPI 断言未验证 | 重写 §3/§4/§11/§12 |
| F2 | P3 | 文档 | `README.en.md` | 英文 README 是 Gitee 模板占位（标题还是中文，内容 `xxxx`） | 重写或删除 |
| F3 | P3 | i18n | 全文 | 无 `tr()`，全中文硬编码 | 迁移到 `tr()` + QTranslator |
| F4 | P3 | 产品 | `933-940`、`862`、`1193-1212` | "单位"设置只对悬浮窗生效，主界面/托盘恒为 auto；保存无反馈；无恢复默认 | 统一格式化入口 + 保存提示 |
| F5 | P3 | 产品 | — | 无异常处理/崩溃转储；无签名（README 自承杀软误报） | 可选：minidump + 签名 |

---

## 3. 详细分析

### A. 稳定性与生命周期

#### A1【P0】托盘图标重建后菜单与点击永久失效

**位置**：`Tray::rebuildIcon()` @830-858，`App::setupTray()` @1339-1371，`App` 的 `configApplied` @1293-1307

**证据**：`rebuildIcon()` 每次都是 **销毁旧 `QSystemTrayIcon` 并 new 一个新的**：

```cpp
void rebuildIcon() {
    if (m_icon) { m_icon->deleteLater(); m_icon = nullptr; }
    m_icon = new QSystemTrayIcon(this);   // 全新对象
    ...
    m_icon->setIcon(icon);
}
```

而右键菜单和点击事件只绑定在 `App` 构造时的那一个对象上：

```cpp
m_tray->icon()->setContextMenu(menu);                       // 1351
QObject::connect(m_tray->icon(), &QSystemTrayIcon::activated, ...); // 1354
```

调用点：

```cpp
if (showTray && !m_tray->icon()->isVisible()) {
    m_tray->rebuildIcon();   // ← 新对象，没有任何菜单/连接
    m_tray->setVisible(true);
}
```

**影响**：用户在设置里取消勾选"显示托盘图标"再勾回来 → 托盘出现一个**没有右键菜单、左键无反应**的图标。此时主窗口如果已经关到托盘，程序唯一入口消失，只能任务管理器杀进程。这是最容易被用户报告为"程序卡死/崩溃"的问题，且 100% 必现。

**方案（推荐）**：把"图标内容"与"图标对象生命周期"解耦 —— 托盘对象只创建一次，之后只换 `QIcon`：

```cpp
// Tray 内部自己建菜单、自己连信号（App::setupTray 的逻辑搬进来）
Tray::Tray(QObject* parent) : QObject(parent) {
    m_menu = new QMenu;                     // 归属 Tray，随 Tray 析构
    m_menu->addAction("打开主界面", this, &Tray::openRequested);
    ...
    m_icon = new QSystemTrayIcon(this);     // 只建一次
    m_icon->setContextMenu(m_menu);
    connect(m_icon, &QSystemTrayIcon::activated, this, &Tray::onActivated);
    m_icon->setIcon(makeIcon());            // 随时可换
}
void Tray::rebuildIcon() { m_icon->setIcon(makeIcon()); }   // 不再 new
```

同时删除 `App::setupTray()` 里对 `m_tray->icon()` 的外部连接，改为 `Tray` 发出 `openRequested()` / `quitRequested()` 由 `App` 接。

**验证**：设置里关闭托盘 → 打开托盘 → 右键菜单正常、左键能唤出主界面；重复 10 次不泄漏（任务管理器看句柄数稳定）。

**顺带**：`setupTray()` 里的 `new QMenu()` 没有父对象（@1340），是确定的泄漏；`Win::refreshTrayIcon()` @944 从未被调用（死代码）。

---

#### A2【P0】退出路径不一致 + 在自身事件中删除自身

**位置**：`main.cpp:1149`（退出按钮）、`1315-1322`（`cleanupAndQuit`）、`1324-1327`（`~App`）、`613-623`（`Ov::contextMenuEvent`）

**证据**：`Ov` 的 `nativeEvent` 会拦截"非自主隐藏"（@676-683），因此销毁窗口前必须调 `prepareDestroy()` 关掉拦截 —— 这一点代码注释自己承认了：

```cpp
// 退出前清理悬浮窗（解除 nativeEvent 拦截，避免销毁时卡死）
void cleanupAndQuit() { if (m_ov) { m_ov->prepareDestroy(); delete m_ov; m_ov = nullptr; } QApplication::quit(); }
```

但主界面的"退出"按钮走的是另一条路：

```cpp
auto* btnQuit = new QPushButton("退出");
connect(btnQuit, &QPushButton::clicked, qApp, &QApplication::quit);   // 1149
```

→ `exec()` 返回 → `App` 析构 → `delete m_ov` **没有** `prepareDestroy()`。也就是说：从托盘退出（安全路径）和从主界面退出（危险路径）行为不同，后者正是注释里提醒的卡死场景。

**其次**：`Ov::contextMenuEvent` 里 `emit quitApp()` → `App::cleanupAndQuit` → `delete m_ov`，即 **在自己成员函数执行期间 delete this**，之后 `contextMenuEvent` 继续返回，属于 use-after-free。当前因为 `WA_TransparentForMouseEvents` + `HTTRANSPARENT`（@500、@672）这条右键路径根本收不到鼠标事件，所以是"潜伏"缺陷；但一旦以后给悬浮窗加交互就会立刻踩到。

**方案**：

1. 单一退出入口：`App::quit()`，内部 `m_ov->prepareDestroy(); m_ov->hide(); m_ov->deleteLater();` + `QCoreApplication::quit();`；按钮、托盘、悬浮窗全部连到它。
2. `QApplication::setQuitOnLastWindowClosed(false)`（显式声明"托盘常驻"的意图，避免将来加窗口时行为漂移）。
3. 删除 `Ov::contextMenuEvent`（死代码），或在其中改用 `QTimer::singleShot(0, ...)` 触发退出。
4. 更彻底的做法见 A3：不再拦截隐藏，`prepareDestroy` 这类"解扣"逻辑就不需要存在。

**验证**：主界面点"退出"、托盘右键"退出"、任务管理器结束任务，三种方式均无残留进程、无 UAC/挂了不动的窗口；连续开关 20 次无句柄增长。

---

#### A3【P1】悬浮窗用 50ms 无条件置顶与 shell 对抗

**位置**：`Ov` 构造函数 @517-523、`onWatchdog()` @751-767、`nativeEvent` @662-685、`onRelocate()` @691-749

**证据**：

```cpp
connect(&m_watchdogTimer, &QTimer::timeout, this, &Ov::onWatchdog);
m_watchdogTimer.start(50);                                     // 20 次/秒

void onWatchdog() {
    ...
    if (!(ex & WS_EX_TOPMOST)) SetWindowLongPtrW(...);           // ① 改样式
    if (!IsWindowVisible(hw)) ShowWindow(hw, SW_SHOWNOACTIVATE); // ② 强行显示
    SetWindowPos(hw, HWND_TOPMOST, 0,0,0,0,                      // ③ 无条件重新置顶
                 SWP_NOACTIVATE|SWP_NOSIZE|SWP_NOMOVE);
}
```

叠加 200ms 的 `onRelocate` 与 `FindWindowW`/`GetWindowRect`，程序**空闲时也保持约 25 次/秒的系统唤醒**，并且每 50ms 强制把一个窗口提到 topmost 带顶端。

**影响**：

- **功耗**：笔记本上持续 25Hz 唤醒会显著抬高待机功耗（这是网速悬浮窗这类"常驻小工具"最被诟病的点）。
- **副作用**：无条件 `SetWindowPos(HWND_TOPMOST)` 会改变其他置顶窗口的相对顺序（如某些播放器、输入法候选窗），表现为"别的窗口莫名被压下去"。
- **与 shell 对抗**：② 会在用户按 Win+D（显示桌面）、切换虚拟桌面、全屏播放、锁屏等场景把窗口强行拉回来；③ 每 50ms 与 Explorer 的 z-order 管理打架。
- **状态不一致**：`nativeEvent` 里对 `WM_SHOWWINDOW(FALSE)` 返回 `*result=0; return true` 并清除 `SWP_HIDEWINDOW`，等于对 Qt 撒谎 —— Win32 层"没隐藏"，Qt 层"已隐藏/未知"，后续 `isVisible()`、`show()`、`hide()` 的语义都不可靠（`prepareDestroy` 这个"手动解扣"就是这个谎言的代价）。
- **Qt 官方语义**：`WindowStaysOnTopHint` + `WS_EX_NOACTIVATE` 已经足够表达"置顶不走焦点"，连续 20Hz 强制置顶属于与窗口管理器的军备竞赛。

**方案（推荐，分两步走）**：

*第一步（低风险，1 天内）：*
- 看门狗改为 **1Hz 轻量校验**（`IsWindowVisible` + `GetWindowLongPtr` 比对），**只在检测到异常时**才调一次 `SetWindowPos`，并在调用前比较 `GetWindow(hw, GW_HWNDPREV)` 是否已经是期望状态。
- 用 `SetTimer`/`QTimer::setTimerType(Qt::CoarseTimer)` 允许系统合并唤醒。
- 位置跟随改成 **事件驱动**：`SetWinEventHook(EVENT_OBJECT_LOCATIONCHANGE, ...)` 监听任务栏，或监听 `WM_SETTINGCHANGE` + 注册 `TaskbarCreated` 消息，替代 200ms 轮询。

*第二步（需要真机验证，2-3 天）：* 按 TrafficMonitor 的成熟做法重构 —— 在 Win10 及更早把窗口 `SetParent` 到任务栏的容器区；Win11 下把窗口设为 `Shell_TrayWnd` 的子窗口，用 `TrayNotifyWnd`（通知区）和 `Start`（开始按钮）两个锚点的矩形变化来定位，并缓存 DPI。参考：[TrafficMonitor 任务栏窗口实现机制](https://opendeep.wiki/zhongyang219/TrafficMonitor/3.3-taskbar-window-implementation)。这样"永不消失"由父窗口关系保证，不再需要看门狗和隐藏拦截。

*备选（如果不想动窗口模型）*：保持独立置顶窗，但接受"Win+D/全屏时会被隐藏"，改为在 `EVENT_SYSTEM_FOREGROUND` 与 `TaskbarCreated` 时恢复 —— 这比拦截隐藏更符合平台约定，代码量最小。

**验证**：任务管理器观察 CPU 空闲占用（目标 <0.1%）、`powercfg /energy` 无高频定时器告警；Win+D、全屏视频、锁屏、切换虚拟桌面、任务栏自动隐藏、重启 Explorer 各场景下悬浮窗行为符合预期且无闪烁。

---

#### A4【P2】单实例与启动竞态

**位置**：`main.cpp:1393-1401`

**证据**：

```cpp
HANDLE hMutex = CreateMutexW(nullptr, FALSE, MUTEX_NAME);
if (GetLastError() == ERROR_ALREADY_EXISTS) {
    HWND hWnd = FindWindowW(nullptr, WIN_TITLE);   // 按标题找窗口
    if (hWnd) { ShowWindow(hWnd, SW_RESTORE); SetForegroundWindow(hWnd); }
    return 0;                                       // 找不到就"什么都不发生"
}
```

**影响**：
- 第二个实例启动时如果第一个实例还在构造窗口（`App` 构造要建 4 个对象 + 初始化 PDH，可能几十 ms），`FindWindowW` 返回空 → 用户双击图标"毫无反应"。
- 窗口标题里带版本号和版权信息（`WIN_TITLE`），以后改文案就会破坏单实例联动。
- `SetForegroundWindow` 受前台锁限制，可能不生效（应配合 `AllowSetForegroundWindow`）。
- `CreateMutexW` 失败（返回 NULL）时 `GetLastError` 可能是上一次的错误码，判断不可靠。

**方案**：命名互斥只做"唯一性判断"，激活已有实例用**注册窗口消息**（`RegisterWindowMessage(L"NetSpeedMon_Show")` + `PostMessage(HWND_BROADCAST, ...)`，目标窗口在 `nativeEventFilter` 里响应）或 `QLocalServer/QLocalSocket`（Qt 原生、跨会话更稳）。第二个实例可用短重试（比如 2s 内每 100ms 发一次广播）。

**验证**：连点 10 次图标，每次都能把主界面拉到前台；`--min` 启动后再次双击，主界面应被唤出。

---

### B. 网速数据正确性

#### B1【P0】双数据源竞争 + 32 位计数器 + 无耗时归一化

**位置**：`onPoll()` @336-376，`getIfTablePoll()` @435-475

**证据 1 —— 两个源每 tick 都在抢，基线互不共享**：

```cpp
if (PdhGetFormattedCounterValue(...) == ERROR_SUCCESS && v.doubleValue > 0) { totalUp += ...; hasData = true; }
...
if (hasData) { ...; if (changed) { ...; return; }          // 有流量 → 用 PDH
              if (m_pdhStaleCount < 3) { ...; return; } }
getIfTablePoll();                                          // 没流量/冻结 → 用 GetIfTable
```

`hasData` 要求**值 > 0**，所以**只要网卡空闲满 1 秒就一定切到 GetIfTable**；而 GetIfTable 的 `prevIn/prevOut` 是 `static` 局部变量（@436-437），**只在走这条路时更新**。

于是出现这个典型序列：空闲（GetIfTable 基线停留在 T0）→ 下载 100 MB（PDH 正常显示）→ 网络重新空闲 → `hasData=false` → 第一次 GetIfTable 差分 = **T0 到现在的全部累计流量**，被当作"1 秒的速度"显示 → 悬浮窗瞬间蹦出一个巨大的假数字。

**证据 2 —— 32 位计数器求和 + 错误注释**：

```cpp
totalIn += row.dwInOctets;      // MIB_IFROW::dwInOctets 是 DWORD(32 位)
...
// 处理 DWORD 溢出：差值计算不受 4GB 回绕影响
m_downSpeed = (totalIn >= prevIn) ? (totalIn - prevIn) : (totalIn + (0xFFFFFFFFULL - prevIn));
```

三个问题：
- 千兆链路下单个接口 ~35 秒就回绕 4 GiB，而这里**先把多个接口的 32 位计数器相加**再差分，任何一个接口回绕都会让"总和"的语义失效。
- 回绕补偿写错了：正确增量是 `0x100000000ULL + totalIn - prevIn`，不是 `totalIn + (0xFFFFFFFF - prevIn)`（差 1，且前提是"整体只回绕一次"——求和后这个前提不成立）。
- `m_upSpeed/m_downSpeed` 是"两次调用之间的字节差"，**没有除以实际经过的秒数**。`QTimer` 默认 CoarseTimer 允许 ±5% 抖动，睡眠唤醒后一次 tick 可能跨几分钟，全部被当成 1 秒。

**影响**：数字会"撒谎"，且撒谎的时刻恰恰是用户最会注意的时刻（刚下载完、刚唤醒、刚切网）。README §4.2 的这段技术说明会被当作正确结论传播，实际是错的。

**方案（推荐：只用 GetIfTable2，删掉 PDH）**：

```cpp
struct IfDelta { quint64 in = 0, out = 0; };
QHash<quint64, IfDelta> m_prev;        // key = InterfaceLuid.Value，按接口记忆
QElapsedTimer m_clock;                 // 记录真实间隔

void poll() {
    const qint64 ms = m_clock.restart();               // 实际经过毫秒
    MIB_IF_TABLE2* t = nullptr;
    if (GetIfTable2(&t) != NO_ERROR) return;           // 出错直接返回，不清零
    quint64 dIn = 0, dOut = 0;
    for (ULONG i = 0; i < t->NumEntries; ++i) {
        const MIB_IF_ROW2& r = t->Table[i];
        if (r.Type == IF_TYPE_SOFTWARE_LOOPBACK ||
            r.Type == IF_TYPE_TUNNEL ||
            r.OperStatus != IfOperStatusUp) continue;
        const quint64 key = r.InterfaceLuid.Value;
        auto it = m_prev.find(key);
        if (it != m_prev.end()) {
            dIn  += (r.InOctets  >= it->in)  ? r.InOctets  - it->in  : r.InOctets;  // 计数重置则按重置处理
            dOut += (r.OutOctets >= it->out) ? r.OutOctets - it->out : r.OutOctets;
        }
        m_prev[key] = { r.InOctets, r.OutOctets };
    }
    FreeMibTable(t);
    if (ms > 0) {
        m_down = quint64(dIn  * 1000.0 / ms);
        m_up   = quint64(dOut * 1000.0 / ms);
    }
}
```

要点：
- `MIB_IF_ROW2::InOctets/OutOctets` 是 **ULONG64**，不存在 4 GiB 回绕（[MS 文档](https://learn.microsoft.com/en-us/windows/win32/api/netioapi/ns-netioapi-mib_if_row2)）。
- 按 `InterfaceLuid` **逐接口**差分，接口增删/重连不会污染其他接口。
- 用真实经过时间做归一化，唤醒后不会爆表。
- 只保留一个数据源，**没有切换就没有基线不连续的问题**。PDH 在这里没有带来任何能力（它自己也是从同一份网卡统计算速率），却带来了实例名解析、本地化、动态接口失效三份复杂度。删除 PDH 可一并消掉 B2/B3。

*备选（若坚持保留 PDH）*：保留 PDH 但**基线共享**——GetIfTable 与 PDH 都用"累计字节 + 时间戳"，切换时只换累计值来源并把基线重置为当前值（第一帧输出 0 而不是巨大值），并且**只在一个 tick 里选一次源**（例如启动时选一次，失败才降级，不每 tick 重选）。

**验证**：`iperf3`/大文件下载期间对比任务管理器"以太网"图表；下载结束瞬间无假尖峰；睡眠 5 分钟后唤醒无尖峰；拔插网线、切 Wi-Fi、连 VPN 后数字连续不跳变；`KB`/`MB` 模式下高位数字也正确。

---

#### B2【P1】PDH 初始化后永不重建

**位置**：`start()` @316-327，`initPdh()` @379-433

**证据**：`m_query` 与 `m_upCs/m_dnCs` 只在 `start()` 里建一次；之后没有任何 `PdhValidatePath`、没有重扫实例、没有对 `PDH_CSTATUS_INVALID_DATA` 的处置。接口列表变化（插拔网卡、Wi-Fi 切换、VPN 连接、驱动重启）后，旧实例名不再存在，PDH 永久返回失败 → 永久走 GetIfTable（引入 B1 的所有问题），或者反过来永久冻结。

**方案**：若按 B1 只留 GetIfTable2，本条自动消失。若保留 PDH：在 `PdhGetFormattedCounterValue` 连续 N 次返回非 `PDH_CSTATUS_VALID_DATA` 时，关闭查询并重新 `initPdh()`（带退避，避免刷日志）。

**验证**：运行中禁用/启用网卡、连接/断开 VPN，观察日志中 PDH 是否自动重建、数值是否恢复。

---

#### B3【P2】PDH 实例名解析脆弱

**位置**：`initPdh()` @396-409、@423-428

**证据**：

```cpp
int l = path.indexOf('(');
int r = path.indexOf(')', l);          // 第一个 '(' 到第一个 ')'
QString inst = path.mid(l + 1, r - l - 1);
```

- 实例名里带 `(`/`)`（虚拟网卡、部分厂商驱动会这样）就会截断；
- PDH 对重名实例要求带 `#index` 后缀，这里解析时**丢掉**了后缀，`PdhAddCounterW` 对第二个重名实例直接失败并被 `continue` 静默跳过；
- `PdhAddCounterW` 失败时已加入的另一个计数器不回滚（`hU` 成功 `hD` 失败），查询里残留无用计数器。

**方案**：不手工解析。用 `PdhAddCounterW(m_query, L"\\Network Interface(*)\\Bytes Sent/sec", 0, &h)` 添加**通配符计数器**，取值时用 `PdhGetFormattedCounterArrayW` 一次性拿到所有实例（含重名与动态实例），既不需要字符串切割，也不需要自己过滤 `#n`。参考：[PdhGetFormattedCounterArrayW](https://learn.microsoft.com/en-us/windows/win32/api/pdh/nf-pdh-pdhgetformattedcounterarrayw)。
（再次说明：按 B1 删除 PDH 后本条不需要修。）

---

#### B4【P1】无耗时归一化，暂停/唤醒后单帧跳变

**位置**：`getIfTablePoll()` @463-470

**证据**：`m_downSpeed = totalIn - prevIn;` —— 变量名是 `Speed`（字节/秒），但两侧都是**字节**。只有当两次调用严格间隔 1.000 秒时这个值才等于速率。

**影响**：定时器抖动、UI 线程繁忙、系统睡眠、断点调试、`--min` 后台长时间挂起后第一次刷新，都会出现明显的数字异常。

**方案**：B1 的方案里已经用 `QElapsedTimer` 解决（速率 = 增量/真实秒数）。若暂不改架构，最小修复是加一个 `QElapsedTimer` 并在算完后 `* 1000 / elapsed`。

**验证**：用 `Debug` 断点把主线程阻塞 5 秒再继续，恢复后第一帧数字不应异常。

---

### C. 配置与文件

#### C1【P0】配置与日志写在 exe 目录

**位置**：`exeDir()` @65-75、`CFG_PATH` @55、`Lg::write` @90-104、`Config::save()` @171-177

**证据**：

```cpp
static const char* CFG_PATH = "config/settings.json";
QDir dir(exeDir() + "/config");  if (!dir.exists()) dir.mkpath(".");
QFile file(cfgFullPath());
if (file.open(QIODevice::WriteOnly)) { file.write(...); }   // 失败无任何反馈
```

**影响**：装到 `C:\Program Files\NetSpeedMon\`（Windows 默认安装位置）或任何 `Program Files` 子目录时，普通权限进程**无法创建/写入** `config/` 与 `logs/`。后果：

- 用户改完设置点"保存"，界面无任何提示，日志文件也写不出来（因为同上），**配置在下一次启动时全部丢失**；
- 开机自启写注册表指向 exe，但 `--min` 启动后同样读不到配置，用户会看到"设置项全是默认值"；
- 这正是 README §13 描述的"便携版"假设，但产品里没有说明"必须放在可写目录"，也没有失败兜底。

**方案**：

```cpp
// 1) 首选用户配置目录
QString cfgDir = QStandardPaths::writableLocation(QStandardPaths::AppConfigLocation);  // %LOCALAPPDATA%/NetSpeedMon
QString logDir = QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation) + "/logs";
// 2) 便携优先：exe 同级存在 settings.json（或 config/）且目录可写 → 用便携
// 3) 探测可写性用 QTemporaryFile(cfgDir + "/.probe")
// 4) 两处都不可写 → 弹出一次提示（QMessageBox），并降级为"仅本次会话生效"
```

`QCoreApplication::applicationDirPath()` 也能顺便替掉手写 `GetModuleFileNameW`（@65-75 还有 `MAX_PATH` 截断风险，长路径下会拿到截断字符串）。

**验证**：把 dist 复制到 `C:\Program Files\NetSpeedMon\`，以普通用户运行 → 能保存设置、能在 `%LOCALAPPDATA%\NetSpeedMon\` 找到配置和日志；把整个目录设为只读 → 有明确提示而不是静默失败。

---

#### C2【P1】配置非原子写、无校验、损坏静默重置

**位置**：`load()` @127-150、`save()` @152-177

**证据**：
- `QJsonDocument::fromJson(file.readAll()).object()` 不检查 `error`，文件损坏时得到空对象 → 全部回退默认值且**不重新写回**，用户看不到任何异常。
- 写入没有 `QSaveFile`，写一半断电/崩溃会留下半截文件（下次启动即触发上一条）。
- 无范围校验：`fs` 被手工改成 0 或 200，`Ov` 会直接用它构造 `QFont`（@535、@558、@578）→ 字体尺寸异常；`ww/wh` 无下限；`theme`/`um`/`tp`/`fsc` 只在 `loadConfigToUI()` 里做了部分回退（@1171-1179），而 `Ov` 在构造时（@505-512）就用上了这些值。
- `wx/wy > 0` 判断（@914）会丢弃 `x==0` 的合法位置；同时**不校验位置是否落在当前显示器范围内** → 换显示器/拔掉外接屏后窗口出现在屏幕外，用户以为程序没启动。

**方案**：
- 用 `QSaveFile` + `commit()` 保证原子性；写入失败返回 bool 并提示。
- `load()` 里做"字段级校验 + 钳制"：`fs` 钳到 `[8,48]`，`ww/wh` 钳到最小尺寸，枚举值落在白名单内，颜色用 `QColor::isValidColor()` 校验（`m_cfg->fc` 非法时 `QColor(m_cfg->fc)` 会产生无效色，`QPen` 用它等于画不出来）。
- 位置恢复时用 `QGuiApplication::screenAt()` / 与 `QScreen::availableGeometry()` 求交，落不到任何屏幕上就居中。
- 文件损坏时把坏文件重命名为 `.bak` 并写日志，让用户/我们可诊断。

**验证**：手动把 `settings.json` 截断成 `{"ff":` → 启动不崩、生成 `.bak`、日志有记录；把 `fs` 改成 `0`/`999` → 界面正常；把 `wx` 设成 `-3000` → 窗口仍在可见区域。

---

#### C3【P0】开机自启未加引号 + 每次启动都写注册表

**位置**：`applyAutoStart()` @179-195，`main()` @1406

**证据**：

```cpp
wchar_t buf[MAX_PATH + 8];
GetModuleFileNameW(nullptr, buf, MAX_PATH);
wcsncat_s(buf, L" --min", _TRUNCATE);
RegSetValueExW(hKey, L"NetSpeedMon", 0, REG_SZ, (BYTE*)buf, (DWORD)(wcslen(buf) * 2 + 2));
```

写入的是 `C:\Program Files\NetSpeedMon\NetSpeedMon.exe --min`，**路径没有引号**。Windows 解析 Run 项时会在第一个空格处断开，实际执行 `C:\Program.exe`（经典跑道攻击面，且启动必然失败）。另外 `main()` @1406 **每次启动都调一遍**，即使 `as=false` 也会去 `RegDeleteValueW`；注册表操作无任何返回值检查与日志。

**影响**：自启在路径含空格时 100% 失效（而安装到 `Program Files` 是默认情形）；`MAX_PATH` 截断后写入的是坏路径；失败无感知。

**方案**：

```cpp
QString cmd = QString("\"%1\" --min").arg(QDir::toNativeSeparators(QCoreApplication::applicationFilePath()));
// 仅在值真的不同才写；写失败 LOG + 在设置界面提示
LSTATUS st = RegSetValueExW(hKey, L"NetSpeedMon", 0, REG_SZ,
                            reinterpret_cast<const BYTE*>(cmd.utf16()),
                            DWORD((cmd.size() + 1) * sizeof(wchar_t)));
```

并改为"读取现值 → 与期望值比较 → 不同才写"。程序启动时不必再写一次（只在用户保存设置时写）。

**验证**：装到含空格的路径 → 勾选自启 → 注销重登，程序静默启动；`reg query HKCU\Software\Microsoft\Windows\CurrentVersion\Run /v NetSpeedMon` 可见带引号的值。

---

#### C4【P1】日志无轮转

**位置**：`Lg::write()` @90-104（另：`dist/logs/` 里已经躺着两份历史日志）

**证据**：按天分文件，但从不清理；每次写入都 `mkpath` + `open` + `close`（三条系统调用/条日志）。

**方案**：保留最近 30 天（启动时扫一遍 `logs/*.log` 删旧的）；单文件超过 5 MB 切分；目录不可写时降级为 `qWarning` 输出并不再重试。

**验证**：伪造 40 天日志 → 启动后只剩 30 天。

---

### D. 窗口、DPI 与布局

#### D1【P0】物理像素与 Qt 逻辑坐标混用（高 DPI 下必坏）

**位置**：`Ov::recalcLayout()` @535-547、`updateSpeed()` @555-570、`onRelocate()` @691-749

**证据**：

```cpp
setFixedSize(qMax(fw, 180), qMax(fh, 22));      // Qt 逻辑像素（Qt6 高 DPI 开启）
...
int w = width(), h = height();                  // 逻辑像素
SetWindowPos(hw, HWND_TOPMOST, x, y, w, h, ...); // 物理像素！
```

同时 `x/y` 来自 `GetWindowRect(Shell_TrayWnd)`（**物理**像素），而 `m_numAreaW` 等来自 `QFontMetrics`（**逻辑**像素），`x` 里还减了 `QFontMetrics(...).horizontalAdvance("  ")`（逻辑）—— 三种坐标系混在一个表达式里。

**影响**（推理，需真机确认具体表现）：在 125% / 150% 缩放下，窗口的**物理**尺寸被设成逻辑尺寸（相当于缩小到 1/1.5），Qt 随即收到 `WM_SIZE` 更新自己的逻辑几何，`width()` 变小，下一轮 `m_lastW != w` 再次 `SetWindowPos`…… 结果是**尺寸持续缩水或与 Qt 的布局互相拉扯**，表现为文字被裁切、闪烁、位置偏移。而 `main()` 里显式设了 `PassThrough` 舍入策略（@1386-1387），让 1.25/1.5 这类非整数比例确实存在 —— 恰恰是这个 bug 的高发区。

README 附录 A3 写的是"在 125%/150% 缩放下应正常工作"，这是**未经验证的断言**。

**方案（推荐：全部用 Qt 逻辑坐标）**：

```cpp
// 把任务栏的物理矩形换算到逻辑坐标（注意：要用任务栏所在屏幕的 dpr）
QScreen* s = QGuiApplication::screenAt(QPoint(tb.left, tb.top));
const qreal dpr = s ? s->devicePixelRatio() : 1.0;
QRect tbLogical = QRect(QPoint(tb.left / dpr, tb.top / dpr),
                        QSize((tb.right - tb.left) / dpr, (tb.bottom - tb.top) / dpr));
// 之后的定位全部用 Qt：move()/resize()/setGeometry()，不再直接调 SetWindowPos 改几何
```

置顶只保留一次 `SetWindowPos(hw, HWND_TOPMOST, 0,0,0,0, SWP_NOMOVE|SWP_NOSIZE|SWP_NOACTIVATE)`，或干脆用 `setWindowFlag(Qt::WindowStaysOnTopHint, true)`。**几何一律交给 Qt**，Win32 只负责"是否有 topmost 标志"。

另需把 `calcColumnWidths` 的输入统一成"当前逻辑字体"，避免 `paintEvent` 里用带 bold 的字体、`recalcLayout` 里用不带 bold 的字体，两处算出的列宽不一致（@535-537 vs @578-580）。

**验证**：在 100% / 125% / 150% / 200% 四档缩放、单屏与双屏混合缩放下，悬浮窗尺寸与文字完整、位置贴合任务栏；拖动任务栏位置（上/下）后立即跟随。

---

#### D2【P1】全屏检测用"矩形包含"判断，误判率高

**位置**：`isFullscreenNow()` @627-637

**证据**：

```cpp
HWND hFg = GetForegroundWindow();
RECT tb, fg; GetWindowRect(hTask,&tb); GetWindowRect(hFg,&fg);
return fg.left <= tb.left && fg.top <= tb.top && fg.right >= tb.right && fg.bottom >= tb.bottom;
```

**影响**（推理）：
- **点击桌面**时前台窗口是 `Progman`/`WorkerW`，它覆盖整个屏幕含任务栏区域 → 判定为"全屏" → 悬浮窗**被隐藏**，用户点一下桌面数字就没了，直到切到别的窗口才回来。
- **最大化窗口**在"任务栏自动隐藏"时占满全屏 → 误判。
- 副屏全屏、多显示器横跨时，只拿主任务栏矩形比较 → 漏判/误判。
- 反过来，真正的全屏独占应用（Direct3D）在某些情况下 `GetForegroundWindow` 返回 0 → 漏判，悬浮窗压在游戏画面上。

**方案**：改用系统提供的权威判断：

```cpp
QUERY_USER_NOTIFICATION_STATE st;
if (SUCCEEDED(SHQueryUserNotificationState(&st)))
    return st == QUNS_BUSY || st == QUNS_RUNNING_D3D_FULL_SCREEN || st == QUNS_PRESENTATION_MODE;
```

`QUNS_BUSY` 明确表示"全屏应用/演示中，不应弹通知"（[MS 文档](https://learn.microsoft.com/en-us/windows/win32/api/shellapi/nf-shellapi-shqueryusernotificationstate)）。同时把"是否隐藏"改为状态机（进入/退出各触发一次 `Show/Hide`），避免 200ms 反复 `ShowWindow`。

**验证**：点桌面、最大化窗口、自动隐藏任务栏、播放全屏视频、全屏游戏、演示模式，逐项确认悬浮窗的显隐符合预期。

---

#### D3【P1】竖排任务栏/副屏/Explorer 重启/DPI 变更均未处理

**位置**：整类 `Ov` 定位逻辑（@691-749），任务栏句柄获取（@506、@694、@713-714）

**证据**：
- 位置计算假定任务栏水平：`y = tb.top + (tbH - h)/2`；`left` 模式硬编码 `tb.left + 160 + margin`（@709）—— 竖排任务栏下 `tbH` 是整屏高度，悬浮窗会跑到任务栏列的垂直中央；`left+160` 可能直接出屏。
- 只用 `Shell_TrayWnd`（主屏任务栏），副屏的 `Shell_SecondaryTrayWnd` 完全不考虑 → 悬浮窗永远只在主屏。
- 没有监听 `RegisterWindowMessage(L"TaskbarCreated")`：**重启 explorer.exe 后 `FindWindowW` 拿到新句柄，但定位计时器每 200ms 重新找，所以这一点侥幸能恢复；然而 `m_lastX/m_lastY` 等缓存不会失效，且期间做过 `ShowWindow` 强制显示，容易出现"窗口在其他位置闪过"**。
- 没有处理 `WM_DISPLAYCHANGE` / `WM_DPICHANGED` / `WM_SETTINGCHANGE`：改分辨率、改缩放、任务栏换边后，字体度量与定位都不会重算（`recalcLayout` 只在保存设置时调用）。

**方案**：
- 用 `QVariantMap` 式的"屏幕信息快照"：监听 `QGuiApplication::screenAdded/screenRemoved/screenGeometryChanged/logicalDotsPerInchChanged`，变化时调用 `recalcLayout()` + 重新定位。
- 监听 `TaskbarCreated` 重新获取任务栏句柄并清空位置缓存。
- 明确支持策略：v1 显式"仅主屏 + 仅水平任务栏"（在设置里置灰并写进 README），或者按 TrafficMonitor 的做法做双实现（见 A3）。

**验证**：任务栏换到顶部/左侧、切主副屏、改 125%→150%、重启 explorer.exe，四种场景各观察一次。

---

#### D4【P2】主窗口无最小尺寸，尺寸还会被持久化

**位置**：`Win` 构造函数 @878-920、`~Win()` @922-931

**证据**：三张卡片横向等宽（@1134-1136），最小可读宽度约 520px；但没有任何 `setMinimumSize`，而 `~Win()` 把当前 `size()` 写回配置并在下次启动时 `resize()`。用户把窗口拖到 200×100 → 界面挤成一团 → 退出 → **永久记住这个尺寸**，下次启动依然挤。

**方案**：`setMinimumSize(560, 320)`；卡片行可加 `QScrollArea` 或允许换行；`resize()` 前与 `minimumSize()` 求 `expandedTo()`。

**验证**：把窗口拖到最小、手动改配置 `ww=200` → 启动后仍可用。

---

#### D5【P2】数值列宽硬编码，高位数值被裁切

**位置**：`calcColumnWidths()` @770-779

**证据**：

```cpp
m_numAreaW = fm.horizontalAdvance(" 888.");   // 固定 5 个字符宽
```

而 `formatSpeedParts()`（@224-249）在 `KB` 模式下把 100 MB/s 格式化为 `102400`（6 字符，≥100 时 0 位小数），`short` 模式还会出现 `1023`。`paintEvent` 用 `drawText(QRect(...), Qt::AlignRight, ...)` —— **Qt 会按 rect 裁剪**，于是数字左边被切掉，显示成 `02400` 之类。

同时 `m_numAreaW` 只按 `" 888."` 估，`MB/s` 与 `B/s` 的宽度差靠 `qMax` 兜（@775-777）尚可，但数字区不够。

**方案**：按**所有模式下的理论最长串**测量并留余量，例如

```cpp
m_numAreaW = fm.horizontalAdvance(" 8888.8") + fm.horizontalAdvance("0");  // 或实测 "102400"
```

更稳的做法：不做固定列宽，改为每次 `paintEvent` 用 `fm.horizontalAdvance(spUp.num)` 计算右对齐位置（1Hz 的代价可忽略），或在数字变化导致宽度跨档时才 `setFixedSize`。

**验证**：`unit=KB` 时用 1 Gbps 链路跑满，数字完整不裁切；字体换成宽体（如 Impact）后仍不裁切。

---

#### D6【P2】主题表色值互换 + 大量死代码

**位置**：`getTheme()` @207-216、`tbColor()` @260-297、`paintEvent` @587-595

**证据**：

```cpp
if (name == "暗黑")  return { QColor(0xff,0xff,0xff), QColor(0x00,0x00,0x00), ... };  // 背景白、文字黑
if (name == "简白")  return { QColor(0x00,0x00,0x00), QColor(0xff,0xff,0xff), ... };  // 背景黑、文字白
```

"暗黑"= 白底黑字，"简白"= 黑底白字 —— **两张表项写反了**。README §6.1 把这组反色原样抄进了文档，所以文档和代码是"一致地错"。

配套问题：
- `Theme::bg` / `Theme::accent` 定义了但**从未被读取**（悬浮窗只用 `.text`），主窗口用的是写死的浅色样式表 → 主题系统事实上只有"文字颜色"一个维度生效。
- `tbColor()`（@260-297，约 38 行，含 `GetPixel` 采样、注册表读取、深浅色回退）**没有任何调用点** —— 明显是"想做任务栏颜色自适应但没接上"的半成品。
- 后果：在 Win11 默认**浅色**任务栏上选"霓虹"（`#00ff88`）或"简白"（白字），对比度极低，悬浮窗几乎看不见。

**方案**：
1. 修正色值（或直接改名为"深底/浅底"两套语义，避免"暗黑/简白"字面歧义）。
2. 决定 `tbColor()` 的去向：要么接上（悬浮窗根据任务栏明暗自动选黑/白文字，并给"自定"颜色加亮度检查），要么删除。
3. 主题结构补上 `bg/text/accent` 的实际使用（主窗口也跟随主题，或明确文档写"仅悬浮窗文字色"）。

**验证**：六套主题逐一在浅色/深色任务栏下目测可读；`grep tbColor` 无残留（若删除）。

---

### E. 构建与工程

#### E1【P1】CMake 配置存在破坏性写法

**位置**：`CMakeLists.txt` 全文

**证据与问题**：

| 行 | 现状 | 问题 |
|---|---|---|
| 11 | `find_package(Qt6 ...)` 在 `add_executable` 之后 | 能用但反常规 |
| 20 | `set(CMAKE_EXE_LINKER_FLAGS "... -static-libgcc -static-libstdc++ -static")` | **三个参数同时存在冗余**；`-static` 在 **MSVC 下直接链接失败**（MSVC 不认识这个参数），所以这个项目实际上"只能在 MinGW 下构建"，但 CMakeLists 没有表达这个约束；`-static`（全静态）与 Qt 共享库组合属于半静态，意义有限 |
| 2 | `project(... LANGUAGES CXX)` | `.rc` 资源需要 **RC 语言**。MSVC 通常可隐式处理，MinGW 需要 windres；未 `enable_language(RC)` 时可能报"无法确定链接语言/未知源文件类型"（**置信度中，需构建机验证**） |
| 27-54 | `file(WRITE ...)` 生成 `.rc` | 每次 configure 都重写；版本号硬编码在字符串里，与 RC 里 `1,0,0,0` 和代码里的 `v4` 三处不同步 |
| — | 无 `install()`、无 `windeployqt`/`qt_generate_deploy_app_script` | README §11.3 让人手工 `cp build/*.dll` —— 这就是 `dist/` 里 82 MB 垃圾的成因；缺文件就白屏（缺 `platforms/qwindows.dll` 会直接报 "could not find or load the Qt platform plugin"） |
| — | 无 `-Wall -Wextra` / `/W4` | 这类大量 Win32 指针转换的代码，警告是免费的质量保障 |
| — | 无 `qt_standard_project_setup()` | AUTOMOC 手写还行，但缺 `qt_add_executable` 带来的默认设置 |

另：`#pragma comment(lib, "pdh.lib")` / `"iphlpapi.lib"`（@45-46）只对 MSVC 有效，与 `target_link_libraries`（@13-17）重复，属噪音。

**方案（草案见附录 A）**：`enable_language(RC)`、编译器条件化链接参数、`qt_generate_deploy_app_script` 生成部署脚本、`install()` 规则、警告开关、版本号单一来源（`project(... VERSION 1.0.0)` → `configure_file` 生成 `version.h` 与 `.rc`）。

**验证**：在 MinGW（MSYS2 UCRT64）与 MSVC 两套工具链下各自 `cmake -B build && cmake --build build && cmake --install build` → 得到可直接运行的目录，且 `build` 目录里没有手抄 DLL 的步骤。

---

#### E2【P2】中文窄字符串在 MSVC 下会乱码

**位置**：`main.cpp` 全文（大量 `"霓虹"`、`"自动"`、`"配置已保存并应用"` 这样的窄字符串字面量）

**证据**：`main.cpp` 首字节为 `2F 2F 20 4E`（`// N`），**无 UTF-8 BOM**，而文件内容是 UTF-8。MinGW/GCC 默认按 UTF-8 处理源文件，所以现在正常；MSVC 在**无 BOM**时按**当前用户代码页**（中文 Windows 是 GBK/936）解释源文件，`"霓虹"` 的字节序列会被当成 GBK 解读 → 运行时 `QString("...")` 得到乱码，界面全是"锟斤拷"。

**方案**：构建脚本加 `/utf-8`（等价于 `/source-charset:utf-8 /execution-charset:utf-8`，[MS 文档](https://learn.microsoft.com/en-us/cpp/build/reference/utf-8-set-source-and-executable-character-sets-to-utf-8)）；或者把字面量全部换成 `u8"..."` / `QStringLiteral(u"...")`。

**验证**：MSVC 下编译并在中文 Windows 上运行，界面无乱码；`--enable-warnings` 下无 C4819。

---

#### E3【P2】版本号四处不一致

| 位置 | 值 |
|---|---|
| `CMakeLists.txt:30-31,41,46` | `FILEVERSION 1,0,0,0` / `1.0.0.0` |
| `main.cpp:53` 窗口标题 | `Copyright © 2026.5 XAF` |
| `main.cpp:1155` 关于对话框 | `NetSpeedMon v4` |
| `main.cpp:1409` 启动日志 | `NetSpeedMon v4 启动` |
| `README.md:641` | `版本：v5 稳定版` |
| `dist` 里的 exe 资源 | `1.0.0.0`（已核对） |

**方案**：`project(NetSpeedMon VERSION 1.0.0)` → `configure_file` 出 `version.h`（`APP_VERSION "1.0.0"`）与 `.rc`，代码与资源都引用它；`QCoreApplication::setApplicationVersion()`；`--version` 参数打印。

---

#### E4【P2】单文件架构、无测试、无 CI

- 1431 行、6 个类、约 40 个自由函数挤在一个 `.cpp` 里；`NetMon`/`Ov`/`Tray`/`Win`/`App` 之间的耦合通过裸指针 + 回调信号完成，任何一个模块想单测都拉不起来。
- **零测试**。最容易测、也最容易出错的纯逻辑恰恰是现成的：`formatSpeedParts()`（@224-249，四种模式 × 边界值）、`Config` 的 load/save 往返与钳制、`SpeedParts` 的列宽计算。
- **零 CI**。Windows + Qt 的 CI 现在很便宜（`aqtinstall` / `jurplel/install-qt-action` + `windows-latest`），构建 + `ctest` 能在 5 分钟内跑完。
- 无 `.clang-format` / `.clang-tidy` / `.editorconfig`。

**方案（渐进，不强求一次到位）**：
1. 先加 `tests/`（QTest）+ CI，覆盖 `formatSpeedParts` 与 `Config`——这两块改动最频繁且零依赖。
2. 再按 `netmon/overlay/tray/settings/config/log` 拆文件；拆分时保持"行为不变 + 每步可编译"，不要和功能修复混在一个 PR 里。

---

#### E5【P1】`.gitignore` 是 Flash Builder 模板，82 MB 二进制入库

**位置**：`.gitignore`（18 行，内容是 `bin-debug/`、`*.swf`、`*.air`、`.actionScriptProperties`……）+ `dist/`

**证据**：
- `git ls-files` 显示 `dist/` 下**全部 60+ 个文件已入库**：`NetSpeedMon.exe`、`Qt6*.dll`、`libicudt78.dll`（33 MB）、`platforms/qwindows.dll`、`config/settings.json`、`logs/2026-05-27.log`、`logs/2026-07-10.log`……
- `.git` 33.4 MB，最大 blob 33.1 MB（ICU 数据）。
- 同时 `build/`、`config/`、`logs/`、`.vs/`、`CMakeFiles/`、`*.user` **都不在忽略列表里**，也就是说本地一旦构建，`git status` 会满屏噪音。

**影响**：clone 慢、仓库膨胀、DLL 冲突无法 merge、用户配置（含窗口坐标）与日志被当源码提交、二进制历史不可清理。

**方案**：
- 换掉 `.gitignore`（草案见附录 B）。
- `git rm -r --cached dist` 让它从索引移除（本地文件保留）。
- 发布物改用 **GitHub Releases / 附件**，或引入 Git LFS（LFS 只解决传输，不解决"二进制不该进源码树"的语义问题，**推荐 Release**）。
- 是否重写历史（`git filter-repo --path dist --invert-paths`）以把 33 MB 从历史里清掉 —— **需要你决定**（会改写所有 commit hash）。

---

#### E6【P3】重复/无用代码与小瑕疵

| 位置 | 问题 |
|---|---|
| `main.cpp:11` 与 `38` | `#include <QApplication>` 重复 |
| `main.cpp:20,26,41,42,43,39` | `QFormLayout`/`QGroupBox`/`QDebug`/`<cmath>`/`<cstdio>`/`QScreen` 未使用（`QScreen` 在 D1 修复后会用到） |
| `main.cpp:45-46` | `#pragma comment(lib, ...)` 与 CMake 重复，且 MSVC-only |
| `main.cpp:535,558` | `m_cfg->fb ? 75 : 50` —— `QFont::Bold/Normal` 的魔法数字 |
| `main.cpp:1048` | `QFontDatabase().families()`：Qt6 里 `QFontDatabase` 全是静态函数，构造临时对象是过时写法 |
| `main.cpp:838-857` 与 `1214-1230` | 两处**完全重复**的"绘制 N 图标兜底"代码（Tray 与 Win 各一份，约 18 行 ×2） |
| `main.cpp:1100` | `setProperty("colorCode", ...)` 设置了但从未读取 |
| `main.cpp:1188-1190` | `loadConfigToUI` 用一套样式表覆盖 `setupUI` 里的色块样式（26×22 变成 60px 宽），点一次颜色后又变回另一种尺寸 —— 视觉跳变 |
| `main.cpp:944` | `refreshTrayIcon()` 死代码（A1 修复时可一并删） |
| `main.cpp:465-466` | 溢出补偿算术错误（见 B1） |

**顺带**：`paintEvent` 每秒都 `QFont`/`QColor` 构造 + `formatSpeedParts` 两次，虽不致命，但既然 B1 要重写采样循环，顺手把 `QFont` 缓存到成员里。

---

### F. 文档与产品

#### F1【P2】README 与实现漂移

- §12 项目结构写的是 `DEVELOPMENT.md`、`bin/`、`release/` —— 三个都**不存在**（实际是 `docs`? 无、`dist/`）。
- §11.4 贴出的 CMakeLists 与真实文件不一致（少了 VERSIONINFO 块、少了 `-static` 的转义写法）。
- §4.2 "DWORD 溢出处理：差值减法不受 4GB 回绕影响" —— **结论错误**（见 B1）。
- §6.1 主题表把"暗黑=白底/简白=黑底"当规范写下来（见 D6）。
- 附录 A3 "高 DPI……应正常工作" —— 未验证，且按 D1 大概率不成立。
- §3.3 把"50ms 无条件 `SetWindowPos`"作为**正确做法**解释了一整节 —— 这是本报告 A3 建议推翻的设计，文档需要同步改写，否则会误导后续维护者。
- §13 说"即开即用无需安装"，但没提"目录必须可写"（C1）。

#### F2【P3】`README.en.md` 是模板占位

内容是 Gitee 的自动生成模板（`1. xxxx`、`Software architecture description`、`Readme\_XXX.md`），标题还是中文。英文用户看到等于没有文档。

#### F3【P3】完全没有 i18n

所有 UI 文案硬编码中文字面量，没有 `tr()`，`QTranslator` 无从下手。若要支持英文，现在改的成本最低（后面代码越拆越散，成本越高）。

#### F4【P3】交互细节

- **"单位"设置只影响悬浮窗**：`Win::setSpeedLabel()`（@935-936）和 `Tray::setToolTip()`（@862）都写死 `"auto"`，只有 `Ov` 用 `m_cfg->um`（@584-585）。用户选了 `KB`，主界面和托盘还是自动单位。
- **保存无反馈**：`onSave()`（@1193-1212）只写日志，界面没有任何"已保存"提示；`applyAutoStart()` 失败也无提示（C3）。
- 无"恢复默认"；无"立即应用 vs 保存"的区分（改完不点保存就白改，用户容易困惑）。
- 无窗口置中/吸附；`wx/wy` 不记忆最大化状态。

#### F5【P3】无签名、无异常处理

- README 附录 A1 自承"部分杀软可能报毒" —— 未签名的 MinGW Qt 单文件 + 大量 DLL + 写注册表 `Run` + 常驻置顶窗口，这五个特征叠加几乎必然触发启发式。可行方向：EV/OV 代码签名（有成本）、提交微软/杀软白名单、减少"可疑行为"（如 C3 只在需要时写注册表）。
- 无 `SetUnhandledExceptionFilter` / minidump：用户崩溃后我们什么也拿不到，只能靠日志（而日志可能写不出来，见 C1）。

---

## 4. 方案调研（外部依据汇总）

| 议题 | 结论 | 依据 |
|---|---|---|
| 网速采集 API | `GetIfTable2` 返回 `MIB_IF_TABLE2`，`MIB_IF_ROW2::InOctets/OutOctets` 是 **ULONG64**，无 32 位回绕；按 `InterfaceLuid` 区分接口 | [GetIfTable2](https://learn.microsoft.com/en-us/windows/win32/api/netioapi/nf-netioapi-getiftable2)、[MIB_IF_ROW2](https://learn.microsoft.com/en-us/windows/win32/api/netioapi/ns-netioapi-mib_if_row2) |
| PDH 通配符取值 | 应使用 `PdhGetFormattedCounterArrayW` 而非手工切实例名（可正确处理重名 `#n` 与特殊字符） | [PdhGetFormattedCounterArrayW](https://learn.microsoft.com/en-us/windows/win32/api/pdh/nf-pdh-pdhgetformattedcounterarrayw) |
| 全屏判定 | 用 `SHQueryUserNotificationState` 的 `QUNS_BUSY` / `QUNS_RUNNING_D3D_FULL_SCREEN` / `QUNS_PRESENTATION_MODE`，比矩形包含可靠 | [SHQueryUserNotificationState](https://learn.microsoft.com/en-us/windows/win32/api/shellapi/nf-shellapi-shqueryusernotificationstate) |
| 配置/数据目录 | 用 `QStandardPaths::AppConfigLocation`（`%LOCALAPPDATA%\<APP>`）与 `AppLocalDataLocation`，不要写 exe 目录 | [QStandardPaths](https://doc.qt.io/qt-6/qstandardpaths.html) |
| 中文源码在 MSVC 下 | 无 BOM 的源文件按 ANSI 代码页解释，必须加 `/utf-8` | [/utf-8](https://learn.microsoft.com/en-us/cpp/build/reference/utf-8-set-source-and-executable-character-sets-to-utf-8) |
| 任务栏嵌入（Win10/Win11） | 成熟方案：Win10 嵌入任务栏内部容器；Win11 任务栏是 XAML 重写、无可嵌入容器，改为**设为 `Shell_TrayWnd` 子窗口**，用 `TrayNotifyWnd`（通知区）与 `Start` 锚点定位，并缓存 DPI 只在变化时重算 | [TrafficMonitor 任务栏窗口实现机制](https://opendeep.wiki/zhongyang219/TrafficMonitor/3.3-taskbar-window-implementation) |
| 部署 | Qt6 官方推荐 `qt_generate_deploy_app_script` / `windeployqt` 生成部署脚本并 `install()`，而非手工拷贝 DLL | Qt6 文档 `qt_generate_deploy_app_script` |
| MinGW 下 `.rc` | 需要 RC 语言（windres）；`project(LANGUAGES CXX)` 未启用时需显式 `enable_language(RC)` | CMake `enable_language` 文档 / [SO: cmake + MinGW + .rc](https://stackoverflow.com/questions/3526794)（**置信度中，需构建机验证**） |

---

## 5. 实施计划（建议分四批，每批可独立验收）

### 批次 1 — "让用户不再认为程序坏了"（P0 功能，约 1–1.5 天）

| # | 任务 | 文件 | 风险 | 验收 |
|---|---|---|---|---|
| 1 | A1 托盘对象与图标内容解耦 | `main.cpp` Tray/App | 低 | 开关托盘 10 次，菜单/点击正常 |
| 2 | C1 配置与日志改标准目录 + 可写探测 + 失败提示 | `main.cpp` Config/Lg | 低 | `Program Files` 下可保存 |
| 3 | C3 自启命令行加引号、幂等写入、校验返回值 | `main.cpp` Config | 低 | 含空格路径可自启 |
| 4 | A2 统一退出入口 + `setQuitOnLastWindowClosed(false)` | `main.cpp` App/Ov | 低 | 三种退出方式均干净 |
| 5 | E2 加 `/utf-8`（若保留 MSVC 支持） | `CMakeLists.txt` | 低 | MSVC 构建无乱码 |

**批次 1 交付即可让产品"不丢配置、不假死、退出干净"。**

### 批次 2 — "让数字是真的"（P0/P1 数据，约 1.5–2 天）

| # | 任务 | 风险 | 验收 |
|---|---|---|---|
| 6 | B1 数据源统一为 `GetIfTable2` + 逐接口差分 + `QElapsedTimer` 归一化 | 中 | 下载完不跳变；睡眠唤醒不跳变；切网不跳变 |
| 7 | B2/B3 随 6 一并删除 PDH 相关代码（或保留但加重建） | 低 | 日志中不再有 PDH 分支 |
| 8 | B4 由 6 覆盖 | — | 断点阻塞 5s 后恢复无尖峰 |
| 9 | F4 单位设置贯通主界面与托盘 | 低 | 三处单位一致 |

> 需要你决策：**是否彻底删掉 PDH**（见 D1 决策项）。这是本批最大的取舍。

### 批次 3 — "让它在任何显示环境都正确"（P0/P1 布局，约 2–3 天，需真机）

| # | 任务 | 风险 | 验收 |
|---|---|---|---|
| 10 | D1 坐标系统一（逻辑 or 物理，二选一） | 中 | 100/125/150/200% 四档正确 |
| 11 | D2 全屏判定改 `SHQueryUserNotificationState` | 低 | 点桌面/最大化不误隐藏 |
| 12 | A3 看门狗降频 + 事件驱动（第一步） | 中 | 空闲 CPU <0.1%，无 z-order 冲突 |
| 13 | D3 屏幕/DPI/任务栏变更响应 + `TaskbarCreated` | 中 | 换边/改缩放/重启 explorer 后自恢复 |
| 14 | D4 最小尺寸 + 位置可见性校验 | 低 | 拖到最小仍可用 |

> A3 的第二步（任务栏子窗口化）建议**单独一批**并需要你安排真机验证窗口（Win10 + Win11 各一台）。

### 批次 4 — "让下一个人能维护"（工程债，约 2 天，可并行）

| # | 任务 | 风险 |
|---|---|---|
| 15 | E1 CMake 现代化 + 部署 + install（附录 A 草案） | 中（需构建机） |
| 16 | E5 `.gitignore` 重写 + `dist/` 出库（历史是否重写待定） | 低 |
| 17 | E3 版本号单一来源 | 低 |
| 18 | D5/D6 布局与主题修正确认 | 低 |
| 19 | E4 先加 `formatSpeedParts`/`Config` 的 QTest + CI | 低 |
| 20 | F1/F2 README 重写（含把本文档并入 `docs/`） | 低 |
| 21 | E6 清理重复/死代码、F3 i18n（可选） | 低 |

**总体估算**：批次 1+2 是"用户可感知的质量"，约 3 天；批次 3 约 2–3 天（含真机验证）；批次 4 约 2 天。合计 **7–8 个工作日**（不含 Win11 任务栏重构的验证往返）。

---

## 6. 需要你决策的事项

| ID | 决策点 | 我的建议 | 影响 |
|---|---|---|---|
| **D1** | 数据源：**保留 PDH+GetIfTable 双源**，还是**只用 GetIfTable2** | **只用 GetIfTable2**，删 PDH | 删掉约 100 行与 3 类 bug（B1/B2/B3）；代价是失去"PDH 口径"（但 PDH 的 Network Interface 计数器本身也来自同一份 NDIS 统计，无信息损失） |
| **D2** | 悬浮窗窗口模型：**保持独立置顶窗**（低成本） vs **改为任务栏子窗口**（TrafficMonitor 方案） | 先做"独立置顶窗 + 事件驱动 + 降频"（批次 3），把子窗口化作为 v2 独立评估 | 子窗口化能根治"消失/置顶打架"，但需要 Win10/Win11 双环境验证，工期不可控 |
| **D3** | 配置目录：**纯便携（exe 同级）** vs **标准用户目录** vs **混合（可写则便携，否则回退）** | 混合；安装版用 `%LOCALAPPDATA%` | 影响所有用户的配置迁移（需一次性迁移旧 `config/settings.json`） |
| **D4** | 是否允许**拆分为多文件** | 允许，但放在批次 4，且与功能修复分开提交 | 单文件 1431 行不利于后续维护；拆分本身零功能风险 |
| **D5** | Git 历史里的 82 MB 二进制：**只从今往后忽略** vs **重写历史清除** | 先 `git rm --cached` + 忽略（不动历史）；历史清理可选 `git filter-repo` | 重写历史会改所有 commit hash，若有其他协作者/克隆需协调 |
| **D6** | 是否要**中英双语** | 现在补 `tr()` 成本最低（批次 4 一起做），翻译可后置 | 影响后续所有 UI 代码写法 |
| **D7** | 版本号口径：统一到 **1.0.0** 还是 **v5** | 统一 `1.0.0`，Release 页面用标签 | 影响 About/标题/资源/README |
| **D8** | 是否接 **CI**（GitHub Actions + Qt） | 接，只做"构建 + ctest"，5 分钟级 | 需要仓库在 GitHub；私有仓库有额度限制 |
| **D9** | 代码风格：是否引入 `.clang-format` | 引入（基于 LLVM 风格，2 空格缩进保持现状） | 首次全量格式化会产生大 diff，建议单独一个 commit |

---

## 附录 A：建议的 CMakeLists 草案

```cmake
cmake_minimum_required(VERSION 3.21)
project(NetSpeedMon VERSION 1.0.0 LANGUAGES CXX)

enable_language(RC)                      # MinGW 需要 windres；MSVC 也可显式启用

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(Qt6 6.5 REQUIRED COMPONENTS Widgets)
qt_standard_project_setup()              # AUTOMOC/AUTOUIC/AUTORCC 与默认设置

qt_add_executable(NetSpeedMon WIN32 main.cpp)

target_link_libraries(NetSpeedMon PRIVATE Qt6::Widgets iphlpapi pdh)

# 编译器差异化：MSVC 不认 -static*
target_compile_options(NetSpeedMon PRIVATE
    $<$<CXX_COMPILER_ID:MSVC>:/utf-8>
    $<$<CXX_COMPILER_ID:MSVC>:/W4>
    $<$<NOT:$<CXX_COMPILER_ID:MSVC>>:-Wall -Wextra>)

target_link_options(NetSpeedMon PRIVATE
    $<$<CXX_COMPILER_ID:GNU>:-static-libgcc -static-libstdc++>)

# 版本号单一来源：生成 version.h 与 .rc
configure_file(cmake/version.h.in  ${CMAKE_BINARY_DIR}/generated/version.h  @ONLY)
configure_file(cmake/app.rc.in     ${CMAKE_BINARY_DIR}/generated/app.rc     @ONLY)
target_include_directories(NetSpeedMon PRIVATE ${CMAKE_BINARY_DIR}/generated)
target_sources(NetSpeedMon PRIVATE ${CMAKE_BINARY_DIR}/generated/app.rc)

# 部署：交给 Qt 官方脚本，不再手工 cp DLL
install(TARGETS NetSpeedMon RUNTIME DESTINATION .)
qt_generate_deploy_app_script(
    TARGET NetSpeedMon
    OUTPUT_SCRIPT deploy_script
    NO_UNSUPPORTED_PLATFORM_ERROR)
install(SCRIPT ${deploy_script})

include(CTest)
if(BUILD_TESTING)
    add_subdirectory(tests)
endif()
```

## 附录 B：建议的 `.gitignore`

```gitignore
# 构建产物
/build/
/build-*/
/out/
CMakeFiles/
CMakeCache.txt
cmake_install.cmake
*.ninja
.ninja_deps
.ninja_log
compile_commands.json

# IDE
.vs/
.vscode/
.idea/
*.user
*.sln
*.vcxproj*

# 运行期数据（不应入库）
/dist/
/release/
/config/
/logs/
*.log

# 本地缓存
*.obj
*.o
*.pdb
*.ilk
*.exp
```

## 附录 C：需要在构建机/真机上验证的清单

- [ ] MinGW（MSYS2 UCRT64）下 `cmake -B build -G Ninja && cmake --build build` 通过；`.rc` 图标与版本信息嵌入成功
- [ ] MSVC（VS 2022 + Qt MSVC kit）下同样通过（验证 `-static` 已条件化、`/utf-8` 生效）
- [ ] 100% / 125% / 150% / 200% 缩放下悬浮窗尺寸、位置、文字完整性
- [ ] 按 Win+D、点桌面、全屏视频、全屏游戏、演示模式，悬浮窗显隐是否符合预期
- [ ] 下载完成瞬间 / 睡眠 5 分钟唤醒 / 拔插网线 / 切 Wi-Fi / 连 VPN，数字是否连续
- [ ] 关闭再打开托盘图标，右键菜单与左键是否正常
- [ ] 装到 `C:\Program Files\` 下，保存设置与开机自启是否生效
- [ ] 任务栏换到顶部/左侧、改主副屏、重启 explorer.exe 后是否自动恢复
- [ ] 空闲 5 分钟后 `powercfg /energy` 是否报"定时器唤醒"问题

---

*本文档为审查结论与方案建议，尚未修改任何代码。审核通过后按 §5 的批次推进，每批次单独提交并附验收记录。*

---

## 7. 实施记录（2026-11-19）

审核结论：**按推荐方案全部实施**。原 `main.cpp`（1431 行单文件）已按 §5 批次 1–4 拆分重构为 `src/` 下 13 个实现文件 + 3 个测试文件。

### 7.1 逐条状态

| ID | 状态 | 落地位置 | 说明 |
|---|---|---|---|
| A1 托盘失效 | ✅ | `src/tray.cpp` | `QSystemTrayIcon` 只创建一次，菜单与 `activated` 连接在 `Tray` 内部建立；改图标只换 `QIcon` |
| A2 退出路径 | ✅ | `src/app.cpp`、`src/main.cpp`、`src/settings_window.cpp` | 单一 `App::quit()`；`setQuitOnLastWindowClosed(false)`；`closeEvent` 用 `singleShot` 延迟退出，避免销毁自身 |
| A3 看门狗对抗 | ✅ 第一步 | `src/overlay.cpp` | 删除 50ms 看门狗与 `WM_SHOWWINDOW`/`WM_WINDOWPOSCHANGING` 拦截；改为 1Hz 轻量检查 + `EVENT_SYSTEM_FOREGROUND` + `TaskbarCreated`；不再直接改 `WS_EX_*` |
| A3 第二步（任务栏子窗口化） | ⏸ 未实施 | — | 需 Win10 + Win11 真机验证，按 §6-D2 留作 v2，README 已注明当前实现为独立置顶窗 |
| A4 单实例 | ✅ | `src/single_instance.cpp` | 命名互斥 + `RegisterWindowMessageW` 广播（重试 3 次覆盖启动竞态）+ `AllowSetForegroundWindow` |
| B1 双源竞争/32 位/无归一化 | ✅ | `src/net_monitor.cpp` | `GetIfTable2` 单源、64 位计数、按 `InterfaceLuid` 逐接口差分、`QElapsedTimer` 真实耗时归一化、计数器回退按 0 处理 |
| B2 PDH 不重建 | ✅ | 同上 | PDH 代码整体移除，问题随数据源一起消失 |
| B3 实例名解析 | ✅ | 同上 | 同上（不再解析 counter path） |
| B4 无耗时归一化 | ✅ | 同上 | 同上 |
| C1 配置写 exe 目录 | ✅ | `src/app_paths.cpp`、`src/config.cpp` | 便携优先 + `QStandardPaths` 回退 + 可写探测 + 旧配置迁移 + 失败返回错误 |
| C2 配置健壮性 | ✅ | `src/config.cpp` | `QSaveFile` 原子写、字段白名单与钳制、损坏文件转 `.bak`、几何哨兵 `(-1,-1,0,0)` |
| C3 自启未加引号 | ✅ | `src/win_util.cpp` | `"路径" --min` 带引号、幂等写入、返回错误码并在设置界面提示；启动时不再重写注册表 |
| C4 日志无轮转 | ✅ | `src/logger.cpp` | 按天 + 保留 30 天 + 首次写入清理 + 失败降级到 stderr |
| D1 物理/逻辑坐标混用 | ✅ | `src/overlay.cpp` | 全部用 Qt 逻辑坐标；任务栏物理矩形按主屏 `devicePixelRatio` 换算；几何交给 `move()/setFixedSize()`，不再用 `SetWindowPos` 改几何 |
| D2 全屏误判 | ✅ | `src/win_util.cpp` | 改用 `SHQueryUserNotificationState`（`QUNS_BUSY` / `QUNS_RUNNING_D3D_FULL_SCREEN` / `QUNS_PRESENTATION_MODE`） |
| D3 兼容性 | ✅ 部分 | `src/overlay.cpp` | `TaskbarCreated`、DPI 变化重建布局、屏幕变化重定位、竖排任务栏尽力适配；**副屏任务栏不支持**（已写入 README 限制） |
| D4 无最小尺寸 | ✅ | `src/config.h`、`src/settings_window.cpp`、`src/app.cpp` | `kMinWindowWidth/Height` 约束、恢复时 `expandedTo`、屏幕可见性校验、越界自动居中 |
| D5 数值被裁切 | ✅ | `src/overlay.cpp`、`src/speed_format.cpp` | 列宽按实际文本 + 各模式宽度样本测量，数字变宽才重建布局 |
| D6 主题反色/死代码 | ✅ | `src/theme.cpp` | 暗黑=深底浅字、简白=浅底深字；`tbColor()` 接入为对比度自适应；`bg/accent` 未用字段删除 |
| E1 CMake | ✅ | `CMakeLists.txt` | `enable_language(RC)`、编译/链接参数按编译器条件化、`qt_add_resources` 嵌图标、`qt_generate_deploy_app_script` + `install`、`/W4`、`-Wall -Wextra` |
| E2 MSVC 中文乱码 | ✅ | `CMakeLists.txt` | MSVC 加 `/utf-8`，GCC 加 `-finput-charset/-fexec-charset` |
| E3 版本号不一致 | ✅ | `cmake/version.h.in`、`cmake/app.rc.in` | `project(VERSION 1.0.0)` 单一来源，代码/资源/关于/日志统一 |
| E4 单文件/无测试/无 CI | ✅ | `src/`、`tests/`、`.github/workflows/build.yml` | 13 个模块、3 组 QTest、MSVC + Qt 6.8 CI（构建 + ctest + 打包） |
| E5 仓库卫生 | ✅ | `.gitignore`、`git rm --cached dist` | 重写忽略规则；`dist/` 出库（本地文件保留，历史未重写） |
| E6 重复/死代码 | ✅ | 多处 | 图标绘制去重（`tray.cpp`）、删 `refreshTrayIcon`/`colorCode`/`tbColor` 死代码、`QFontDatabase` → `QFontComboBox`、魔法数字 75/50 消除 |
| F1/F2 文档漂移 | ✅ | `README.md`、`README.en.md` | 重写；删除"4GB 回绕不影响""高 DPI 应正常"等错误结论；目录结构与实际一致 |
| F3 i18n | ⏸ 未实施 | — | 界面仍为中文硬编码；代码已按模块拆分，后续加 `tr()` 成本可控 |
| F4 交互细节 | ✅ | `src/settings_window.cpp`、`src/tray.cpp` | 单位设置贯通悬浮窗/主窗口/托盘；保存反馈；恢复默认；打开配置目录；版本信息 |
| F5 签名/崩溃转储 | ⏸ 未实施 | — | 需要代码签名证书；已在 README「已知限制」说明 |

### 7.2 需要在真机确认的事项

2026-11-19 已在本机安装 Qt 6.8.3 (msvc2022_64) 并用 MSVC 2022 完成配置、编译、单元测试与启动验证（见 §7.5、§7.6）。以下为仍需**人工目视 / 真机**确认的项：

- [ ] MSYS2/MinGW 与 MSVC 两套工具链均能配置、编译、通过 `ctest`
- [ ] 100% / 125% / 150% / 200% 缩放下悬浮窗尺寸、位置、文字完整
- [ ] 点桌面、Win+D、最大化的自动隐藏任务栏、全屏视频/游戏、演示模式下的显隐
- [ ] 下载结束瞬间、睡眠唤醒、拔插网线、切 Wi-Fi、连 VPN 后数字连续
- [ ] 关闭再打开托盘图标，菜单与左键仍可用
- [ ] 装到 `C:\Program Files\` 下能保存设置、能开机自启
- [ ] 任务栏换边、改主副屏、重启 explorer.exe 后自动恢复

### 7.3 与旧行为的差异（用户可见）

1. 配置/日志在只读目录下改到 `%LOCALAPPDATA%\XAF\NetSpeedMon\`（首次启动自动迁移旧配置）。
2. 单位设置现在同时作用于主界面和托盘提示（以前只影响悬浮窗）。
3. 关闭主窗口时：有托盘图标则收进托盘；**没有托盘图标时直接退出**（以前会变成无入口的僵尸窗口）。
4. 主题色值修正（"暗黑"变回深色底），内置主题在对比度不足时会自动微调文字亮度。
5. "short" 单位模式下 ≥100 的数值不再保留 1 位小数（与其它模式统一）。
6. 悬浮窗不再强行对抗 Win+D/全屏：被系统隐藏后会在下一次前台切换或 1 秒内恢复。


### 7.4 实施中额外发现并修正的问题

以下问题不在最初 29 条之内，是在编码与交叉审查过程中发现的，已一并修复：

| 位置 | 问题 | 修正 |
|---|---|---|
| `CMakeLists.txt` | `$<$<NOT:$<CXX_COMPILER_ID:MSVC>>:-Wall -Wextra>`：生成表达式的结果按 `;` 拆分，带空格的字符串会作为**单个参数**传给 GCC，导致 `unrecognized command-line option` | 改为 `if(MSVC)/else()` + `add_compile_options`，逐个参数书写；`-static-libgcc/-static-libstdc++` 同理拆开 |
| `src/win_util.cpp` | `RegGetValueW` 用 `RRF_RT_REG_SZ \| RRF_RT_REG_EXPAND_SZ` 组合读取 Run 项，参数校验规则容易踩坑，失败会让自启检测/删除永久失效 | 改用 `RRF_RT_ANY`，读回后再校验 `type` 是否为 `REG_SZ`/`REG_EXPAND_SZ` |
| `src/win_util.cpp` | DWM `ColorizationColor` 是 `0xAARRGGBB`，却用 `GetRValue/GetBValue`（那是 `0x00BBGGRR`）解析 → 红蓝互换 | 改为位移取值 |
| `cmake/app.rc.in` | `IDI_ICON1` 未定义，图标被存成**字符串资源名**而非数字 ID，Explorer 取图标行为不确定 | 改为 `#define IDI_APPICON 101` + 数字 ID；注释改为纯 ASCII（rc.exe 与 windres 代码页不一致） |
| `CMakeLists.txt` | 未显式链接 `gdi32`（`GetPixel`） | 加入 `gdi32` |
| `tests/*` | `/utf-8` 只加在可执行目标上，测试目标编译中文字面量时仍按 ANSI 码页解析 | 改用目录级 `add_compile_options`，测试继承 |
| `src/tray.cpp` | `isSystemTrayAvailable()` 在构造时缓存，`setIconVisible` 直接被它挡死：资源管理器延迟启动/重启后图标永远不会出现 | 去掉缓存判断，直接交给 `QSystemTrayIcon`（Qt 自己处理 `TaskbarCreated`） |
| `src/app.cpp` | `--min` + 托盘不可用 + 悬浮窗关闭 = 完全无入口的进程 | 增加退化逻辑：两者都不可用时显示主窗口并记日志 |
| `src/settings_window.cpp` | 编辑直接写共享 `Config`：改颜色/恢复默认会**立即**影响悬浮窗（每秒读 `m_cfg`），但其它项要等保存；退出时又把未保存的值整体落盘 | 引入 `m_working` 编辑副本，只有点"保存设置"才一次性提交并 `emit configApplied()`；每次显示窗口从已应用配置重载 |
| `src/settings_window.cpp` | 字号标注 " px"，但 `QFont(family, size)` 是**磅值** | 标注改为 " pt"，文档同步 |
| `.github/workflows/build.yml` | 未指定 `-A x64`，Qt 版本用通配符 | 固定 `-A x64`、`6.8.3` + `win64_msvc2022_64` |
| `src/net_monitor.cpp` | 只包含 `iphlpapi.h` 时 `MIB_IF_ROW2` / `GetIfTable2` 未声明——`netioapi.h` 的用户态部分整段包在 `#ifdef _WS2IPDEF_` 里，必须先包含 `ws2ipdef.h` | include 顺序改为 `winsock2.h → ws2ipdef.h → windows.h → iphlpapi.h → netioapi.h`（真实编译时才暴露） |
| `CMakeLists.txt` | `qt_generate_deploy_app_script` 默认把运行库放进 `bin/`、插件放进 `plugins/`，而 exe 装在安装根目录 → 双击必缺 DLL | 安装后把 `bin/*.dll` 拉到 exe 同层并写 `qt.conf`（`Plugins = plugins`），得到可直接压缩分发的便携目录 |

### 7.5 验证记录

| 验证项 | 方式 | 结果 |
|---|---|---|
| CMake 脚本（configure + generate） | 用最小 Qt6 替身包（stub Qt6Config）在本机 CMake 3.31 + MSVC 2022 下配置 | ✅ 通过（生成构建文件成功） |
| `app.rc`（图标 + VERSIONINFO） | 用 Windows SDK 10.0.26100 的 `rc.exe` 实际编译 | ✅ 退出码 0，产出 209 KB `.res`，图标为数字 ID |
| 括号/成员/声明一致性 | 脚本静态扫描 13 个源文件 + 3 个测试 | ✅ 无未闭合括号、无未声明成员或方法 |
| 头文件包含 | 启发式扫描每个 `.cpp` 用到的 Qt 类型 | ✅ 无缺失（命中项均为宏/枚举） |
| 单元测试期望值 | 逐条对照实现推演 | ✅ 一致（含边界值与钳制后的几何） |
| 实际编译（MSVC 2022 + Qt 6.8.3） | `cmake --build --config Release` | ✅ 通过，`/W4 /permissive-` 下 0 警告，exe + 3 个测试全部链接 |
| 单元测试 | `ctest -C Release` | ✅ 3/3 通过（speed_format / config / theme） |
| 部署 | `cmake --install --prefix build/install` | ✅ exe + Qt 运行库同层 + `qt.conf` + `plugins/`，可直接运行 |
| 启动运行 | 实际启动 `--min`，枚举进程窗口 | ✅ 悬浮窗 (1475,1042) 218×28 贴合任务栏；日志/配置按便携模式落盘 |
| 主窗口与单实例 | 启动两次 | ✅ 主窗口居中 736×439；第二个实例 394ms 内退出，仅保留 1 个进程 |


### 7.6 启动时"先闪一个方框再变成悬浮窗"的定位与修复

**现象**：启动瞬间在任务栏位置闪一个矩形，随后才变成正常的悬浮窗文字。

**根因（旧实现，三处叠加）**：

1. `Ov` 构造函数里用 Win32 直接显示窗口：
   `SetWindowPos(hw, HWND_TOPMOST, tb.right - 280, tb.top + ..., 220, 30, SWP_NOACTIVATE | SWP_SHOWWINDOW)`
   —— 显示的是一个**固定 220×30、位置在"任务栏右端 -280"** 的窗口，且绕过了 Qt 的显示流程；
2. 真正的尺寸来自 `recalcLayout()`，真正的位置来自 200ms 的 `onRelocate()` 定时器 ⇒ 第一帧到第 200ms 之间，窗口**既不是最终尺寸也不是最终位置**，随后"跳"到托盘区左侧；
3. `show()` 发生在事件循环启动之前，第一帧还没来得及绘制，Windows 先按未初始化内容合成了一帧。

**修复**：

1. 构造函数只记录"要不要显示"，不再 `show()`；改由 `App::init()` 在配置应用完成后调用 `Overlay::start()`；
2. `start()` 先在**隐藏状态**下 `rebuildLayout()` 定尺寸、`reanchor()` 定位置，几何确定后才显示；
3. 显示走 `showPrimed()`：`DwmSetWindowAttribute(DWMWA_CLOAK)` 先遮蔽 → `show()` → `repaint()` 同步完成第一帧（含 `UpdateLayeredWindow`）→ 撤销 cloak。窗口第一次出现在屏幕上时已经是正确位置、正确尺寸、已绘制内容；
4. `rebuildLayout()` 只在尺寸真的变化时才 `setFixedSize()`，避免显示后再触发一次几何变化。

**实测证据**：

| 验证项 | 结果 |
|---|---|
| 首次上屏时的几何（应用日志） | `悬浮窗显示：1475,1042 218x28` —— 已经是任务栏上的最终位置与尺寸，不存在"先 220×30 再跳"的过程 |
| 启动后逐帧像素对比（80ms 采样，覆盖任务栏悬浮窗区域） | 悬浮窗出现后差异占比稳定在 **3.5%**（只有文字笔画），没有任何一帧接近实心方框（若为实心方框应 ≈60%） |
| 旧版 exe 同条件对照 | 旧版的几何跳变是确定存在的（构造时 220×30 @ `tb.right-280`，200ms 后移动），但单帧闪烁短于 80ms 采样间隔，像素法没抓到 |

**残留风险**：若某些驱动/DWM 组合在 `repaint()` 真正 flush 前就合成了窗口，最坏仍可能有一帧残影。真要彻底消除可把 cloak 的撤销延后到 `QTimer::singleShot(0, ...)`（代价是不可见窗口多存在一个事件循环周期）。当前实现已消除"位置/尺寸不对的方框"这个主因。

### 7.7 悬浮窗"消失不回来"的回归与修复

**症状**：用户反馈悬浮窗不显示了。

**排查过程**（本机实测）：

| 检查 | 结果 |
|---|---|
| 进程与窗口是否存在 | ✅ 进程存活，`Qt…QWindowTool…` 窗口存在，`IsWindowVisible=TRUE`，位置 1475,1042 218×28 |
| z-order 是否被任务栏压住 | ❌ 不是：`WS_EX_TOPMOST` 在位，`GW_HWNDPREV` 链上没有任务栏 |
| 启动时是否真的画出来了 | ✅ 该矩形内相对空白基准有 ~510 个像素变化（文字笔画） |
| 被系统隐藏后能否自愈 | ❌ **不能**：`ShowWindow(SW_HIDE)` 后等 2.6s，像素变化仍为 0 |

**根因（两个）**：

1. 自愈判断用的是 Qt 的 `isVisible()`。窗口被**系统**隐藏时（Win+D、显示桌面、显示器休眠、全屏切换），Qt 内部状态仍是"可见"，于是 `if (!isVisible())` 永远不成立 —— 而旧实现里是靠 50ms 看门狗里的 `IsWindowVisible(hwnd)` 兜住的，我在去对抗改造时把它一起删了，属于**回归**。
2. `IsWindowVisible()` 对**最小化**窗口返回 `TRUE`。而 Win+D / "显示桌面" 实际是把窗口最小化，不是隐藏 —— 所以只补 `IsWindowVisible` 仍然会漏。

**修复**：

```cpp
void Overlay::ensureShown() {
    // ...
    const bool iconic   = IsIconic(hwnd) != FALSE;          // 最小化（Win+D）
    const bool osHidden = IsWindowVisible(hwnd) == FALSE;   // 被系统隐藏
    if (iconic || osHidden) {
        ShowWindow(hwnd, SW_SHOWNOACTIVATE);                // 同时支持"恢复"与"显示"，且不抢焦点
        Log::info(iconic ? "悬浮窗被最小化，已恢复显示" : "悬浮窗被系统隐藏，已恢复显示");
        repaint();
    }
    ensureTopMost();  // 仅在丢失 topmost 或紧邻上方就是任务栏时才重设
}
```

`reevaluate()`（1Hz）与托盘激活、全屏状态切换都会走到 `ensureShown()`。

**验证**：

| 场景 | 结果 |
|---|---|
| 正常启动 | 514 px（可见） |
| `SW_MINIMIZE`（等价 Win+D）后 1.7s | ✅ 513 px，`iconic=False`，日志"悬浮窗被最小化，已恢复显示" |
| `SW_HIDE`（系统隐藏）后 1.7s | ✅ 513 px，日志"悬浮窗被系统隐藏，已恢复显示" |
| 再去掉 `WS_EX_TOPMOST` 后 1.7s | ✅ TOPMOST 自动恢复，像素不变 |
| 连续多次最小化 | ✅ 每次都能恢复 |
| 单元测试 | ✅ 3/3 通过 |

**为什么没有改成"任务栏子窗口"**：`SetParent(overlay, Shell_TrayWnd)` 确实能一劳永逸地解决遮挡问题（TrafficMonitor 的 Win11 方案），但代价是：① Qt 的顶层几何管理会与子窗口坐标打架，所有定位得改成 `SetWindowPos` 的父客户区坐标；② Explorer 重启后父窗口销毁，需要重建并重新挂载；③ 分层子窗口虽然 Win8+ 支持，但 Qt 的 backing store/flush 路径未在这种用法下验证过。当前"独立置顶窗 + 精确自愈"已覆盖同样的失效场景且保持逐像素透明，因此保留现有结构；若后续仍遇到无法自愈的遮挡，再切子窗口方案。

### 7.8 "悬浮窗不显示"的真正原因：自动对比度把文字调淡了

**症状**：用户反馈悬浮窗完全不显示。

**截图排查**（这次直接抓图看，不再靠推断）：

- 悬浮窗窗口存在、位置正确、`IsWindowVisible=TRUE`，文字也确实画出来了；
- 但笔画最深处的像素是 `RGB(255,209,194)` —— 这相当于主题色 `#FF7043` 以约 **25% 不透明度**叠在任务栏上；而主题色本应是 `(255,112,67)`。
- 结论：不是"没画"，是**颜色被改淡**，淡到肉眼以为不存在。

**根因**：

1. `ensureContrast()` 在判定"背景偏深"时会把前景色**提亮**。
2. 判定依据是采样任务栏颜色，而采样点取在任务栏**中线偏左 60%** —— 那里全是开始按钮/搜索/固定图标，亮度中位数被图标拉暗 → 判成"深色" → 橙色被提亮成淡橙、青色被提亮成淡青。
3. 更底层：`GetPixel` 用**窗口 DC** 取不到 DWM 合成的任务栏真实颜色（返回无效值），必须用**屏幕 DC**。

**修复**：

1. 采样改到任务栏**上下边缘的空白条带**（`y = top+3` / `bottom-4`，横向 10 个点取亮度中位数），数据来源改为屏幕 DC（`GetDC(nullptr)`）。
2. 判定阈值固定：中位亮度 > 0.45 记浅色，否则深色；采样失败才退回 `SystemUsesLightTheme` 注册表。
3. 修正触发阈值从 3.0 降到 **2.2**：主题原色在浅色任务栏上保持原貌，只有真的看不清时才压暗/提亮。
4. `自定` 主题完全不做修正（用户选什么就是什么）。
5. 判定结果写入日志：`任务栏亮度：浅色 / 深色 / 未知`，以后一眼能看出颜色策略。

**验证**（像素级）：

| 项 | 修复前 | 修复后 |
|---|---|---|
| 日志判定 | 深色（错） | `任务栏亮度：浅色` ✅ |
| 橙色笔画最深像素 | `(255,209,194)` 淡橙 | `(255,112,67)` = 主题原色 ✅ |
| 青色笔画最深像素 | 淡青 | `(63,156,198)`（压暗到对比度 2.2 以上）✅ |
| 肉眼 | 几乎看不见 | 清晰可读 ✅ |
| 单元测试 | — | 3/3 通过（新增"亮度未知时不改色"用例）✅ |

**教训**：自动"智能修正"必须可验证 —— 采样点选错比阈值选错更致命，而且会朝反方向把颜色改坏。这类逻辑要么有日志，要么不做。

### 7.9 点击托盘三角箭头就消失：z-order 被任务栏抢走

**症状**：点任务栏右侧的托盘溢出箭头（︿）后，悬浮窗消失。

**排查中先纠正了两个自己埋的坑**：

1. `FindWindowW("Shell_TrayWnd", $null)` 在 PowerShell 里 `$null` 没有被封送成 NULL，
   返回 0 —— 之前几轮 z-order 诊断因此全部失效（对比的是一个空句柄）。
   改用只收类名的 `FindWindowW(cls, IntPtr.Zero)` 后立刻复现。
2. 把运行中的悬浮窗 `SetParent` 到 `Shell_TrayWnd` 做 PoC：`SetParent` 成功、`GetParent` 等于任务栏、
   `IsWindowVisible=TRUE`，**但屏幕上什么都不画**（截图验证）。原因：Qt 的透明窗口走
   `UpdateLayeredWindow`，而该 API 对子窗口不生效。**结论："任务栏子窗口"与"内部透明"不可兼得，
   这条路走不通**（Win11 上若接受不透明背景才可考虑）。

**根因**（有正确句柄后复现）：

| 步骤 | 结果 |
|---|---|
| 启动 | 995 像素可见 |
| 把任务栏抬到 topmost 顶端（等价点托盘/箭头时 Explorer 的行为） | **0 像素 → 被完全盖住**，`任务栏在我上方=True` |
| 等 2.5s | 1023 像素 → 恢复 |

即：任务栏也是 topmost 窗口，被点击后 Explorer 会把自己抬到 topmost 带顶端，把我们的窗口压在下面。
原来的 `ensureTopMost()` 只在"紧邻上方窗口正好是任务栏"时才修复，托盘面板插在中间时就不触发 —— 于是"消失"。

**修复**：

1. 新增 `keepOnTop()`，由 **200ms** 定时器驱动，**无条件** `SetWindowPos(HWND_TOPMOST, SWP_NOMOVE|SWP_NOSIZE|SWP_NOACTIVATE|SWP_NOOWNERZORDER)`；
   不再依赖任何 z-order 猜测。
2. 把"被隐藏/最小化"的恢复也并入这个快定时器（`IsIconic` + `IsWindowVisible`），
   恢复延迟从 1s 降到 ≤200ms；恢复日志按"每次事件一条"限频，避免 5Hz 刷屏。
3. `ensureTopMost()` 退化为 `keepOnTop()` 的别名，避免两套逻辑。

**验证**（像素级，全部 ≤300ms 恢复）：

| 场景 | 结果 |
|---|---|
| 启动 | 1012 px ✅ |
| 任务栏抢置顶后 300ms | 1028 px ✅ |
| `SW_HIDE` 后 300ms | 1028 px，`visible=True` ✅ |
| `SW_MINIMIZE`（等价 Win+D）后 300ms | 1028 px，`iconic=False` ✅ |
| 点托盘箭头后 400ms | 972 px ✅（差值是箭头图标本身变化） |
| 托盘面板打开期间 1.5s | 923 px ✅ 一直可见 |

**代价**：可见状态下每 200ms 一次 `SetWindowPos`（5 次/秒）。实测 CPU 与电池影响可忽略，
比旧版 50ms（20 次/秒）低 4 倍，且比"1 秒后才回来"的观感好得多。

### 7.10 彻底修复：把任务栏设为悬浮窗的 owner

**背景**：7.9 的 200ms 保活只是"被抢走后抢回来"，被抢的瞬间仍有最长 200ms 空窗；用户反馈"经常会不显示"。

**方案调研**（对着 MS 文档逐条排除）：

| 方案 | 结论 |
|---|---|
| **任务栏子窗口**（`SetParent` 到 `Shell_TrayWnd`） | ❌ 不可行。Win8+ 虽然允许 `WS_EX_LAYERED` 用于子窗口，但 Qt 的透明窗口走 `UpdateLayeredWindow`，对子窗口不生效 —— PoC 实测：`SetParent` 成功、`GetParent` 正确、`IsWindowVisible=TRUE`，**屏幕上什么都不画**（截图）。与"内部透明"不可兼得 |
| **Deskband**（任务栏注册的 COM 带） | ❌ Win11 任务栏改为 XAML，已移除该接口 |
| **轮询抢 topmost** | ⚠️ 能恢复但有 200ms 空窗，属于兜底而非根治 |
| **Owned window（owner = 任务栏）** | ✅ MS 文档明确：*"An owned window is always above its owner in the z-order."* 窗口仍是顶层窗口（Qt 透明渲染不受影响），由系统保证永远压在任务栏之上 |

**实现**：

1. `Overlay::attachToTaskbarOwner()`：`SetWindowLongPtrW(hwnd, GWLP_HWNDPARENT, taskbar)`。
   首次显示（`showPrimed`）和 200ms 保活里都会检查并补挂，Qt 若重置了 owner 会自动补回。
2. `keepOnTop()` 不再无条件 `SetWindowPos`：只在 `WS_EX_TOPMOST` 位丢失时兜底一次；
   "被系统隐藏/最小化"的恢复仍在这个 200ms 定时器里。
3. **代价与对策**：owner 被销毁时（Explorer 重启）本窗口会被**连带销毁**。
   `App` 增加 2s 看门狗：`nativeHandle()` 指向的 HWND 若 `IsWindow` 为假 → `prepareDestroy()` + `deleteLater()` + 重建 `Overlay`。
   `nativeEvent` 里 `WM_NCCREATE` 会刷新缓存句柄，避免窗口重建后被误判为"已死"而重复创建。

**验证**（像素级）：

| 场景 | 7.9（轮询） | 7.10（owner） |
|---|---|---|
| 强行把任务栏抬到 topmost 顶端后 250ms | 0 像素（被盖住）→ 150~300ms 才回来 | **984 像素，全程可见，`任务栏在我上方=False`** ✅ |
| 点托盘溢出箭头 | 可见 | 可见 ✅ |
| 托盘面板打开期间 | 可见 | 可见 ✅ |
| `SW_MINIMIZE` / `SW_HIDE` | 200ms 恢复 | 200ms 恢复 ✅ |
| **真实重启 explorer.exe** | 窗口存活但可能不重挂 z-order | 窗口被连带销毁 → 2s 内自动重建并**重新定位到新任务栏**（日志：`悬浮窗显示：1531,1038`，x 从 1455 变为 1531）✅ |
| ctest | 3/3 | 3/3 ✅ |

**备注**：Explorer 重启过程中 `SHQueryUserNotificationState` 会短暂返回"全屏"状态，导致悬浮窗被隐藏一瞬（日志有记录 `检测到全屏应用`），shell 恢复后 1 秒内自动显示，属于可接受的瞬态。

### 7.11 v1.1.0：版本资源、默认字重与最终渲染参数

- **版本号**：`project(VERSION 1.0.0 → 1.1.0)`，由 `cmake/version.h.in` 与 `cmake/app.rc.in` 统一生成，
  exe 的 `FileVersion` / `ProductVersion` / 关于对话框 / 启动日志全部同步（实测 exe 资源为 `1.1.0`）。
- **exe 版本资源补全**（资源管理器 → 属性 → 详细信息可见）：
  `CompanyName=XAF`、`LegalCopyright=Copyright (C) 2026 XAF. All rights reserved.`、
  `LegalTrademarks=NetSpeedMon`、`Comments=Author: XAF | Source: https://gitee.com/user_gitee/NetSpeedMon`。
- **默认字重改为加粗**（`Config::defaults().bold = true`）：12pt 在透明分层窗口上偏软，加粗后更清晰。
- **恢复 `PreferFullHinting`**：与加粗搭配时锐度最好（不加粗时全提示会把字形挤窄，所以两者绑定为默认）。
- 已知取舍：加粗会牺牲一点秀气感；用户仍可在设置里取消勾选"加粗"，字号 8–48 可调。

验证：MSVC 零警告、ctest 3/3、exe 资源字段逐项核对、实机截图确认渲染。
