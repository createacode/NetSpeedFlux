// Win32 头最先包含
#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include "app.h"

#include <QCoreApplication>
#include <QFile>
#include <QGuiApplication>
#include <QMessageBox>
#include <QScreen>
#include <QString>
#include <QWidget>

#include "app_paths.h"
#include "config.h"
#include "logger.h"
#include "net_monitor.h"
#include "overlay.h"
#include "settings_window.h"
#include "tray.h"
#include "win_util.h"

namespace nsm {
namespace {

bool isGeometryOnSomeScreen(const QRect& rect) {
    // 只要求标题栏左上角可达：多屏拔插后窗口可能落在屏幕外，但位置仍在桌面范围内。
    for (QScreen* screen : QGuiApplication::screens()) {
        if (screen->availableGeometry().contains(rect.topLeft())) return true;
    }
    return false;
}

QRect centeredGeometry(const QWidget* widget) {
    QScreen* screen = QGuiApplication::primaryScreen();
    if (!screen) return QRect(0, 0, widget->width(), widget->height());
    const QRect available = screen->availableGeometry();
    const QSize size = widget->size();
    const QPoint topLeft(available.left() + (available.width() - size.width()) / 2,
                         available.top() + (available.height() - size.height()) / 2);
    return QRect(topLeft, size);
}

}  // namespace

App::App(QObject* parent) : QObject(parent) {}

App::~App() {
    if (m_overlay) {
        m_overlay->prepareDestroy();
        delete m_overlay;
        m_overlay = nullptr;
    }
    delete m_window;
    m_window = nullptr;
    // m_monitor / m_tray 是 this 的子对象，由 QObject 析构函数回收。
}

void App::init() {
    const AppPaths& paths = AppPaths::instance();
    m_store = std::make_unique<ConfigStore>(paths.configFile());

    const ConfigLoadResult result = m_store->load();
    if (result.parseError) {
        // 坏文件留档，避免每次启动都失败，也方便事后诊断。
        const QString backup = paths.configFile() + QStringLiteral(".bak");
        QFile::remove(backup);
        if (QFile::rename(paths.configFile(), backup)) {
            Log::warning(result.error + QStringLiteral("，已备份为 ") + backup);
        } else {
            Log::error(result.error);
        }
    } else if (!result.error.isEmpty()) {
        Log::warning(result.error);
    }
    m_cfg = result.config;

    if (!paths.warning().isEmpty()) Log::warning(paths.warning());
    Log::info(QStringLiteral("NetSpeedMon %1 启动（配置：%2，模式：%3）")
                  .arg(QCoreApplication::applicationVersion(), paths.configFile(),
                       paths.portable() ? QStringLiteral("便携") : QStringLiteral("用户目录")));

    m_monitor = new NetMonitor(this);
    m_window = new SettingsWindow(&m_cfg);
    m_tray = new Tray(this);
    m_overlay = new Overlay(m_cfg);

    restoreWindowGeometry();
    m_window->loadFromConfig();

    connect(m_monitor, &NetMonitor::updated, this, &App::onSpeeds);
    connect(m_window, &SettingsWindow::configApplied, this, &App::onConfigApplied);
    connect(m_window, &SettingsWindow::quitRequested, this, &App::quit);
    connect(m_tray, &Tray::openRequested, this, &App::activate);
    connect(m_tray, &Tray::quitRequested, this, &App::quit);

    if (result.fileMissing) {
        // 首次运行就把默认配置落盘，让用户能直接找到并编辑。
        QString error;
        if (!m_store->save(m_cfg, &error)) Log::warning(error);
    }

    // 配置里的自启状态与注册表不一致时只提示，不擅自修改系统设置。
    if (m_cfg.autoStart && !win::isAutoStartEnabled()) {
        Log::warning(QStringLiteral("配置要求开机自启，但注册表启动项缺失或指向了旧路径"));
    }

    applyConfig();
    // 悬浮窗在这里才真正显示：配置已应用、事件循环即将启动，第一帧不会闪。
    if (m_overlay) m_overlay->start();
    m_monitor->start();

    // 悬浮窗的 owner 是任务栏；Explorer 重启会连带销毁它，2 秒检查一次并重建。
    m_overlayWatchdog.setInterval(2000);
    m_overlayWatchdog.setTimerType(Qt::CoarseTimer);
    connect(&m_overlayWatchdog, &QTimer::timeout, this, &App::checkOverlayAlive);
    m_overlayWatchdog.start();
}

void App::checkOverlayAlive() {
    if (!m_overlay || m_quitting) return;

    const HWND hwnd = static_cast<HWND>(m_overlay->nativeHandle());
    if (!hwnd || IsWindow(hwnd)) return;  // 还没创建或仍然健在

    Log::warning(QStringLiteral("悬浮窗原生窗口已失效（资源管理器重启？），正在重建"));
    m_overlay->prepareDestroy();
    m_overlay->deleteLater();

    m_overlay = new Overlay(m_cfg);
    m_overlay->start();
}

void App::applyConfig() {
    if (m_tray) m_tray->setIconVisible(m_cfg.showTray);
    if (m_overlay) {
        m_overlay->applyConfig();
        m_overlay->setUserVisible(m_cfg.showOverlay);
    }
}

void App::restoreWindowGeometry() {
    if (!m_window) return;

    if (!m_cfg.hasWindowGeometry || m_cfg.windowGeometry.isEmpty()) {
        m_window->setGeometry(centeredGeometry(m_window));
        return;
    }

    QRect geometry = m_cfg.windowGeometry;
    geometry.setSize(geometry.size().expandedTo(QSize(kMinWindowWidth, kMinWindowHeight)));

    if (!isGeometryOnSomeScreen(geometry)) {
        Log::warning(QStringLiteral("保存的窗口位置不在任何屏幕上，改用居中显示"));
        geometry = centeredGeometry(m_window);
    }
    m_window->resize(geometry.size());
    m_window->move(geometry.topLeft());
}

void App::showMainWindow() {
    if (!m_window) return;

    if (!m_cfg.hasWindowGeometry) m_window->setGeometry(centeredGeometry(m_window));
    m_window->showNormal();
    m_window->raise();
    m_window->activateWindow();

    if (m_overlay && m_cfg.showOverlay) m_overlay->setUserVisible(true);
}

void App::showTrayOnly() {
    if (m_tray) m_tray->setIconVisible(m_cfg.showTray);
    if (m_overlay && m_cfg.showOverlay) m_overlay->setUserVisible(true);

    // --min + 托盘不可用 + 悬浮窗关闭 = 完全不可见的进程，退化成显示主窗口。
    const bool trayVisible = m_tray && m_tray->iconVisible();
    const bool overlayVisible = m_cfg.showOverlay && m_overlay && m_overlay->userVisible();
    if (!trayVisible && !overlayVisible) {
        Log::warning(QStringLiteral("托盘与悬浮窗都不可用，改为显示主窗口"));
        showMainWindow();
    }
}

void App::activate() {
    if (m_quitting) return;
    showMainWindow();
}

void App::onSpeeds(quint64 up, quint64 down) {
    if (m_window) m_window->setSpeeds(up, down);
    if (m_overlay) m_overlay->setSpeeds(up, down);
    if (m_tray) m_tray->setSpeeds(up, down, m_cfg.unit);
}

void App::onConfigApplied() {
    m_cfg.clamp();
    applyConfig();

    QString error;
    if (!m_store->save(m_cfg, &error)) {
        Log::error(error);
        QMessageBox::warning(m_window, QStringLiteral("保存配置失败"),
                             error + QStringLiteral("\n\n程序仍可继续使用，但设置不会被记住。"));
    }

    QString autoStartError;
    if (!win::setAutoStart(m_cfg.autoStart, &autoStartError)) {
        Log::warning(autoStartError);
        QMessageBox::warning(m_window, QStringLiteral("开机自启设置失败"), autoStartError);
    }

    Log::info(QStringLiteral("配置已应用（主题=%1，字体=%2 %3，单位=%4）")
                  .arg(m_cfg.theme, m_cfg.fontFamily, QString::number(m_cfg.fontSize),
                       unitModeToString(m_cfg.unit)));
}

void App::saveConfig() {
    if (!m_window || !m_store) return;

    m_cfg.hasWindowGeometry = true;
    m_cfg.windowGeometry = QRect(m_window->frameGeometry().topLeft(), m_window->size());

    QString error;
    if (!m_store->save(m_cfg, &error)) Log::error(error);
}

void App::quit() {
    if (m_quitting) return;
    m_quitting = true;

    Log::info(QStringLiteral("NetSpeedMon 退出"));
    saveConfig();

    // 先摘掉悬浮窗（含 WinEvent 钩子），再退出事件循环。
    if (m_overlay) {
        m_overlay->prepareDestroy();
        delete m_overlay;
        m_overlay = nullptr;
    }
    if (m_tray) m_tray->setIconVisible(false);
    if (m_monitor) m_monitor->stop();

    QCoreApplication::quit();
}

}  // namespace nsm
