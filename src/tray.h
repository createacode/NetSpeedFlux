// NetSpeedFlux — 系统托盘（图标对象只创建一次，只替换图标内容）
#pragma once

#include <QObject>

#include "speed_format.h"

class QMenu;
class QSystemTrayIcon;
class QAction;

namespace nsf {

class Tray : public QObject {
    Q_OBJECT

public:
    explicit Tray(QObject* parent = nullptr);
    ~Tray() override;

    void setIconVisible(bool visible);
    bool iconVisible() const;

    void setSpeeds(quint64 up, quint64 down, UnitMode unit);

signals:
    void openRequested();
    void quitRequested();

private:
    void createIcon();

    QSystemTrayIcon* m_icon = nullptr;
    QMenu* m_menu = nullptr;
    QAction* m_actOpen = nullptr;
    QAction* m_actQuit = nullptr;
    bool m_available = false;
};

}  // namespace nsf
