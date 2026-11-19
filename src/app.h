// NetSpeedFlux — 应用主控
#pragma once

#include <QObject>
#include <QTimer>

#include <memory>

#include "config.h"

namespace nsf {

class ConfigStore;
class NetMonitor;
class Overlay;
class SettingsWindow;
class Tray;

class App : public QObject {
    Q_OBJECT

public:
    explicit App(QObject* parent = nullptr);
    ~App() override;

    void init();

    void showMainWindow();
    void showTrayOnly();

public slots:
    // 第二个实例请求激活已有窗口。
    void activate();
    void quit();

private slots:
    void onSpeeds(quint64 up, quint64 down);
    void onConfigApplied();
    // 悬浮窗的 owner 是任务栏，Explorer 重启会把它连带销毁，这里负责重建。
    void checkOverlayAlive();

private:
    void applyConfig();
    void restoreWindowGeometry();
    void saveConfig();

    Config m_cfg;
    std::unique_ptr<ConfigStore> m_store;
    NetMonitor* m_monitor = nullptr;
    SettingsWindow* m_window = nullptr;
    Tray* m_tray = nullptr;
    Overlay* m_overlay = nullptr;
    QTimer m_overlayWatchdog;
    bool m_quitting = false;
};

}  // namespace nsf
