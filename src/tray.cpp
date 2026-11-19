#include "tray.h"

#include <QAction>
#include <QColor>
#include <QFont>
#include <QIcon>
#include <QMenu>
#include <QPainter>
#include <QPixmap>
#include <QRect>
#include <QSystemTrayIcon>

#include "logger.h"

namespace nsm {
namespace {

QIcon iconFromResource() {
    QIcon icon(QStringLiteral(":/app.ico"));
    // 资源缺失时 QIcon 仍可能非空，用 pixmap 判断真正可用性。
    if (icon.isNull() || icon.pixmap(32, 32).isNull()) return QIcon();
    return icon;
}

QIcon fallbackIcon() {
    QPixmap pixmap(64, 64);
    pixmap.fill(Qt::transparent);

    QPainter painter(&pixmap);
    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.setPen(Qt::NoPen);
    painter.setBrush(QColor(0x4F, 0xC3, 0xF7));
    painter.drawRoundedRect(0, 0, 64, 64, 10, 10);
    painter.setPen(Qt::white);
    painter.setFont(QFont(QStringLiteral("Arial"), 36, QFont::Bold));
    painter.drawText(QRect(0, 0, 64, 64), Qt::AlignCenter, QStringLiteral("N"));
    painter.end();

    return QIcon(pixmap);
}

}  // namespace

Tray::Tray(QObject* parent) : QObject(parent) {
    m_available = QSystemTrayIcon::isSystemTrayAvailable();
    createIcon();

    if (!m_available) {
        Log::warning(QStringLiteral("系统托盘不可用，相关功能已禁用"));
    }
}

Tray::~Tray() {
    if (m_icon) {
        m_icon->setVisible(false);
        m_icon->setContextMenu(nullptr);
    }
    delete m_menu;
    m_menu = nullptr;
}

void Tray::createIcon() {
    // 菜单生命周期与 Tray 一致；图标对象也只创建一次（旧实现重建图标会丢掉菜单和信号）。
    if (!m_menu) {
        m_menu = new QMenu();
        m_actOpen = m_menu->addAction(QStringLiteral("打开主界面"));
        m_menu->addSeparator();
        m_actQuit = m_menu->addAction(QStringLiteral("退出"));

        connect(m_actOpen, &QAction::triggered, this, &Tray::openRequested);
        connect(m_actQuit, &QAction::triggered, this, &Tray::quitRequested);
    }

    if (!m_icon) {
        m_icon = new QSystemTrayIcon(this);
        m_icon->setContextMenu(m_menu);
        connect(m_icon, &QSystemTrayIcon::activated, this,
                [this](QSystemTrayIcon::ActivationReason reason) {
                    if (reason == QSystemTrayIcon::Trigger || reason == QSystemTrayIcon::DoubleClick) {
                        emit openRequested();
                    }
                });
    }

    QIcon icon = iconFromResource();
    if (icon.isNull()) icon = fallbackIcon();
    m_icon->setIcon(icon);
}

void Tray::setIconVisible(bool visible) {
    if (!m_icon) return;
    // 不要用构造时缓存的 isSystemTrayAvailable() 拦截：
    // 资源管理器重启/延迟启动后托盘会恢复，缓存的 false 会让图标永远不出现。
    m_icon->setVisible(visible);
}

bool Tray::iconVisible() const {
    return m_icon && m_icon->isVisible();
}

void Tray::setSpeeds(quint64 up, quint64 down, UnitMode unit) {
    if (!m_icon) return;
    m_icon->setToolTip(QStringLiteral("↑ %1   ↓ %2")
                           .arg(formatSpeed(static_cast<double>(up), unit),
                                formatSpeed(static_cast<double>(down), unit)));
}

}  // namespace nsm
