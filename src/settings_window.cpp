#include "settings_window.h"

#include <QApplication>
#include <QCheckBox>
#include <QCloseEvent>
#include <QColor>
#include <QColorDialog>
#include <QComboBox>
#include <QCoreApplication>
#include <QDesktopServices>
#include <QDir>
#include <QFileInfo>
#include <QFontComboBox>
#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
#include <QMessageBox>
#include <QPushButton>
#include <QShowEvent>
#include <QSpinBox>
#include <QSystemTrayIcon>
#include <QTimer>
#include <QUrl>
#include <QVBoxLayout>

#include "app_paths.h"
#include "speed_format.h"
#include "theme.h"

namespace nsm {
namespace {

const char* const kStyleSheet = R"(
QWidget#settingsRoot { background:#f5f6fa; color:#2d3436; }
QFrame#speedBar { background:#eef1f6; border:none; border-radius:14px; }
QFrame#card { background:#ffffff; border:1px solid #e0e4e8; border-radius:8px; }
QLabel#cardTitle { color:#0984e3; font-weight:bold; font-size:12px; padding-bottom:2px; }
QLabel#rowLabel { color:#636e72; font-size:12px; }
QLabel#hintLabel { color:#9aa3ab; font-size:11px; }
QLabel#statusLabel { color:#00a86b; font-size:12px; }
QLabel#versionLabel { color:#9aa3ab; font-size:11px; }
QLabel#speedUp { color:#FF7043; font-size:17px; font-weight:bold; }
QLabel#speedDown { color:#4FC3F7; font-size:17px; font-weight:bold; }
QComboBox, QSpinBox, QFontComboBox {
    background:#ffffff; border:1px solid #dfe6e9; border-radius:4px;
    padding:3px 6px; min-height:20px;
}
QComboBox:hover, QSpinBox:hover, QFontComboBox:hover { border-color:#74b9ff; }
QComboBox::drop-down { width:18px; border:none; }
QPushButton {
    background:#0984e3; color:#ffffff; border:none; border-radius:6px;
    padding:6px 18px; min-height:24px; font-weight:bold;
}
QPushButton:hover { background:#0770c9; }
QPushButton:pressed { background:#065ea8; }
QPushButton#btnQuit { background:#d63031; }
QPushButton#btnQuit:hover { background:#b71c1c; }
QPushButton#btnAbout { background:#b2bec3; }
QPushButton#btnAbout:hover { background:#98a0a8; }
QPushButton#btnReset { background:#e0e4e8; color:#2d3436; }
QPushButton#btnReset:hover { background:#d0d6dc; }
QPushButton#btnColor { border:1px solid #b2bec3; border-radius:4px; padding:0; }
QCheckBox { spacing:6px; color:#555555; }
QCheckBox::indicator { width:14px; height:14px; border-radius:3px; border:2px solid #b2bec3; }
QCheckBox::indicator:checked { background:#0984e3; border-color:#0984e3; }
)";

void selectByData(QComboBox* combo, const QString& value) {
    const int index = combo->findData(value);
    combo->setCurrentIndex(index >= 0 ? index : 0);
}

}  // namespace

SettingsWindow::SettingsWindow(Config* config, QWidget* parent)
    : QWidget(parent), m_cfg(config), m_working(*config) {
    setObjectName(QStringLiteral("settingsRoot"));
    setWindowTitle(QStringLiteral("NetSpeedMon 网速监测"));
    setMinimumSize(kMinWindowWidth, kMinWindowHeight);
    resize(720, 400);
    setStyleSheet(QString::fromUtf8(kStyleSheet));

    buildUi();
    loadFromConfig();
}

QWidget* SettingsWindow::createCard(const QString& title) {
    auto* card = new QFrame(this);
    card->setObjectName(QStringLiteral("card"));
    auto* layout = new QVBoxLayout(card);
    layout->setContentsMargins(12, 10, 12, 10);
    layout->setSpacing(6);

    auto* titleLabel = new QLabel(title, card);
    titleLabel->setObjectName(QStringLiteral("cardTitle"));
    layout->addWidget(titleLabel);

    return card;
}

void SettingsWindow::addRow(QWidget* card, const QString& label, QWidget* field) {
    auto* layout = qobject_cast<QVBoxLayout*>(card->layout());
    if (!layout) return;

    auto* row = new QHBoxLayout;
    row->setSpacing(8);

    auto* name = new QLabel(label, card);
    name->setObjectName(QStringLiteral("rowLabel"));
    name->setFixedWidth(46);

    row->addWidget(name);
    row->addWidget(field, 1);
    layout->addLayout(row);
}

void SettingsWindow::buildUi() {
    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(14, 12, 14, 12);
    root->setSpacing(10);

    // ---- 速度横幅 ----
    auto* speedBar = new QFrame(this);
    speedBar->setObjectName(QStringLiteral("speedBar"));
    auto* speedRow = new QHBoxLayout(speedBar);
    speedRow->setContentsMargins(16, 6, 16, 6);
    speedRow->addStretch();

    m_speedUp = new QLabel(speedBar);
    m_speedUp->setObjectName(QStringLiteral("speedUp"));
    m_speedDown = new QLabel(speedBar);
    m_speedDown->setObjectName(QStringLiteral("speedDown"));

    speedRow->addWidget(m_speedUp);
    speedRow->addSpacing(24);
    speedRow->addWidget(m_speedDown);
    speedRow->addStretch();
    root->addWidget(speedBar);

    // ---- 三张设置卡片 ----
    auto* cardsRow = new QHBoxLayout;
    cardsRow->setSpacing(10);

    // 卡片 1：显示
    QWidget* cardDisplay = createCard(QStringLiteral("显示"));
    m_themeCombo = new QComboBox(cardDisplay);
    for (const QString& name : themeNames()) m_themeCombo->addItem(name, name);
    addRow(cardDisplay, QStringLiteral("主题"), m_themeCombo);

    m_unitCombo = new QComboBox(cardDisplay);
    m_unitCombo->addItem(unitModeDisplayName(UnitMode::Auto), unitModeToString(UnitMode::Auto));
    m_unitCombo->addItem(unitModeDisplayName(UnitMode::KB), unitModeToString(UnitMode::KB));
    m_unitCombo->addItem(unitModeDisplayName(UnitMode::MB), unitModeToString(UnitMode::MB));
    m_unitCombo->addItem(unitModeDisplayName(UnitMode::Short), unitModeToString(UnitMode::Short));
    addRow(cardDisplay, QStringLiteral("单位"), m_unitCombo);

    m_anchorCombo = new QComboBox(cardDisplay);
    m_anchorCombo->addItem(QStringLiteral("左侧"), anchorToString(OverlayAnchor::Left));
    m_anchorCombo->addItem(QStringLiteral("居中"), anchorToString(OverlayAnchor::Center));
    m_anchorCombo->addItem(QStringLiteral("右侧"), anchorToString(OverlayAnchor::Right));
    addRow(cardDisplay, QStringLiteral("位置"), m_anchorCombo);

    m_fullscreenCombo = new QComboBox(cardDisplay);
    m_fullscreenCombo->addItem(QStringLiteral("显示"), fullscreenToString(FullscreenAction::Show));
    m_fullscreenCombo->addItem(QStringLiteral("隐藏"), fullscreenToString(FullscreenAction::Hide));
    addRow(cardDisplay, QStringLiteral("全屏"), m_fullscreenCombo);
    qobject_cast<QVBoxLayout*>(cardDisplay->layout())->addStretch();

    // 卡片 2：字体
    QWidget* cardFont = createCard(QStringLiteral("字体"));
    m_fontCombo = new QFontComboBox(cardFont);
    m_fontCombo->setEditable(false);
    addRow(cardFont, QStringLiteral("字体"), m_fontCombo);

    m_fontSize = new QSpinBox(cardFont);
    m_fontSize->setRange(8, 48);
    m_fontSize->setSuffix(QStringLiteral(" pt"));
    addRow(cardFont, QStringLiteral("大小"), m_fontSize);

    auto* styleRow = new QHBoxLayout;
    styleRow->setSpacing(10);
    auto* styleLabel = new QLabel(QStringLiteral("样式"), cardFont);
    styleLabel->setObjectName(QStringLiteral("rowLabel"));
    styleLabel->setFixedWidth(46);
    m_bold = new QCheckBox(QStringLiteral("加粗"), cardFont);
    m_italic = new QCheckBox(QStringLiteral("斜体"), cardFont);
    styleRow->addWidget(styleLabel);
    styleRow->addWidget(m_bold);
    styleRow->addWidget(m_italic);
    styleRow->addStretch();
    qobject_cast<QVBoxLayout*>(cardFont->layout())->addLayout(styleRow);

    auto* colorRow = new QHBoxLayout;
    colorRow->setSpacing(8);
    auto* colorLabel = new QLabel(QStringLiteral("颜色"), cardFont);
    colorLabel->setObjectName(QStringLiteral("rowLabel"));
    colorLabel->setFixedWidth(46);
    m_colorButton = new QPushButton(cardFont);
    m_colorButton->setObjectName(QStringLiteral("btnColor"));
    m_colorButton->setFixedSize(28, 22);
    m_colorButton->setCursor(Qt::PointingHandCursor);
    m_colorButton->setToolTip(QStringLiteral("自定主题的文字颜色"));
    connect(m_colorButton, &QPushButton::clicked, this, &SettingsWindow::chooseColor);

    m_colorValue = new QLabel(cardFont);
    m_colorValue->setObjectName(QStringLiteral("hintLabel"));
    colorRow->addWidget(colorLabel);
    colorRow->addWidget(m_colorButton);
    colorRow->addWidget(m_colorValue);
    colorRow->addStretch();
    qobject_cast<QVBoxLayout*>(cardFont->layout())->addLayout(colorRow);

    auto* colorHint = new QLabel(QStringLiteral("仅“自定”主题生效；其他主题会自动保证对比度"), cardFont);
    colorHint->setObjectName(QStringLiteral("hintLabel"));
    colorHint->setWordWrap(true);
    qobject_cast<QVBoxLayout*>(cardFont->layout())->addWidget(colorHint);
    qobject_cast<QVBoxLayout*>(cardFont->layout())->addStretch();

    // 卡片 3：系统
    QWidget* cardSystem = createCard(QStringLiteral("系统"));
    m_showTray = new QCheckBox(QStringLiteral("显示托盘图标"), cardSystem);
    m_showOverlay = new QCheckBox(QStringLiteral("显示悬浮窗"), cardSystem);
    m_autoStart = new QCheckBox(QStringLiteral("开机自启（--min 静默启动）"), cardSystem);

    auto* configButton = new QPushButton(QStringLiteral("打开配置目录"), cardSystem);
    configButton->setObjectName(QStringLiteral("btnReset"));
    connect(configButton, &QPushButton::clicked, this, &SettingsWindow::openConfigFolder);

    auto* systemLayout = qobject_cast<QVBoxLayout*>(cardSystem->layout());
    systemLayout->addWidget(m_showTray);
    systemLayout->addWidget(m_showOverlay);
    systemLayout->addWidget(m_autoStart);
    systemLayout->addWidget(configButton);
    systemLayout->addStretch();

    auto* versionLabel = new QLabel(
        QStringLiteral("v%1 · Qt %2")
            .arg(QCoreApplication::applicationVersion(), QString::fromLatin1(qVersion())),
        cardSystem);
    versionLabel->setObjectName(QStringLiteral("versionLabel"));
    systemLayout->addWidget(versionLabel);

    cardsRow->addWidget(cardDisplay, 1);
    cardsRow->addWidget(cardFont, 1);
    cardsRow->addWidget(cardSystem, 1);
    root->addLayout(cardsRow, 1);

    // ---- 底部按钮 ----
    auto* footer = new QHBoxLayout;
    footer->setSpacing(8);

    m_status = new QLabel(this);
    m_status->setObjectName(QStringLiteral("statusLabel"));
    footer->addWidget(m_status);
    footer->addStretch();

    auto* btnReset = new QPushButton(QStringLiteral("恢复默认"), this);
    btnReset->setObjectName(QStringLiteral("btnReset"));
    connect(btnReset, &QPushButton::clicked, this, &SettingsWindow::restoreDefaults);

    auto* btnAbout = new QPushButton(QStringLiteral("关于"), this);
    btnAbout->setObjectName(QStringLiteral("btnAbout"));
    connect(btnAbout, &QPushButton::clicked, this, [this]() {
        QMessageBox::about(
            this, QStringLiteral("关于 NetSpeedMon"),
            QStringLiteral("NetSpeedMon %1\n\n"
                           "任务栏实时网速监测工具\n"
                           "作者：XAF\n"
                           "项目主页：https://gitee.com/user_gitee/NetSpeedMon\n\n"
                           "Qt %2\n"
                           "配置文件：%3")
                .arg(QCoreApplication::applicationVersion(), QString::fromLatin1(qVersion()),
                     AppPaths::instance().configFile()));
    });

    auto* btnQuit = new QPushButton(QStringLiteral("退出"), this);
    btnQuit->setObjectName(QStringLiteral("btnQuit"));
    connect(btnQuit, &QPushButton::clicked, this, &SettingsWindow::quitRequested);

    auto* btnSave = new QPushButton(QStringLiteral("保存设置"), this);
    connect(btnSave, &QPushButton::clicked, this, &SettingsWindow::saveSettings);

    footer->addWidget(btnReset);
    footer->addWidget(btnAbout);
    footer->addWidget(btnQuit);
    footer->addWidget(btnSave);
    root->addLayout(footer);
}

void SettingsWindow::loadFromConfig() {
    selectByData(m_themeCombo, m_working.theme);
    selectByData(m_unitCombo, unitModeToString(m_working.unit));
    selectByData(m_anchorCombo, anchorToString(m_working.anchor));
    selectByData(m_fullscreenCombo, fullscreenToString(m_working.fullscreen));

    m_fontCombo->setCurrentFont(QFont(m_working.fontFamily));
    m_fontSize->setValue(m_working.fontSize);
    m_bold->setChecked(m_working.bold);
    m_italic->setChecked(m_working.italic);
    m_showTray->setChecked(m_working.showTray);
    m_showOverlay->setChecked(m_working.showOverlay);
    m_autoStart->setChecked(m_working.autoStart);

    updateColorButton();
    updateSpeedLabels();
}

void SettingsWindow::commitToWorking() {
    m_working.theme = m_themeCombo->currentData().toString();
    m_working.unit = unitModeFromString(m_unitCombo->currentData().toString());
    m_working.anchor = anchorFromString(m_anchorCombo->currentData().toString());
    m_working.fullscreen = fullscreenFromString(m_fullscreenCombo->currentData().toString());
    m_working.fontFamily = m_fontCombo->currentText();
    m_working.fontSize = m_fontSize->value();
    m_working.bold = m_bold->isChecked();
    m_working.italic = m_italic->isChecked();
    m_working.showTray = m_showTray->isChecked();
    m_working.showOverlay = m_showOverlay->isChecked();
    m_working.autoStart = m_autoStart->isChecked();

    m_working.hasWindowGeometry = true;
    // 保存“外框左上角 + 客户区尺寸”，恢复时用 move()/resize() 还原，避免逐次缩小。
    m_working.windowGeometry = QRect(frameGeometry().topLeft(), size());
    m_working.clamp();

    // 控件上的值也要跟随钳制后的结果（例如字号越界）。
    loadFromConfig();
}

void SettingsWindow::saveSettings() {
    commitToWorking();

    // 一次性提交：悬浮窗/托盘只在 emit 之后才会看到新配置。
    *m_cfg = m_working;
    m_cfg->hasWindowGeometry = true;
    m_cfg->windowGeometry = QRect(frameGeometry().topLeft(), size());
    m_cfg->clamp();

    emit configApplied();
    setStatus(QStringLiteral("已保存并应用"));
}

void SettingsWindow::restoreDefaults() {
    m_working = Config::defaults();
    m_working.hasWindowGeometry = true;
    m_working.windowGeometry = QRect(frameGeometry().topLeft(), size());
    loadFromConfig();
    setStatus(QStringLiteral("已恢复默认值，点击“保存设置”生效"));
}

void SettingsWindow::chooseColor() {
    const QColor current(m_working.customColor);
    const QColor picked = QColorDialog::getColor(current.isValid() ? current : QColor(0x00, 0xff, 0x88),
                                                 this, QStringLiteral("字体颜色"));
    if (!picked.isValid()) return;
    m_working.customColor = picked.name();
    updateColorButton();
    setStatus(QStringLiteral("颜色已选好，点击“保存设置”生效"));
}

void SettingsWindow::updateColorButton() {
    const QColor color(m_working.customColor);
    m_colorButton->setStyleSheet(
        QStringLiteral("QPushButton#btnColor { background:%1; border:1px solid #b2bec3; "
                       "border-radius:4px; padding:0; }")
            .arg(color.isValid() ? color.name() : QStringLiteral("#00ff88")));
    m_colorValue->setText(color.isValid() ? color.name() : QStringLiteral("#00ff88"));
}

void SettingsWindow::updateSpeedLabels() {
    m_speedUp->setText(QStringLiteral("↑ ") + formatSpeed(static_cast<double>(m_up), m_cfg->unit));
    m_speedDown->setText(QStringLiteral("↓ ") + formatSpeed(static_cast<double>(m_down), m_cfg->unit));
}

void SettingsWindow::setSpeeds(quint64 up, quint64 down) {
    m_up = up;
    m_down = down;
    updateSpeedLabels();
}

void SettingsWindow::setStatus(const QString& text) {
    m_status->setText(text);
    QTimer::singleShot(3000, this, [this]() { m_status->clear(); });
}

void SettingsWindow::openConfigFolder() {
    const QFileInfo info(AppPaths::instance().configFile());
    QDesktopServices::openUrl(QUrl::fromLocalFile(info.absolutePath()));
}

void SettingsWindow::showEvent(QShowEvent* event) {
    QWidget::showEvent(event);
    // 每次显示都从“已应用”的配置重新载入，丢弃上次未保存的编辑。
    m_working = *m_cfg;
    loadFromConfig();
}

void SettingsWindow::closeEvent(QCloseEvent* event) {
    // 托盘不可用时（或用户关掉了托盘），关闭窗口就是退出，否则程序会变成没有入口的僵尸进程。
    const bool trayUsable = m_cfg->showTray && QSystemTrayIcon::isSystemTrayAvailable();
    if (trayUsable) {
        hide();
        event->ignore();
        return;
    }

    // 延迟到事件处理结束再退出，避免在 closeEvent 里删除自身。
    event->accept();
    QTimer::singleShot(0, this, &SettingsWindow::quitRequested);
}

}  // namespace nsm
