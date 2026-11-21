// NetSpeedFlux — 任务栏悬浮窗（透明、置顶、鼠标穿透、事件驱动）
#pragma once

#include <QColor>
#include <QFont>
#include <QMetaObject>
#include <QPair>
#include <QPainter>
#include <QPoint>
#include <QRect>
#include <QTimer>
#include <QVector>
#include <QWidget>

#include "config.h"
#include "speed_format.h"
#include "theme.h"

namespace nsf {

class Overlay : public QWidget {
    Q_OBJECT

public:
    explicit Overlay(const Config& config);
    ~Overlay() override;

    // 供 App 的看门狗检查：原生窗口是否还活着（owner 被销毁时会被连带销毁）。
    void* nativeHandle() const { return m_nativeHandle; }

    // 启动完成后再显示：把"第一帧"控制在我们手里，避免启动时闪一个方框。
    void start();

    // 配置变化后重新计算字体、尺寸和位置。
    void applyConfig();
    void setSpeeds(quint64 up, quint64 down);

    void setUserVisible(bool visible);
    bool userVisible() const { return m_userVisible; }

    // 托盘图标关闭后，悬浮窗是唯一入口：此时挂一个低级鼠标钩子，检测"落在悬浮窗上的双击"。
    // 注意：窗口本身始终点击穿透，钩子只听不拦 —— 单击完全不受影响（无感）。
    void setInteractive(bool interactive);
    bool isInteractive() const { return m_interactive; }

    // 由全局鼠标钩子回调（GUI 线程）
    void onGlobalLeftButtonDown(const QPoint& screenPos);

    // 关闭前调用：解除 WinEvent 钩子并隐藏，避免销毁过程中的窗口消息异常。
    void prepareDestroy();

signals:
    void activationRequested();

protected:
    void paintEvent(QPaintEvent* event) override;
    void showEvent(QShowEvent* event) override;
    bool nativeEvent(const QByteArray& eventType, void* message, qintptr* result) override;

private slots:
    // 1Hz 定时 + 前台窗口变化时触发（跨线程通过字符串调用，必须是 slot）。
    void reevaluate();

private:
    void installHooks();
    void removeHooks();
    void showPrimed();
    void ensureShown();
    void ensureTopMost();
    // 结构性保活：把任务栏设为本窗口的 owner。
    // MS 文档：owned window 永远在 owner 之上 —— 任务栏再怎么抬自己也不会盖住我们。
    void attachToTaskbarOwner();
    // 200ms 一次的保活：无条件把窗口压回 topmost 带顶端。
    // 不依赖任何 z-order 判断——点托盘箭头、Win+D、任务栏重排都会让 Explorer
    // 把自己抬到我们上面，靠"猜"谁在上面并不可靠。
    void keepOnTop();
    void rebuildLayout();
    void reanchor();
    // 任务栏上被组件占用的区间（逻辑坐标）。按像素扫描得到，并剔除自己当前占据的那段，
    // 这样自身移动不会改变这个集合（否则会把自己当障碍物来回漂移）。
    QVector<QPair<int, int>> taskbarContent() const;
    void applyVisibility();
    void refreshTaskbarBrightness();
    void drawColumn(QPainter& painter, int index, bool up, const SpeedParts& parts,
                    const QColor& color) const;
    // 矢量上传/下载图标（实心箭头，圆角收边），比字体里的 ↑ ↓ 更清晰：
    // 不依赖字体、任意 DPI 都锐利，尺寸也能和字号精确对齐。
    void drawArrowIcon(QPainter& painter, const QRectF& box, const QColor& color, bool up) const;
    // 圆角描边框 + 顶部高光 + 极淡外发光，内部保持透明。
    void paintFrame(QPainter& painter) const;

    const Config* m_cfg = nullptr;
    QTimer m_timer;
    QTimer m_topTimer;
    QFont m_textFont;
    TaskbarBrightness m_brightness = TaskbarBrightness::Unknown;
    quint64 m_up = 0;
    quint64 m_down = 0;
    int m_arrowW = 0;
    int m_arrowH = 0;
    int m_iconGap = 5;
    int m_numW = 0;
    int m_unitW = 0;
    int m_colW = 0;
    int m_gap = 10;
    int m_pad = 13;   // 窗口边缘到文字的水平距离 = 外发光余量 + 框内边距
    int m_vPad = 8;   // 垂直同理
    int m_glowMargin = 3;
    int m_boxPadX = 10;
    int m_boxPadY = 5;
    QPoint m_lastPos{-1, -1};
    QRect m_lastTaskbarRect;
    QRect m_lastNotifyRect;   // 托盘区矩形（图标增减/箭头状态变化时会变）
    QVector<QPair<int, int>> m_lastContentSignature;  // 上次定位时看到的组件布局
    qint64 m_lastNotReadyLogMs = 0;                   // "尚未就绪"日志节流
    bool m_settleScheduled = false;                    // 首次上屏后的"沉降"重算是否已安排
    qreal m_lastDpr = 0.0;
    bool m_userVisible = true;
    bool m_interactive = false;
    qint64 m_lastClickMs = 0;
    QPoint m_lastClickPos;
    bool m_fullscreenHidden = false;
    bool m_started = false;
    bool m_shownOnce = false;
    bool m_restoring = false;   // 避免"恢复中"重复写日志
    void* m_nativeHandle = nullptr;
    int m_tickCounter = 0;
    void* m_winEventHook = nullptr;
};

}  // namespace nsf
