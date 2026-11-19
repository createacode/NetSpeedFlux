#include <QtTest>

#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTemporaryDir>

#include "config.h"

using namespace nsf;

class TstConfig : public QObject {
    Q_OBJECT

private slots:
    void defaultsAreSane();
    void saveAndLoadRoundTrip();
    void clampsOutOfRangeValues();
    void reportsCorruptFile();
    void reportsMissingFile();
    void migratesLegacyGeometryKeys();
    void writesLegacyKeysForDowngrade();
    void enumConversionsRoundTrip();
};

void TstConfig::defaultsAreSane() {
    const Config config = Config::defaults();
    QVERIFY(config.fontSize >= 8 && config.fontSize <= 48);
    QVERIFY(!config.theme.isEmpty());
    QVERIFY(!config.hasWindowGeometry);
}

void TstConfig::saveAndLoadRoundTrip() {
    QTemporaryDir dir;
    QVERIFY(dir.isValid());

    const QString path = dir.filePath(QStringLiteral("settings.json"));
    const ConfigStore store(path);

    Config config = Config::defaults();
    config.theme = QStringLiteral("赛博");
    config.unit = UnitMode::MB;
    config.anchor = OverlayAnchor::Center;
    config.fontFamily = QStringLiteral("Consolas");
    config.fontSize = 20;
    config.bold = true;
    config.italic = true;
    config.showTray = false;
    config.showOverlay = true;
    config.fullscreen = FullscreenAction::Show;
    config.autoStart = true;
    config.customColor = QStringLiteral("#123456");
    config.hasWindowGeometry = true;
    config.windowGeometry = QRect(120, 80, 760, 420);

    QString error;
    QVERIFY2(store.save(config, &error), qPrintable(error));

    const ConfigLoadResult result = store.load();
    QVERIFY(!result.parseError);
    QVERIFY(!result.fileMissing);
    QCOMPARE(result.config.theme, config.theme);
    QVERIFY(result.config.unit == config.unit);
    QVERIFY(result.config.anchor == config.anchor);
    QCOMPARE(result.config.fontFamily, config.fontFamily);
    QCOMPARE(result.config.fontSize, config.fontSize);
    QCOMPARE(result.config.bold, config.bold);
    QCOMPARE(result.config.italic, config.italic);
    QCOMPARE(result.config.showTray, config.showTray);
    QCOMPARE(result.config.showOverlay, config.showOverlay);
    QVERIFY(result.config.fullscreen == config.fullscreen);
    QCOMPARE(result.config.autoStart, config.autoStart);
    QCOMPARE(result.config.customColor, config.customColor);
    QVERIFY(result.config.hasWindowGeometry);
    QCOMPARE(result.config.windowGeometry, config.windowGeometry);
}

void TstConfig::clampsOutOfRangeValues() {
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString path = dir.filePath(QStringLiteral("settings.json"));

    QFile file(path);
    QVERIFY(file.open(QIODevice::WriteOnly));
    file.write(R"({"fs":999,"theme":"不存在的主题","fc":"blue","wx":10,"wy":20,"ww":10,"wh":10})");
    file.close();

    const ConfigStore store(path);
    const ConfigLoadResult result = store.load();
    QVERIFY(!result.parseError);

    QCOMPARE(result.config.fontSize, 48);
    QCOMPARE(result.config.theme, QStringLiteral("霓虹"));
    QCOMPARE(result.config.customColor, QStringLiteral("#00ff88"));
    QVERIFY(result.config.hasWindowGeometry);
    QVERIFY(result.config.windowGeometry.width() >= kMinWindowWidth);
    QVERIFY(result.config.windowGeometry.height() >= kMinWindowHeight);
}

void TstConfig::reportsCorruptFile() {
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString path = dir.filePath(QStringLiteral("settings.json"));

    QFile file(path);
    QVERIFY(file.open(QIODevice::WriteOnly));
    file.write(R"({"ff": )");
    file.close();

    const ConfigStore store(path);
    const ConfigLoadResult result = store.load();
    QVERIFY(result.parseError);
    QVERIFY(!result.error.isEmpty());
    QCOMPARE(result.config.theme, Config::defaults().theme);
}

void TstConfig::reportsMissingFile() {
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString path = dir.filePath(QStringLiteral("does-not-exist.json"));

    const ConfigStore store(path);
    const ConfigLoadResult result = store.load();
    QVERIFY(result.fileMissing);
    QVERIFY(!result.parseError);
    QCOMPARE(result.config.fontSize, Config::defaults().fontSize);
}

void TstConfig::migratesLegacyGeometryKeys() {
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString path = dir.filePath(QStringLiteral("settings.json"));

    QFile file(path);
    QVERIFY(file.open(QIODevice::WriteOnly));
    file.write(R"({"ff":"Arial","fs":14,"wx":100,"wy":50,"ww":800,"wh":400})");
    file.close();

    const ConfigStore store(path);
    const ConfigLoadResult result = store.load();
    QVERIFY(result.config.hasWindowGeometry);
    QCOMPARE(result.config.windowGeometry, QRect(100, 50, 800, 400));
    QCOMPARE(result.config.fontFamily, QStringLiteral("Arial"));
}

void TstConfig::writesLegacyKeysForDowngrade() {
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString path = dir.filePath(QStringLiteral("settings.json"));

    Config config = Config::defaults();
    config.hasWindowGeometry = true;
    config.windowGeometry = QRect(10, 20, 760, 420);

    const ConfigStore store(path);
    QString error;
    QVERIFY2(store.save(config, &error), qPrintable(error));

    QFile file(path);
    QVERIFY(file.open(QIODevice::ReadOnly));
    const QJsonObject object = QJsonDocument::fromJson(file.readAll()).object();
    QCOMPARE(object.value(QStringLiteral("wx")).toInt(), 10);
    QCOMPARE(object.value(QStringLiteral("wy")).toInt(), 20);
    QCOMPARE(object.value(QStringLiteral("ww")).toInt(), 760);
    QCOMPARE(object.value(QStringLiteral("wh")).toInt(), 420);
}

void TstConfig::enumConversionsRoundTrip() {
    const QList<OverlayAnchor> anchors{OverlayAnchor::Left, OverlayAnchor::Center,
                                      OverlayAnchor::Right};
    for (OverlayAnchor anchor : anchors) {
        QVERIFY(anchorFromString(anchorToString(anchor)) == anchor);
    }

    const QList<FullscreenAction> actions{FullscreenAction::Show, FullscreenAction::Hide};
    for (FullscreenAction action : actions) {
        QVERIFY(fullscreenFromString(fullscreenToString(action)) == action);
    }
}

QTEST_APPLESS_MAIN(TstConfig)

#include "tst_config.moc"
