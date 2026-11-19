# NetSpeedFlux

> Windows 任务栏实时网速悬浮窗 · Qt6 / C++17 / Win32

在任务栏上叠加一个小巧的透明悬浮窗，实时显示上传（↑ 橙）与下载（↓ 蓝）速度。
鼠标点击穿透到任务栏，不抢焦点、不进 Alt+Tab。

当前版本：**v1.3.0**

---

## 功能

- **实时网速**：每秒刷新，四种单位模式（自动 / KB / MB / 短）；上传/下载用矢量箭头图标（不依赖字体、任意 DPI 都锐利）
- **任务栏悬浮窗**：透明置顶、鼠标穿透、跟随任务栏位置与 DPI
- **自动对比度**：内置主题在浅色/深色任务栏上自动修正文字亮度，保证可读
- **全屏自动隐藏**：检测到全屏应用/演示模式时隐藏悬浮窗
- **系统托盘**：左键打开主界面，右键菜单可打开/退出，悬停显示实时速度
- **关闭行为**：关闭主窗口只是收进后台，程序继续跑；关掉托盘图标后仍如此（悬浮窗就是唯一入口）
- **托盘关闭后双击悬浮窗打开设置**：悬浮窗始终点击穿透（单击完全不影响任务栏）；
  没有托盘图标时用一个"只听不拦"的低级鼠标钩子识别双击，托盘可用时该钩子不安装
- **字体自定义**：字体、字号、加粗、斜体、自定颜色
- **配置持久化**：JSON 配置，窗口位置记忆（含多屏校验）
- **开机自启**：写 `HKCU\Run`，带 `--min` 静默启动
- **单实例**：重复启动会激活已有窗口
- **按天日志**：保留最近 30 天

---

## 架构

```
src/
├── main.cpp              程序入口：单实例、路径初始化、启动参数
├── app.{h,cpp}           应用主控：装配、信号连接、统一退出
├── net_monitor.{h,cpp}   网速采样（GetIfTable2）
├── overlay.{h,cpp}       任务栏悬浮窗（透明、置顶、穿透）
├── tray.{h,cpp}          系统托盘（图标对象只创建一次）
├── settings_window.*     设置主窗口
├── config.{h,cpp}        配置结构、校验、原子读写
├── app_paths.{h,cpp}     便携/用户目录选择与迁移
├── logger.{h,cpp}        按天日志 + 过期清理
├── speed_format.*        速度格式化（纯逻辑，可单测）
├── theme.{h,cpp}         主题色板与 WCAG 对比度
├── win_util.{h,cpp}      任务栏几何、全屏检测、注册表自启
└── single_instance.*     命名互斥 + 广播消息唤醒

tests/                    QtTest 单元测试（格式化 / 配置 / 主题）
cmake/                    version.h.in、app.rc.in
docs/                     代码审查与整改记录
```

### 数据采集

只使用一个数据源：**`GetIfTable2`**（`MIB_IF_ROW2`）。

- `InOctets` / `OutOctets` 是 **64 位**计数器，不存在 4 GiB 回绕
- 按 `InterfaceLuid` **逐接口**差分，接口增删/重连不会污染其他接口
- 速率 = 增量 ÷ **真实经过时间**（`QElapsedTimer`），定时器抖动和睡眠唤醒都不会产生假峰值
- 跳过环回与隧道接口（Teredo / ISATAP / VPN 隧道），避免重复计数

早期版本用了「PDH + GetIfTable」双数据源自动降级，两个源各自维护基线，切换瞬间会把几十秒的累计流量当成一秒的速度，且 32 位计数器求和后差分在千兆链路下必然出错。该方案已整体移除。

### 悬浮窗

- `Qt::FramelessWindowHint | Qt::Tool | Qt::WindowStaysOnTopHint | Qt::WindowDoesNotAcceptFocus`
- `WA_TranslucentBackground` + `WM_NCHITTEST → HTTRANSPARENT` 实现透明与点击穿透
- 不拦截 `WM_SHOWWINDOW` / `WM_WINDOWPOSCHANGING`，不与 Explorer 争抢窗口管理
- 每 **1 秒**做一次轻量状态检查（可见性、任务栏矩形、DPI），位置/尺寸变化时才移动窗口
- 监听 `EVENT_SYSTEM_FOREGROUND` 让全屏切换立即生效，监听 `TaskbarCreated` 处理资源管理器重启
- 所有几何计算都在 **Qt 逻辑坐标** 下完成（任务栏矩形按所在屏幕的 `devicePixelRatio` 换算）
- **边框质感**：圆角描边（1px 渐变：上亮下暗）+ 顶部内高光 + 底部内暗线 + 极淡外发光，
  内部保持全透明；边框色取主题强调色并降饱和，避免"荧光描边"抢走数字的注意力
- **首帧处理**：先在隐藏状态下定好尺寸与位置，再用 DWM cloak 遮蔽显示、同步画完第一帧后揭幕，
  因此启动时不会先闪一个方框再跳到位
