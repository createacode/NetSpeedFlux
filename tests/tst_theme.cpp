#include <QtTest>

#include <QColor>

#include "config.h"
#include "theme.h"

using namespace nsm;

class TstTheme : public QObject {
    Q_OBJECT

private slots:
    void exposesSixThemes();
    void darkAndLightThemesAreNotSwapped();
    void contrastRatioBasics();
    void ensureContrastFixesLowContrast();
    void builtinOverlayColorsStayReadable();
    void unknownBrightnessKeepsThemeColors();
    void customThemeKeepsUserColor();
    void borderColorIsUsableForEveryTheme();
};

void TstTheme::exposesSixThemes() {
    const QStringList names = themeNames();
    QCOMPARE(names.size(), 6);
    QVERIFY(names.contains(QStringLiteral("霓虹")));
    QVERIFY(names.contains(QStringLiteral("自定")));
    QVERIFY(isBuiltinTheme(QStringLiteral("暗黑")));
    QVERIFY(!isBuiltinTheme(QStringLiteral("不存在")));
}

void TstTheme::darkAndLightThemesAreNotSwapped() {
    const ThemeColors dark = themeByName(QStringLiteral("暗黑"));
    QVERIFY(dark.valid);
    QVERIFY2(relativeLuminance(dark.background) < 0.2, "暗黑主题必须是深色背景");
    QVERIFY2(relativeLuminance(dark.text) > 0.5, "暗黑主题必须是浅色文字");

    const ThemeColors light = themeByName(QStringLiteral("简白"));
    QVERIFY(light.valid);
    QVERIFY2(relativeLuminance(light.background) > 0.8, "简白主题必须是浅色背景");
    QVERIFY2(relativeLuminance(light.text) < 0.2, "简白主题必须是深色文字");
}

void TstTheme::contrastRatioBasics() {
    const double blackWhite = contrastRatio(Qt::black, Qt::white);
    QVERIFY(blackWhite > 20.0 && blackWhite < 22.0);
    QVERIFY(qAbs(contrastRatio(Qt::white, Qt::white) - 1.0) < 0.001);
}

void TstTheme::ensureContrastFixesLowContrast() {
    const QColor lowContrast(0x00, 0xff, 0x88);
    const QColor white(0xff, 0xff, 0xff);
    QVERIFY(contrastRatio(lowContrast, white) < 3.0);

    const QColor fixed = ensureContrast(lowContrast, white, 3.0);
    QVERIFY2(contrastRatio(fixed, white) >= 3.0, "修正后必须达到最低对比度");
}

void TstTheme::builtinOverlayColorsStayReadable() {
    // 与实现中的参考底色保持一致
    const QColor lightTaskbar(0xf3, 0xf3, 0xf3);
    const QColor darkTaskbar(0x20, 0x20, 0x20);
    constexpr double kMinReadable = 2.2;

    for (const QString& name : themeNames()) {
        if (name == QStringLiteral("自定")) continue;

        const OverlayColors onLight =
            overlayColors(name, QStringLiteral("#00ff88"), TaskbarBrightness::Light);
        QVERIFY2(contrastRatio(onLight.up, lightTaskbar) >= kMinReadable, qPrintable(name));
        QVERIFY2(contrastRatio(onLight.down, lightTaskbar) >= kMinReadable, qPrintable(name));

        const OverlayColors onDark =
            overlayColors(name, QStringLiteral("#00ff88"), TaskbarBrightness::Dark);
        QVERIFY2(contrastRatio(onDark.up, darkTaskbar) >= kMinReadable, qPrintable(name));
        QVERIFY2(contrastRatio(onDark.down, darkTaskbar) >= kMinReadable, qPrintable(name));
    }
}

void TstTheme::unknownBrightnessKeepsThemeColors() {
    // 任务栏明暗未知时不应改写主题色（否则会把颜色调反）
    const OverlayColors colors =
        overlayColors(QStringLiteral("霓虹"), QStringLiteral("#00ff88"), TaskbarBrightness::Unknown);
    QVERIFY(colors.up == QColor(0xFF, 0x70, 0x43));
    QVERIFY(colors.down == QColor(0x4F, 0xC3, 0xF7));
}

void TstTheme::customThemeKeepsUserColor() {
    const OverlayColors colors =
        overlayColors(QStringLiteral("自定"), QStringLiteral("#123456"), TaskbarBrightness::Light);
    QVERIFY(colors.up == QColor(0x12, 0x34, 0x56));
    QVERIFY(colors.down == QColor(0x12, 0x34, 0x56));
}

void TstTheme::borderColorIsUsableForEveryTheme() {
    const QColor lightTaskbar(0xf3, 0xf3, 0xf3);
    const QColor darkTaskbar(0x20, 0x20, 0x20);

    for (const QString& name : themeNames()) {
        const QColor onLight =
            overlayBorderColor(name, QStringLiteral("#123456"), TaskbarBrightness::Light);
        QVERIFY2(onLight.isValid(), qPrintable(name));
        QCOMPARE(onLight.alpha(), 204);
        // 边框要在任务栏上"有存在感"，又不能刺眼
        QVERIFY2(contrastRatio(onLight, lightTaskbar) >= 1.30, qPrintable(name));

        const QColor onDark =
            overlayBorderColor(name, QStringLiteral("#123456"), TaskbarBrightness::Dark);
        QVERIFY2(onDark.isValid(), qPrintable(name));
        QCOMPARE(onDark.alpha(), 204);
        QVERIFY2(contrastRatio(onDark, darkTaskbar) >= 1.30, qPrintable(name));
    }
}

QTEST_APPLESS_MAIN(TstTheme)

#include "tst_theme.moc"
