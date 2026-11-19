// NetSpeedMon — 设置主窗口
#pragma once

#include <QWidget>

#include "config.h"

class QCheckBox;
class QCloseEvent;
class QComboBox;
class QFontComboBox;
class QLabel;
class QPushButton;
class QSpinBox;

namespace nsm {

class SettingsWindow : public QWidget {
    Q_OBJECT

public:
    explicit SettingsWindow(Config* config, QWidget* parent = nullptr);

    // 把配置刷到控件上（初始化与恢复默认时使用）。
    void loadFromConfig();
    void setSpeeds(quint64 up, quint64 down);

signals:
    void configApplied();
    void quitRequested();

protected:
    void closeEvent(QCloseEvent* event) override;
    void showEvent(QShowEvent* event) override;

private:
    void buildUi();
    QWidget* createCard(const QString& title);
    void addRow(QWidget* card, const QString& label, QWidget* field);
    void commitToWorking();
    void updateColorButton();
    void updateSpeedLabels();
    void setStatus(const QString& text);
    void chooseColor();
    void restoreDefaults();
    void saveSettings();
    void openConfigFolder();

    Config* m_cfg = nullptr;
    // 编辑中的副本：只有点“保存设置”才写回 m_cfg 并生效，
    // 避免未保存的改动被悬浮窗立刻读到、又被退出流程落盘。
    Config m_working;

    quint64 m_up = 0;
    quint64 m_down = 0;

    QLabel* m_speedUp = nullptr;
    QLabel* m_speedDown = nullptr;
    QComboBox* m_themeCombo = nullptr;
    QComboBox* m_unitCombo = nullptr;
    QComboBox* m_anchorCombo = nullptr;
    QComboBox* m_fullscreenCombo = nullptr;
    QFontComboBox* m_fontCombo = nullptr;
    QSpinBox* m_fontSize = nullptr;
    QCheckBox* m_bold = nullptr;
    QCheckBox* m_italic = nullptr;
    QPushButton* m_colorButton = nullptr;
    QLabel* m_colorValue = nullptr;
    QCheckBox* m_showTray = nullptr;
    QCheckBox* m_showOverlay = nullptr;
    QCheckBox* m_autoStart = nullptr;
    QLabel* m_status = nullptr;
};

}  // namespace nsm