- **钉在任务栏之上**：把任务栏设为窗口 owner，系统保证悬浮窗永远在任务栏之上（点托盘箭头、开始菜单都盖不住它）；被 Win+D / 显示桌面隐藏或最小化后 200ms 内恢复

---

## 构建

### 依赖

| 组件 | 版本 |
|------|------|
| Qt | ≥ 6.5（Widgets） |
| CMake | ≥ 3.21 |
| 编译器 | MSVC 2022 或 MinGW-w64 GCC ≥ 13 |

### MSYS2（MinGW）

```bash
pacman -S mingw-w64-ucrt-x86_64-qt6-base \
          mingw-w64-ucrt-x86_64-gcc \
          mingw-w64-ucrt-x86_64-cmake \
          mingw-w64-ucrt-x86_64-ninja

cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTING=ON
cmake --build build
ctest --test-dir build --output-on-failure
```

### MSVC

```powershell
cmake -S . -B build -DCMAKE_PREFIX_PATH="C:\Qt\6.8.0\msvc2022_64"
cmake --build build --config Release
ctest --test-dir build -C Release --output-on-failure
```

### 打包

```powershell
cmake --install build --prefix dist --config Release
```

`install` 会通过 `qt_generate_deploy_app_script` 自动部署 Qt 运行时与插件，不需要手工拷贝 DLL。

---

## 运行数据位置

程序**优先便携模式**：exe 所在目录可写时，配置与日志就在 exe 旁边。

| 模式 | 配置 | 日志 |
|------|------|------|
| 便携（exe 目录可写） | `<exe>/config/settings.json` | `<exe>/logs/YYYY-MM-DD.log` |
| 用户目录（Program Files 等只读位置） | `%LOCALAPPDATA%/XAF/NetSpeedFlux/settings.json` | `%LOCALAPPDATA%/XAF/NetSpeedFlux/logs/` |

从便携版换到安装版时，首次启动会把旧配置复制到用户目录（不覆盖已有文件）。
设置在「系统 → 打开配置目录」里可直接跳转。

### 配置文件

```json
{
  "cfgVersion": 2,
  "ff": "Microsoft YaHei UI",
  "fs": 12,
  "fb": false,
  "fi": false,
  "fc": "#00ff88",
  "theme": "霓虹",
  "sti": true,
  "sto": true,
  "tp": "right",
  "um": "auto",
  "fsc": "hide",
  "as": false,
  "wx": 490, "wy": 205, "ww": 720, "wh": 400
}
```

| 键 | 说明 |
|----|------|
| `ff` / `fs` / `fb` / `fi` | 字体名 / 字号 pt (8-48) / 加粗 / 斜体 |
| `fc` | 「自定」主题的文字颜色 |
| `theme` | 霓虹 / 暗黑 / 明亮 / 赛博 / 简白 / 自定 |
| `sti` / `sto` | 显示托盘图标 / 显示悬浮窗 |
| `tp` | 悬浮窗位置：left / center / right |
| `um` | 单位：auto / KB / MB / short |
| `fsc` | 全屏时：hide（隐藏）/ show（保持显示） |
| `as` | 开机自启 |
| `wx/wy/ww/wh` | 主窗口位置与尺寸（继续写这四个键，便于回退旧版本） |

配置写入使用 `QSaveFile`（原子替换）。文件损坏时会被重命名为 `settings.json.bak` 并使用默认配置继续启动。

---

## 命令行

| 参数 | 行为 |
|------|------|
| （无） | 显示主窗口 + 托盘 + 悬浮窗 |
| `--min` / `-m` | 只显示托盘与悬浮窗（开机自启用） |

---

## 测试

```bash
ctest --test-dir build --output-on-failure
```

覆盖：

- `speed_format`：四种单位模式的边界值、精度、宽度样本
- `config`：往返读写、越界钳制、损坏 JSON、旧键迁移、枚举转换
- `theme`：主题表正确性（暗黑/简白不再互换）、WCAG 对比度、内置主题可读性、自定主题不被改写

CI（GitHub Actions）在 Windows + MSVC + Qt 6.8 上执行配置、构建、测试和打包。

---

## 已知限制

1. **仅主屏任务栏**：只处理 `Shell_TrayWnd`，副屏的 `Shell_SecondaryTrayWnd` 暂不支持
2. **竖排任务栏**：按"通知区上方居中"做尽力适配，未在真机验证
3. **Windows 11**：任务栏是 XAML 重写，右侧定位依赖 `TrayNotifyWnd` 锚点；若系统改动内部结构会退化到固定偏移
4. **未签名**：部分杀毒软件可能对未签名的常驻程序报可疑，需要代码签名才能根治
5. 悬浮窗为点击穿透窗口，不提供鼠标交互，所有操作走托盘或主窗口

---

## 许可

见 [LICENSE](LICENSE)。
