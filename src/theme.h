// NetSpeedMon — 主题色板与对比度工具
#pragma once

#include <QColor>
#include <QString>
#include <QStringList>

namespace nsm {

// 任务栏明暗。未知时不做任何颜色修正，保持主题原色。
enum class TaskbarBrightness {
    Unknown,
    Light,
    Dark,
};

struct ThemeColors {
    QString name;
    QColor background;
    QColor text;
    QColor accent;
    bool valid = false;
};

QStringList themeNames();
bool isBuiltinTheme(const QString& name);
ThemeColors themeByName(const QString& name);

// WCAG 2.1 相对亮度与对比度，取值范围 0..1 / 1..21。
double relativeLuminance(const QColor& color);
double contrastRatio(const QColor& a, const QColor& b);

// 对比度不足时把前景色朝黑/白方向调整，尽量保留色相。
QColor ensureContrast(const QColor& foreground, const QColor& background, double minRatio = 3.0);

struct OverlayColors {
    QColor up;
    QColor down;
};

// 悬浮窗文字色：
// - "自定" 完全尊重用户选择的颜色；
// - 其它主题使用主题色，只有在任务栏明暗已知且对比度低到影响阅读时才微调。
OverlayColors overlayColors(const QString& themeName, const QString& customColor,
                            TaskbarBrightness brightness);

// 悬浮窗边框色：取主题强调色，保证在任务栏上不会"消失"，带一点透明以显得轻盈。
QColor overlayBorderColor(const QString& themeName, const QString& customColor,
                          TaskbarBrightness brightness);

}  // namespace nsm
