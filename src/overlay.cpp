// Win32 头必须最先包含。
#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <dwmapi.h>

#include "overlay.h"

#include <QDateTime>
#include <QEvent>
#include <QFontMetrics>
#include <QMouseEvent>
#include <QLinearGradient>
#include <QPainterPath>
#include <QGuiApplication>
#include <QScreen>
#include <QStringList>

#include "logger.h"
#include "theme.h"
#include "win_util.h"

#ifndef DWMWA_CLOAK
#define DWMWA_CLOAK 13
#endif

namespace nsf {
namespace {

Overlay* g_overlay = nullptr;
UINT g_taskbarCreatedMessage = 0;
HHOOK g_mouseHook = nullptr;

// 低级鼠标钩子：只听不拦，永远 CallNextHookEx，所以单击照旧穿透到任务栏。
LRESULT CALLBACK globalMouseHook(int code, WPARAM wParam, LPARAM lParam) {
    if (code == HC_ACTION && wParam == WM_LBUTTONDOWN && g_overlay != nullptr) {
        const auto* info = reinterpret_cast<const MSLLHOOKSTRUCT*>(lParam);
        if (info != nullptr) {
            g_overlay->onGlobalLeftButtonDown(QPoint(info->pt.x, info->pt.y));
        }
    }
    return CallNextHookEx(g_mouseHook, code, wParam, lParam);
}

void CALLBACK foregroundChanged(HWINEVENTHOOK, DWORD, HWND, LONG, LONG, DWORD, DWORD) {
    if (g_overlay) QMetaObject::invokeMethod(g_overlay, "reevaluate", Qt::QueuedConnection);
}

// 物理像素 → Qt 逻辑像素（基于任务栏所在屏幕的 DPR）。
QRect toLogical(const QRect& physical, qreal dpr) {
    if (physical.isEmpty() || dpr <= 0.0) return QRect();
    return QRect(QPoint(qRound(physical.x() / dpr), qRound(physical.y() / dpr)),
                 QSize(qRound(physical.width() / dpr), qRound(physical.height() / dpr)));
}

}  // namespace

Overlay::Overlay(const Config& config) : QWidget(nullptr), m_cfg(&config) {
    setWindowFlags(Qt::FramelessWindowHint | Qt::Tool | Qt::WindowStaysOnTopHint |
                   Qt::WindowDoesNotAcceptFocus | Qt::NoDropShadowWindowHint);
    setAttribute(Qt::WA_TranslucentBackground, true);
    setAttribute(Qt::WA_ShowWithoutActivating, true);
    setAttribute(Qt::WA_TransparentForMouseEvents, true);
    setFocusPolicy(Qt::NoFocus);
    setWindowTitle(QStringLiteral("NetSpeedFlux Overlay"));

    g_overlay = this;
    g_taskbarCreatedMessage = RegisterWindowMessageW(L"TaskbarCreated");

    m_brightness = win::taskbarBrightness();
    Log::info(QStringLiteral("任务栏亮度：%1")
                  .arg(m_brightness == TaskbarBrightness::Light ? QStringLiteral("浅色")
                       : m_brightness == TaskbarBrightness::Dark ? QStringLiteral("深色")
                                                                 : QStringLiteral("未知")));

    rebuildLayout();

    m_timer.setInterval(1000);
    m_timer.setTimerType(Qt::CoarseTimer);
    connect(&m_timer, &QTimer::timeout, this, &Overlay::reevaluate);

    // 200ms 保活：比 1Hz 的全量检查快得多，专门对付任务栏/托盘面板抢 z-order。
    m_topTimer.setInterval(200);
    m_topTimer.setTimerType(Qt::CoarseTimer);
    connect(&m_topTimer, &QTimer::timeout, this, &Overlay::keepOnTop);
    m_topTimer.start();

    installHooks();

    // 这里只记录意图，不 show()：等 App 把配置应用完、事件循环起来后再做第一帧。
    m_userVisible = m_cfg->showOverlay;
}

Overlay::~Overlay() {
    if (g_mouseHook != nullptr) {
        UnhookWindowsHookEx(g_mouseHook);
        g_mouseHook = nullptr;
    }
    removeHooks();
    m_timer.stop();
    if (g_overlay == this) g_overlay = nullptr;
}

void Overlay::installHooks() {
    // 前台窗口变化时立即重新评估全屏状态（比 1Hz 轮询更及时）。
    m_winEventHook = SetWinEventHook(EVENT_SYSTEM_FOREGROUND, EVENT_SYSTEM_FOREGROUND, nullptr,
                                     foregroundChanged, 0, 0,
                                     WINEVENT_OUTOFCONTEXT | WINEVENT_SKIPOWNPROCESS);
    if (!m_winEventHook) Log::warning(QStringLiteral("SetWinEventHook 安装失败，全屏检测将退化为 1Hz 轮询"));
}

void Overlay::removeHooks() {
    if (m_winEventHook) {
        UnhookWinEvent(static_cast<HWINEVENTHOOK>(m_winEventHook));
        m_winEventHook = nullptr;
    }
}

void Overlay::applyConfig() {
    m_brightness = win::taskbarBrightness();
    rebuildLayout();
    applyVisibility();
}

void Overlay::setSpeeds(quint64 up, quint64 down) {
    if (up == m_up && down == m_down) return;

    m_up = up;
    m_down = down;

    const QFontMetrics metrics(m_textFont);
    const int needed =
        qMax(metrics.horizontalAdvance(formatSpeedParts(static_cast<double>(up), m_cfg->unit).number),
             metrics.horizontalAdvance(formatSpeedParts(static_cast<double>(down), m_cfg->unit).number));
    if (needed > m_numW) {
        rebuildLayout();  // 数字变宽了，重新预留列宽，避免被裁切
    } else {
        update();
    }
}

void Overlay::rebuildLayout() {
    m_textFont = QFont(m_cfg->fontFamily, m_cfg->fontSize);
    m_textFont.setBold(m_cfg->bold);
    m_textFont.setItalic(m_cfg->italic);
    // 全提示：把笔画尽量吸附到像素网格，配合加粗使用效果最好（实测比默认 hinting 更锐）。
    // 注意：不加粗时全提示会让字形被挤窄、显得不顺眼，所以默认字重也是加粗。
    // 另外两种"锐化"手段已验证不可取：1px 偏移描边＝重影；关抗锯齿＝锯齿。
    m_textFont.setHintingPreference(QFont::PreferFullHinting);

    const QFontMetrics metrics(m_textFont);
    // 图标尺寸跟字号走：高度约为字高 0.72，宽高比 0.78
    m_arrowH = qMax(9, qRound(metrics.height() * 0.72));
    m_arrowW = qMax(7, qRound(m_arrowH * 0.78));

    int numberWidth = 0;
    const QStringList samples = numberWidthSamples(m_cfg->unit);
    for (const QString& sample : samples) {
        numberWidth = qMax(numberWidth, metrics.horizontalAdvance(sample));
    }
    numberWidth =
        qMax(numberWidth,
             metrics.horizontalAdvance(formatSpeedParts(static_cast<double>(m_up), m_cfg->unit).number));
    numberWidth =
        qMax(numberWidth,
             metrics.horizontalAdvance(formatSpeedParts(static_cast<double>(m_down), m_cfg->unit).number));
    m_numW = numberWidth;

    int unitWidth = 0;
    const QStringList units = allUnitLabels();
    for (const QString& unit : units) {
        unitWidth = qMax(unitWidth, metrics.horizontalAdvance(unit));
    }
    m_unitW = unitWidth;

    m_colW = m_arrowW + m_iconGap + m_numW + m_unitW;
    m_pad = m_glowMargin + m_boxPadX;
    m_vPad = m_glowMargin + m_boxPadY;
    const int width = m_pad * 2 + m_colW * 2 + m_gap;
    const int height = m_vPad * 2 + metrics.height();

    const QSize target(qMax(width, 160), qMax(height, 24));
    if (size() != target) setFixedSize(target);  // 显示之后不做无谓的 resize
    reanchor();
    update();
}

namespace {
// 布局签名用的量化器：把区间边界对齐到 8px，托盘 hover 高光这类几个像素的变化不算"布局变化"，
// 避免鼠标扫过托盘时悬浮窗来回挪。
QVector<QPair<int, int>> quantize(const QVector<QPair<int, int>>& ranges) {
    QVector<QPair<int, int>> result;
    result.reserve(ranges.size());
    for (const auto& range : ranges) {
        const int from = (range.first / 8) * 8;
        const int to = ((range.second + 7) / 8) * 8;
        if (!result.isEmpty() && from <= result.last().second) {
            result.last().second = qMax(result.last().second, to);
        } else {
            result.append({from, to});
        }
    }
    return result;
}

// 目标位置与当前位置差这么多以内就不动（防抖）
constexpr int kMoveThresholdPx = 8;
}  // namespace

QVector<QPair<int, int>> Overlay::taskbarContent() const {
    QVector<QPair<int, int>> content;
    const QRect taskbarPhysical = win::taskbarRectPhysical();
    if (taskbarPhysical.isEmpty()) return content;

    QScreen* screen = QGuiApplication::primaryScreen();
    const qreal dpr = screen ? screen->devicePixelRatio() : 1.0;

    const QRect self = geometry();
    const QRect selfPhysical(qRound(self.x() * dpr), qRound(self.y() * dpr),
                             qRound(self.width() * dpr), qRound(self.height() * dpr));
    // 只有自己确实落在任务栏里才剔除，否则启动时 pos() 还是 (0,0)，
    // 会把左上角的组件（如天气小组件）误当成"自己"而漏掉。
    const bool excludeSelf = selfPhysical.intersects(taskbarPhysical);

    for (const auto& raw : win::taskbarContentRanges(taskbarPhysical)) {
        if (excludeSelf) {
            // 只丢弃"基本就是自己"的那一段（自己画的边框/文字），不做切分，
            // 否则自己一移动，占用集合就被切开，居中锚点会来回跳。
            // 只要与自身相交就整段丢弃：自己画出来的边框/文字绝不能变成障碍物，
            // 否则每个 tick 都会把自己往旁边挤（这是之前左右乱挪的根源）。
            const int overlap = qMin(raw.second, selfPhysical.right()) -
                                qMax(raw.first, selfPhysical.left());
            if (overlap > 0) continue;
        }
        content.append({qRound(raw.first / dpr), qRound(raw.second / dpr)});
    }
    return content;
}

void Overlay::reanchor() {
    const QRect taskbarPhysical = win::taskbarRectPhysical();
    if (taskbarPhysical.isEmpty()) return;

    // 只处理主屏任务栏（Shell_TrayWnd），所以用主屏的 DPR 做物理→逻辑换算。
    // 注意不能拿物理像素点去调 screenAt()，那是逻辑坐标系。
    QScreen* screen = QGuiApplication::primaryScreen();
    const qreal dpr = screen ? screen->devicePixelRatio() : 1.0;
    m_lastDpr = dpr;

    const QRect taskbar = toLogical(taskbarPhysical, dpr);
    if (taskbar.isEmpty()) return;

    // 托盘区会随图标增减、箭头状态左右移动，这里记录下来供 1Hz 检查比对
    const QRect notifyPhysical = win::trayNotifyRectPhysical();
    m_lastNotifyRect = notifyPhysical;

    // 开机瞬间任务栏可能还没画完：扫描不到任何组件时先不定位置，交给 1Hz 重试，
    // 否则会按"空任务栏"把自己摆到一个莫名其妙的地方，直到用户点任务栏才恢复。
    // 托盘区还没建好时绝不定位：此时"右边全是空白"会把悬浮窗摆到时钟上，
    // 之后扫描又会把我们自己的像素当成"自己"剔掉，位置就锁死了再也回不来。
    if (notifyPhysical.isEmpty()) {
        m_lastContentSignature.clear();
        Log::info(QStringLiteral("托盘区尚未就绪，稍后重新定位"));
        return;
    }
    const QVector<QPair<int, int>> content = taskbarContent();
    if (content.isEmpty()) {
        m_lastContentSignature.clear();
        Log::info(QStringLiteral("任务栏尚未就绪，稍后重新定位"));
        return;
    }
    m_lastContentSignature = quantize(content);

    const QSize overlaySize = size();
    int x = taskbar.left();
    int y = taskbar.top();
    const bool vertical = taskbar.height() > taskbar.width();

    if (vertical) {
        x = taskbar.left() + (taskbar.width() - overlaySize.width()) / 2;
        const QRect notify = toLogical(notifyPhysical, dpr);
        y = notify.isEmpty() ? taskbar.top() + m_gap
                             : notify.top() - overlaySize.height() - m_gap;
    } else {
        // 放置规则（确定性优先，像素扫描只做"避让护栏"）：
        //   右侧 = 托盘区左边界 - 宽度 - 间距（永远贴着托盘，不依赖扫描结果）
        //   左侧 = 任务栏最左 + 间距，撞到组件就往右让
        //   居中 = 屏幕中心，撞到组件就向两边找最近的空位
        // 这样开机时序、鼠标 hover、图标增减都不会改变"默认位置"的判定依据。
        const QRect notify = toLogical(notifyPhysical, dpr);
        // 判定"撞到组件"时把组件区间左右各扩 m_gap，保证最终至少留出间距而不是只"不重叠"
        const int margin = m_gap;
        auto blocked = [&content, margin](int from, int to) -> const QPair<int, int>* {
            for (const auto& range : content) {
                if (from < range.second + margin && to > range.first - margin) return &range;
            }
            return nullptr;
        };

        switch (m_cfg->anchor) {
        case OverlayAnchor::Right: {
            // 右侧锚点不做任何像素级避让：托盘区左边界是系统给的确定值，
            // 用 notify.left - 宽度 - 间距 得到的落点永远紧贴托盘。
            // 之前在这里加"撞到组件就左移"，结果开机时任务栏还没画完、扫描报出的假组件
            // 会把悬浮窗推到 x≈1346（实测），而托盘本身没变，看起来就是"位置一直不对"。
            x = notify.isEmpty() ? taskbar.right() - overlaySize.width() * 3 / 2
                                 : notify.left() - overlaySize.width() - m_gap - 4;
            break;
        }
        case OverlayAnchor::Left: {
            int target = taskbar.left() + m_gap;
            for (int guard = 0; guard < 32; ++guard) {
                const QPair<int, int>* blocker = blocked(target, target + overlaySize.width());
                if (blocker == nullptr) break;
                target = blocker->second + m_gap;
                if (target + overlaySize.width() > taskbar.right()) break;
            }
            x = target;
            break;
        }
        case OverlayAnchor::Center:
        default: {
            const int center = taskbar.left() + (taskbar.width() - overlaySize.width()) / 2;
            int target = center;
            const int limit = qMax(taskbar.width(), overlaySize.width());
            for (int offset = 0; offset <= limit; offset += 4) {
                // 先试中心左侧偏移，再试右侧偏移，取先找到的
                const int candidates[2] = {center - offset, center + offset};
                bool found = false;
                for (const int candidate : candidates) {
                    if (candidate < taskbar.left() ||
                        candidate + overlaySize.width() > taskbar.right() + 1) {
                        continue;
                    }
                    if (blocked(candidate, candidate + overlaySize.width()) == nullptr) {
                        target = candidate;
                        found = true;
                        break;
                    }
                }
                if (found) break;
            }
            x = target;
            break;
        }
        }
        y = taskbar.top() + (taskbar.height() - overlaySize.height()) / 2;
    }
    int maxX = taskbar.left() + qMax(0, taskbar.width() - overlaySize.width());
    // 硬约束：右对齐时绝不越过托盘区左边界，托盘变窄时也不会压到"隐藏图标"箭头上
    if (m_cfg->anchor == OverlayAnchor::Right && !notifyPhysical.isEmpty()) {
        const QRect notify = toLogical(notifyPhysical, dpr);
        if (!notify.isEmpty()) {
            maxX = qMin(maxX, notify.left() - overlaySize.width() - 6);
        }
    }
    const int maxY = taskbar.top() + qMax(0, taskbar.height() - overlaySize.height());
    x = qBound(taskbar.left(), x, qMax(taskbar.left(), maxX));
    y = qBound(taskbar.top(), y, maxY);

    // 防抖：位置变化小于阈值就不动，鼠标扫过托盘、图标高光变化都不会让它抖
    if (m_lastPos.x() >= 0 && qAbs(x - m_lastPos.x()) <= kMoveThresholdPx &&
        qAbs(y - m_lastPos.y()) <= kMoveThresholdPx) {
        m_lastTaskbarRect = taskbarPhysical;
        return;
    }

    if (QPoint(x, y) != m_lastPos) {
        // 把判定依据写进日志：日后位置不对，一眼能看出是托盘区没就绪还是组件避让导致
        Log::info(QStringLiteral("定位依据：taskbar=%1,%2 %3x%4 notify=%5,%6 %7x%8 组件=%9段 -> x=%10")
                      .arg(taskbar.left())
                      .arg(taskbar.top())
                      .arg(taskbar.width())
                      .arg(taskbar.height())
                      .arg(notifyPhysical.left())
                      .arg(notifyPhysical.top())
                      .arg(notifyPhysical.width())
                      .arg(notifyPhysical.height())
                      .arg(content.size())
                      .arg(x));
        if (m_lastPos.x() >= 0) {  // 首次定位不打印 (-1,-1)
            Log::info(QStringLiteral("悬浮窗定位：%1,%2 -> %3,%4")
                          .arg(m_lastPos.x())
                          .arg(m_lastPos.y())
                          .arg(x)
                          .arg(y));
        }
        m_lastPos = QPoint(x, y);
        move(x, y);
    }
    m_lastTaskbarRect = taskbarPhysical;
}

void Overlay::start() {
    if (m_started) return;
    m_started = true;
    rebuildLayout();  // 隐藏状态下把尺寸和位置定下来
    applyVisibility();
}

void Overlay::showPrimed() {
    // 第一帧没画完就上屏，Windows 会把分层窗口当成实心方块闪一下。
    // 用 DWM cloak 让窗口在映射期间不参与合成，画完再撤销。
    HWND hwnd = reinterpret_cast<HWND>(winId());
    BOOL cloak = TRUE;
    const bool cloaked =
        hwnd != nullptr && SUCCEEDED(DwmSetWindowAttribute(hwnd, DWMWA_CLOAK, &cloak, sizeof(cloak)));

    // 记录首次上屏时的几何：只要这里就是最终位置/尺寸，就不会出现"先闪一个方框再跳到任务栏"。
    Log::info(QStringLiteral("悬浮窗显示：%1,%2 %3x%4")
                  .arg(pos().x())
                  .arg(pos().y())
                  .arg(width())
                  .arg(height()));

    show();
    repaint();  // 同步完成第一帧（含 UpdateLayeredWindow）

    m_nativeHandle = reinterpret_cast<void*>(winId());
    attachToTaskbarOwner();  // 结构性保活：owner = 任务栏

    // 开机时首次定位往往发生在 shell 还没稳定的时候（托盘区宽度、图标都还没定型）。
    // 3 秒后再强制重算一次，不受"布局签名未变"的限制；位置差 <阈值 时仍然不动。
    if (!m_settleScheduled) {
        m_settleScheduled = true;
        QTimer::singleShot(3000, this, [this]() {
            m_lastContentSignature.clear();
            reanchor();
        });
    }

    if (cloaked) {
        cloak = FALSE;
        DwmSetWindowAttribute(hwnd, DWMWA_CLOAK, &cloak, sizeof(cloak));
    }
}

void Overlay::applyVisibility() {
    if (!m_started) return;  // 启动前只记录状态，不碰窗口

    // 用户隐藏时才停止计时器；全屏隐藏期间仍需轮询以恢复显示。
    if (m_userVisible) {
        if (!m_timer.isActive()) m_timer.start();
    } else {
        m_timer.stop();
    }

    const bool wantVisible = m_userVisible && !m_fullscreenHidden;
    if (wantVisible) {
        ensureShown();
    } else if (isVisible()) {
        hide();
    }
}

void Overlay::ensureShown() {
    HWND hwnd = reinterpret_cast<HWND>(winId());
    if (!hwnd) return;

    // 开机自启时任务栏可能还没创建：这时候上屏会在左上角闪一下，宁可先不显示，
    // 等 1Hz 的 reevaluate 发现任务栏后再走正常显示流程。
    if (!m_shownOnce && win::taskbarRectPhysical().isEmpty()) {
        return;
    }

    if (!m_shownOnce) {
        m_shownOnce = true;
        reanchor();
        showPrimed();
        return;
    }

    // Qt 认为自己是隐藏的（我们自己 hide 过）：走正常显示流程。
    if (!isVisible()) {
        m_lastPos = QPoint(-1, -1);
        reanchor();
        showPrimed();
        return;
    }

    // Qt 以为可见、但窗口其实被系统藏了或最小化了。
    // 注意不能用 isVisible() 判断，Qt 不会感知这种外部隐藏；
    // 另外 IsWindowVisible() 对"最小化"窗口仍然返回 TRUE，必须单独查 IsIconic()。
    const bool iconic = IsIconic(hwnd) != FALSE;
    const bool osHidden = IsWindowVisible(hwnd) == FALSE;
    if (iconic || osHidden) {
        // SW_SHOWNOACTIVATE 能同时处理"最小化恢复"和"被隐藏显示"，且不抢焦点。
        ShowWindow(hwnd, SW_SHOWNOACTIVATE);
        Log::info(iconic ? QStringLiteral("悬浮窗被最小化，已恢复显示")
                         : QStringLiteral("悬浮窗被系统隐藏，已恢复显示"));
        repaint();
    }
    ensureTopMost();
}

void Overlay::attachToTaskbarOwner() {
    HWND hwnd = reinterpret_cast<HWND>(winId());
    HWND taskbar = static_cast<HWND>(win::taskbarWindow());
    if (!hwnd || !taskbar) return;
    if (reinterpret_cast<HWND>(GetWindowLongPtrW(hwnd, GWLP_HWNDPARENT)) == taskbar) return;
    SetWindowLongPtrW(hwnd, GWLP_HWNDPARENT, reinterpret_cast<LONG_PTR>(taskbar));
}

void Overlay::keepOnTop() {
    if (!m_userVisible || m_fullscreenHidden) return;

    HWND hwnd = reinterpret_cast<HWND>(winId());
    if (!hwnd || !IsWindow(hwnd)) return;   // 窗口已被连带销毁时交给 App 的看门狗重建

    // 被系统最小化/隐藏时顺手恢复，比 1Hz 的全量检查快得多。
    // IsWindowVisible 对"最小化"窗口返回 TRUE，所以必须同时查 IsIconic。
    if (IsIconic(hwnd) || !IsWindowVisible(hwnd)) {
        ShowWindow(hwnd, SW_SHOWNOACTIVATE);
        repaint();
        if (!m_restoring) {
            m_restoring = true;
            Log::info(QStringLiteral("悬浮窗被系统隐藏/最小化，已恢复显示"));
        }
    } else if (m_restoring) {
        m_restoring = false;
    }

    if (!IsWindowVisible(hwnd)) return;

    // 结构性保活：owner = 任务栏。这是"永远在任务栏之上"的唯一可靠机制，
    // 不依赖轮询抢 z-order（任务栏把自己抬到 topmost 顶端时我们依然在其上）。
    attachToTaskbarOwner();

    // 兜底：owner 机制失效时（例如任务栏被替换）仍然保证 topmost 位。
    if (!(GetWindowLongPtrW(hwnd, GWL_EXSTYLE) & WS_EX_TOPMOST)) {
        SetWindowPos(hwnd, HWND_TOPMOST, 0, 0, 0, 0,
                     SWP_NOACTIVATE | SWP_NOMOVE | SWP_NOSIZE | SWP_NOOWNERZORDER);
    }
}

void Overlay::ensureTopMost() {
    keepOnTop();
}

void Overlay::setInteractive(bool interactive) {
    if (m_interactive == interactive) return;
    m_interactive = interactive;
    m_lastClickMs = 0;

    // 窗口本身永远保持点击穿透（否则单击会被吃掉）。托盘关闭时额外挂一个全局鼠标钩子，
    // 只观察"落在悬浮窗矩形内的连击"，不拦截任何事件 —— 单击完全无感。
    if (interactive) {
        if (g_mouseHook == nullptr) {
            g_mouseHook = SetWindowsHookExW(WH_MOUSE_LL, globalMouseHook,
                                            GetModuleHandleW(nullptr), 0);
            if (g_mouseHook == nullptr) {
                Log::warning(QStringLiteral("无法安装鼠标钩子，双击悬浮窗打开设置将不可用"));
            }
        }
        Log::info(QStringLiteral("托盘已关闭：双击悬浮窗可打开设置（单击仍然穿透）"));
    } else {
        if (g_mouseHook != nullptr) {
            UnhookWindowsHookEx(g_mouseHook);
            g_mouseHook = nullptr;
        }
        Log::info(QStringLiteral("托盘可用：悬浮窗保持点击穿透，双击无动作"));
    }
}

void Overlay::onGlobalLeftButtonDown(const QPoint& screenPos) {
    if (!m_interactive || !m_userVisible || m_fullscreenHidden) return;

    HWND hwnd = reinterpret_cast<HWND>(winId());
    if (!hwnd || !IsWindow(hwnd) || !IsWindowVisible(hwnd)) return;

    RECT windowRect = {};
    if (!GetWindowRect(hwnd, &windowRect)) return;
    const QRect rect(windowRect.left, windowRect.top, windowRect.right - windowRect.left,
                     windowRect.bottom - windowRect.top);
    if (!rect.contains(screenPos)) {
        m_lastClickMs = 0;
        return;
    }

    // 用系统设置的双击时间/距离判断，和资源管理器行为一致
    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    const int tolerance = qMax(8, GetSystemMetrics(SM_CXDOUBLECLK));
    const bool isDoubleClick = m_lastClickMs != 0 && (now - m_lastClickMs) <= GetDoubleClickTime() &&
                               (screenPos - m_lastClickPos).manhattanLength() <= tolerance;

    if (isDoubleClick) {
        m_lastClickMs = 0;
        // 排队执行，避免在钩子回调里直接弹窗口
        QMetaObject::invokeMethod(this, [this]() { emit activationRequested(); },
                                  Qt::QueuedConnection);
        return;
    }
    m_lastClickMs = now;
    m_lastClickPos = screenPos;
}

void Overlay::setUserVisible(bool visible) {
    m_userVisible = visible;
    if (!visible) m_fullscreenHidden = false;
    applyVisibility();
}

void Overlay::prepareDestroy() {
    m_userVisible = false;
    m_timer.stop();
    removeHooks();
    hide();
}

void Overlay::refreshTaskbarBrightness() {
    const TaskbarBrightness brightness = win::taskbarBrightness();
    if (brightness == m_brightness) return;
    m_brightness = brightness;
    Log::info(QStringLiteral("任务栏亮度：%1")
                  .arg(brightness == TaskbarBrightness::Light ? QStringLiteral("浅色")
                       : brightness == TaskbarBrightness::Dark ? QStringLiteral("深色")
                                                               : QStringLiteral("未知")));
    update();
}

void Overlay::reevaluate() {
    if (!m_userVisible) return;

    const bool fullscreen =
        m_cfg->fullscreen == FullscreenAction::Hide && win::isFullscreenApplicationActive();
    if (fullscreen != m_fullscreenHidden) {
        m_fullscreenHidden = fullscreen;
        Log::info(fullscreen ? QStringLiteral("检测到全屏应用，悬浮窗已隐藏")
                             : QStringLiteral("退出全屏，悬浮窗已恢复"));
        applyVisibility();
    }

    if (m_fullscreenHidden) return;

    // 被系统隐藏（Win+D / 显示桌面 / 显示器休眠）或被任务栏压住时自愈。
    ensureShown();

    if (++m_tickCounter >= 10) {
        m_tickCounter = 0;
        refreshTaskbarBrightness();
    }

    const QRect taskbarPhysical = win::taskbarRectPhysical();
    if (taskbarPhysical.isEmpty()) return;

    QScreen* screen = QGuiApplication::primaryScreen();
    const qreal dpr = screen ? screen->devicePixelRatio() : 1.0;

    if (!qFuzzyCompare(dpr, m_lastDpr)) {
        m_lastDpr = dpr;
        rebuildLayout();  // DPI 变了要重算字体度量与窗口尺寸
        return;
    }
    // 任务栏或托盘区任一变化都重新定位：否则图标增减后会出现"有时贴着箭头、有时离很远"
    const QRect notifyPhysical = win::trayNotifyRectPhysical();
    if (notifyPhysical != m_lastNotifyRect) {
        Log::info(QStringLiteral("托盘区几何变化（%1x%2 @%3 -> %4x%5 @%6），重新定位悬浮窗")
                      .arg(m_lastNotifyRect.width())
                      .arg(m_lastNotifyRect.height())
                      .arg(m_lastNotifyRect.left())
                      .arg(notifyPhysical.width())
                      .arg(notifyPhysical.height())
                      .arg(notifyPhysical.left()));
        reanchor();
    } else if (taskbarPhysical != m_lastTaskbarRect) {
        reanchor();
    } else if (m_lastContentSignature.isEmpty()) {
        // 开机时任务栏还没画完（扫描不到组件）：每秒重试，直到能正常定位
        reanchor();
    } else {
        // 组件布局变了（图标增减、天气小组件加载完成、开机后任务栏才画完）也要重新定位，
        // 否则开机时按"半成品任务栏"算出的位置会一直保持，直到用户点一下任务栏才恢复。
        if (quantize(taskbarContent()) != m_lastContentSignature) reanchor();
    }
}

void Overlay::paintFrame(QPainter& painter) const {
    const qreal glow = m_glowMargin;
    const QRectF box = QRectF(rect()).adjusted(glow, glow, -glow, -glow);
    if (box.width() <= 2.0 || box.height() <= 2.0) return;

    const qreal radius = qMin<qreal>(8.0, qMin(box.width(), box.height()) / 2.0);
    QColor border = overlayBorderColor(m_cfg->theme, m_cfg->customColor, m_brightness);

    painter.save();

    painter.setBrush(Qt::NoBrush);

    // 极淡的外发光：两圈逐级变淡，让框"浮"起来一点
    for (int i = 2; i >= 1; --i) {
        QColor glowColor = border;
        glowColor.setAlpha(i == 2 ? 10 : 20);
        painter.setPen(QPen(glowColor, 1.0));
        painter.drawRoundedRect(box.adjusted(-i, -i, i, i), radius + i, radius + i);
    }

    // 主体边框：上亮下暗的竖向渐变，模拟顶部光源，避免"一根死线"
    QLinearGradient gradient(box.topLeft(), box.bottomLeft());
    QColor top = border.lighter(120);
    QColor bottom = border.darker(120);
    gradient.setColorAt(0.0, top);
    gradient.setColorAt(0.55, border);
    gradient.setColorAt(1.0, bottom);
    painter.setPen(QPen(QBrush(gradient), 1.1));
    painter.drawRoundedRect(box, radius, radius);

    // 顶部内侧高光 + 底部内侧暗线：一明一暗形成厚度，避免"一根平面线"
    const qreal straightWidth = box.width() - radius * 1.5;
    if (straightWidth > 2.0) {
        painter.setPen(QPen(QColor(255, 255, 255, 88), 1.0));
        painter.drawLine(QPointF(box.left() + radius * 0.75, box.top() + 1.0),
                         QPointF(box.right() - radius * 0.75, box.top() + 1.0));

        painter.setPen(QPen(QColor(0, 0, 0, 30), 1.0));
        painter.drawLine(QPointF(box.left() + radius * 0.75, box.bottom() - 1.0),
                         QPointF(box.right() - radius * 0.75, box.bottom() - 1.0));
    }

    painter.restore();
}

void Overlay::drawArrowIcon(QPainter& painter, const QRectF& box, const QColor& color, bool up) const {
    const qreal width = box.width();
    const qreal height = box.height();
    const qreal centerX = box.center().x();
    const qreal stem = qMax<qreal>(1.6, width * 0.22);   // 竖杆宽度（Material 风格约 22%）
    const qreal headHeight = height * 0.46;              // 箭头三角高度

    QPainterPath path;
    if (up) {
        path.moveTo(centerX - stem / 2.0, box.bottom());
        path.lineTo(centerX - stem / 2.0, box.top() + headHeight);
        path.lineTo(centerX - width / 2.0, box.top() + headHeight);
        path.lineTo(centerX, box.top());
        path.lineTo(centerX + width / 2.0, box.top() + headHeight);
        path.lineTo(centerX + stem / 2.0, box.top() + headHeight);
        path.lineTo(centerX + stem / 2.0, box.bottom());
    } else {
        path.moveTo(centerX - stem / 2.0, box.top());
        path.lineTo(centerX - stem / 2.0, box.bottom() - headHeight);
        path.lineTo(centerX - width / 2.0, box.bottom() - headHeight);
        path.lineTo(centerX, box.bottom());
        path.lineTo(centerX + width / 2.0, box.bottom() - headHeight);
        path.lineTo(centerX + stem / 2.0, box.bottom() - headHeight);
        path.lineTo(centerX + stem / 2.0, box.top());
    }
    path.closeSubpath();

    painter.save();
    // 描边 + 填充同色，用圆角连接把尖角磨圆一点，小尺寸下更耐看
    QPen pen(color, 0.9);
    pen.setJoinStyle(Qt::RoundJoin);
    pen.setCapStyle(Qt::RoundCap);
    painter.setPen(pen);
    painter.setBrush(color);
    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.drawPath(path);
    painter.restore();
}

void Overlay::drawColumn(QPainter& painter, int index, bool up, const SpeedParts& parts,
                         const QColor& color) const {
    const int left = m_pad + index * (m_colW + m_gap);
    const int iconTop = (height() - m_arrowH) / 2;

    drawArrowIcon(painter, QRectF(left, iconTop, m_arrowW, m_arrowH), color, up);

    painter.setFont(m_textFont);
    const QRect numberRect(left + m_arrowW + m_iconGap, 0, m_numW, height());
    const QRect unitRect(left + m_arrowW + m_iconGap + m_numW, 0, m_unitW, height());


    painter.setPen(color);
    painter.drawText(numberRect, Qt::AlignRight | Qt::AlignVCenter, parts.number);
    painter.drawText(unitRect, Qt::AlignRight | Qt::AlignVCenter, parts.unit);
}

void Overlay::paintEvent(QPaintEvent* event) {
    Q_UNUSED(event)

    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.setRenderHint(QPainter::TextAntialiasing, true);

    paintFrame(painter);

    const OverlayColors colors = overlayColors(m_cfg->theme, m_cfg->customColor, m_brightness);
    drawColumn(painter, 0, true, formatSpeedParts(static_cast<double>(m_up), m_cfg->unit), colors.up);
    drawColumn(painter, 1, false, formatSpeedParts(static_cast<double>(m_down), m_cfg->unit),
               colors.down);
}

void Overlay::showEvent(QShowEvent* event) {
    QWidget::showEvent(event);
    QTimer::singleShot(0, this, [this]() { reanchor(); });
}

bool Overlay::nativeEvent(const QByteArray& eventType, void* message, qintptr* result) {
    if (eventType != QByteArrayLiteral("windows_generic_MSG") &&
        eventType != QByteArrayLiteral("windows_dispatcher_MSG")) {
        return QWidget::nativeEvent(eventType, message, result);
    }

    MSG* msg = static_cast<MSG*>(message);
    if (!msg) return false;

    // 原生窗口可能被重建（owner 被销毁后 Qt 重新创建），保证缓存句柄始终是最新的，
    // 否则 App 的看门狗会误判"窗口已死"而重复创建悬浮窗。
    if (msg->message == WM_NCCREATE) {
        m_nativeHandle = reinterpret_cast<void*>(msg->hwnd);
    }

    // 鼠标穿透：让点击落到任务栏上。窗口始终穿透（单击无感），
    // 托盘关闭时的"双击打开设置"由低级鼠标钩子另行识别，不依赖窗口收事件。
    if (msg->message == WM_NCHITTEST) {
        if (result) *result = HTTRANSPARENT;
        return true;
    }

    // 资源管理器重启后重新定位。
    if (g_taskbarCreatedMessage != 0 && msg->message == g_taskbarCreatedMessage) {
        m_lastTaskbarRect = QRect();
        m_lastPos = QPoint(-1, -1);
        m_brightness = win::taskbarBrightness();
        reanchor();
        return false;
    }

    return QWidget::nativeEvent(eventType, message, result);
}

}  // namespace nsf
