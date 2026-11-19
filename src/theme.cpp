#include "theme.h"

#include <QtMath>

namespace nsf {
namespace {

const QColor kUpColor(0xFF, 0x70, 0x43);    // #FF7043
const QColor kDownColor(0x4F, 0xC3, 0xF7);  // #4FC3F7

// 任务栏的典型底色，只用于判断对比度是否够用，不用于绘制。
const QColor kLightTaskbar(0xF3, 0xF3, 0xF3);
const QColor kDarkTaskbar(0x20, 0x20, 0x20);

// 只有低到这个程度才动手（正常主题色在浅色任务栏上仍应保留原貌）。
constexpr double kMinReadableRatio = 2.2;

const char* const kThemeNames[] = {"霓虹", "暗黑", "明亮", "赛博", "简白", "自定"};

double linearize(double channel) {
    return channel <= 0.03928 ? channel / 12.92 : qPow((channel + 0.055) / 1.055, 2.4);
}

QColor referenceBackground(TaskbarBrightness brightness) {
    switch (brightness) {
    case TaskbarBrightness::Light: return kLightTaskbar;
    case TaskbarBrightness::Dark: return kDarkTaskbar;
    case TaskbarBrightness::Unknown:
    default: return QColor();
    }
}

}  // namespace

QStringList themeNames() {
    QStringList names;
    for (const char* name : kThemeNames) names << QString::fromUtf8(name);
    return names;
}

bool isBuiltinTheme(const QString& name) {
    for (const char* candidate : kThemeNames) {
        if (name == QString::fromUtf8(candidate)) return true;
    }
    return false;
}

ThemeColors themeByName(const QString& name) {
    ThemeColors theme;
    theme.name = name;

    if (name == QStringLiteral("霓虹")) {
        theme = {name, QColor(0x1a, 0x1a, 0x2e), QColor(0x00, 0xff, 0x88), QColor(0x00, 0xff, 0x88), true};
    } else if (name == QStringLiteral("暗黑")) {
        theme = {name, QColor(0x1e, 0x1e, 0x1e), QColor(0xe6, 0xe6, 0xe6), QColor(0x4f, 0xc3, 0xf7), true};
    } else if (name == QStringLiteral("明亮")) {
        theme = {name, QColor(0xf5, 0xf5, 0xf5), QColor(0x33, 0x33, 0x33), QColor(0x21, 0x96, 0xf3), true};
    } else if (name == QStringLiteral("赛博")) {
        theme = {name, QColor(0x0a, 0x0a, 0x1a), QColor(0x00, 0xff, 0xff), QColor(0xff, 0x00, 0xff), true};
    } else if (name == QStringLiteral("简白")) {
        theme = {name, QColor(0xff, 0xff, 0xff), QColor(0x11, 0x11, 0x11), QColor(0x21, 0x96, 0xf3), true};
    }
    // "自定" 与未知主题：valid = false，由调用方决定用色。
    return theme;
}

double relativeLuminance(const QColor& color) {
    if (!color.isValid()) return 0.0;
    const QColor rgb = color.toRgb();
    const double r = linearize(rgb.redF());
    const double g = linearize(rgb.greenF());
    const double b = linearize(rgb.blueF());
    return 0.2126 * r + 0.7152 * g + 0.0722 * b;
}

double contrastRatio(const QColor& a, const QColor& b) {
    if (!a.isValid() || !b.isValid()) return 21.0;
    const double la = relativeLuminance(a);
    const double lb = relativeLuminance(b);
    const double lighter = qMax(la, lb);
    const double darker = qMin(la, lb);
    return (lighter + 0.05) / (darker + 0.05);
}

QColor ensureContrast(const QColor& foreground, const QColor& background, double minRatio) {
    if (!foreground.isValid() || !background.isValid()) return foreground;
    if (contrastRatio(foreground, background) >= minRatio) return foreground;

    const bool backgroundIsLight = relativeLuminance(background) > 0.35;
    for (int step = 1; step <= 16; ++step) {
        const int factor = 100 + step * 25;  // 125% .. 500%
        const QColor candidate =
            backgroundIsLight ? foreground.darker(factor) : foreground.lighter(factor);
        if (contrastRatio(candidate, background) >= minRatio) return candidate;
    }
    return backgroundIsLight ? QColor(Qt::black) : QColor(Qt::white);
}

OverlayColors overlayColors(const QString& themeName, const QString& customColor,
                            TaskbarBrightness brightness) {
    OverlayColors colors;

    // 用户自选颜色永远原样使用，不做"智能修正"。
    if (themeName == QStringLiteral("自定")) {
        QColor custom(customColor);
        if (!custom.isValid()) custom = QColor(0x00, 0xff, 0x88);
        colors.up = custom;
        colors.down = custom;
        return colors;
    }

    if (themeName == QStringLiteral("霓虹")) {
        colors.up = kUpColor;
        colors.down = kDownColor;
    } else {
        const ThemeColors theme = themeByName(themeName);
        const QColor text = theme.valid ? theme.text : QColor(0x00, 0xff, 0x88);
        colors.up = text;
        colors.down = text;
    }

    // 任务栏明暗未知时保持主题原色：宁可不动，也不要把颜色改错。
    const QColor reference = referenceBackground(brightness);
    if (!reference.isValid()) return colors;

    colors.up = ensureContrast(colors.up, reference, kMinReadableRatio);
    colors.down = ensureContrast(colors.down, reference, kMinReadableRatio);
    return colors;
}

QColor overlayBorderColor(const QString& themeName, const QString& customColor,
                          TaskbarBrightness brightness) {
    QColor base;
    if (themeName == QStringLiteral("自定")) {
        base = QColor(customColor);
        if (!base.isValid()) base = QColor(0x00, 0xff, 0x88);
    } else {
        const ThemeColors theme = themeByName(themeName);
        base = theme.valid ? theme.accent : QColor(0x00, 0xff, 0x88);
    }

    // 只保证"不消失在任务栏里"，不过度修正，否则会破坏主题观感。
    const QColor reference = referenceBackground(brightness);
    if (reference.isValid()) base = ensureContrast(base, reference, 1.35);

    // 直接拿主题强调色画边会像"荧光描边"，喧宾夺主。
    // 这里用纯 RGB 运算降饱和 + 收窄亮度（不用 HSL 接口：Qt 6.8 把它改成了 float，
    // 跨版本编译不安全）。
    int r = base.red();
    int g = base.green();
    int b = base.blue();

    const int luma = (r * 299 + g * 587 + b * 114) / 1000;
    constexpr int kMixToGray = 58;  // 只保留 42% 原色
    r = (r * (100 - kMixToGray) + luma * kMixToGray) / 100;
    g = (g * (100 - kMixToGray) + luma * kMixToGray) / 100;
    b = (b * (100 - kMixToGray) + luma * kMixToGray) / 100;

    // 亮度收到 [88, 178]，避免过亮刺眼或过暗看不清
    const int softened = (r * 299 + g * 587 + b * 114) / 1000;
    if (softened > 178) {
        const int delta = softened - 178;
        r -= delta; g -= delta; b -= delta;
    } else if (softened < 88) {
        const int delta = 88 - softened;
        r += delta; g += delta; b += delta;
    }

    return QColor(qBound(0, r, 255), qBound(0, g, 255), qBound(0, b, 255), 204);
}

}  // namespace nsf
