#include <QtTest>

#include "speed_format.h"

using namespace nsf;

class TstSpeedFormat : public QObject {
    Q_OBJECT

private slots:
    void zeroAndNegative();
    void autoMode();
    void kilobytesMode();
    void megabytesMode();
    void shortMode();
    void largeValuesAreNotClippedBySamples();
    void unitModeStringsRoundTrip();
    void widthSamplesCoverEveryMode();
};

void TstSpeedFormat::zeroAndNegative() {
    const SpeedParts zero = formatSpeedParts(0.0, UnitMode::Auto);
    QCOMPARE(zero.number, QStringLiteral("0.00"));
    QCOMPARE(zero.unit, QStringLiteral(" B/s"));

    const SpeedParts negative = formatSpeedParts(-12345.0, UnitMode::Auto);
    QCOMPARE(negative.number, QStringLiteral("0.00"));
    QCOMPARE(negative.unit, QStringLiteral(" B/s"));
}

void TstSpeedFormat::autoMode() {
    QCOMPARE(formatSpeed(512.0, UnitMode::Auto), QStringLiteral("512 B/s"));
    QCOMPARE(formatSpeed(1024.0, UnitMode::Auto), QStringLiteral("1.00 KB/s"));
    QCOMPARE(formatSpeed(1536.0, UnitMode::Auto), QStringLiteral("1.50 KB/s"));
    QCOMPARE(formatSpeed(1024.0 * 1024.0, UnitMode::Auto), QStringLiteral("1.00 MB/s"));
    QCOMPARE(formatSpeed(96.5 * 1024.0, UnitMode::Auto), QStringLiteral("96.5 KB/s"));
}

void TstSpeedFormat::kilobytesMode() {
    QCOMPARE(formatSpeed(512.0, UnitMode::KB), QStringLiteral("0.50 KB/s"));
    QCOMPARE(formatSpeed(1536.0, UnitMode::KB), QStringLiteral("1.50 KB/s"));

    // 100 MB/s → 102400 KB/s，必须完整保留 6 位数字（旧实现会按 3 位裁切）。
    const SpeedParts hundredMb = formatSpeedParts(100.0 * 1024.0 * 1024.0, UnitMode::KB);
    QCOMPARE(hundredMb.number, QStringLiteral("102400"));
    QCOMPARE(hundredMb.unit, QStringLiteral(" KB/s"));
}

void TstSpeedFormat::megabytesMode() {
    QCOMPARE(formatSpeed(1024.0 * 1024.0, UnitMode::MB), QStringLiteral("1.00 MB/s"));
    QCOMPARE(formatSpeed(512.0 * 1024.0, UnitMode::MB), QStringLiteral("0.50 MB/s"));
}

void TstSpeedFormat::shortMode() {
    QCOMPARE(formatSpeed(512.0, UnitMode::Short), QStringLiteral("512 B/s"));
    QCOMPARE(formatSpeed(1536.0, UnitMode::Short), QStringLiteral("1.50 K/s"));
    QCOMPARE(formatSpeed(3.0 * 1024.0 * 1024.0, UnitMode::Short), QStringLiteral("3.00 M/s"));
}

void TstSpeedFormat::largeValuesAreNotClippedBySamples() {
    // 1 Gbps 下 KB 模式是 6 位数，宽度样本必须能覆盖。
    const QStringList samples = numberWidthSamples(UnitMode::KB);
    bool hasWideSample = false;
    for (const QString& sample : samples) {
        if (sample.size() >= 7) hasWideSample = true;
    }
    QVERIFY(hasWideSample);
}

void TstSpeedFormat::unitModeStringsRoundTrip() {
    const QList<UnitMode> modes{UnitMode::Auto, UnitMode::KB, UnitMode::MB, UnitMode::Short};
    for (UnitMode mode : modes) {
        QVERIFY(unitModeFromString(unitModeToString(mode)) == mode);
    }
    QVERIFY(unitModeFromString(QStringLiteral("nonsense")) == UnitMode::Auto);
}

void TstSpeedFormat::widthSamplesCoverEveryMode() {
    const QList<UnitMode> modes{UnitMode::Auto, UnitMode::KB, UnitMode::MB, UnitMode::Short};
    for (UnitMode mode : modes) {
        QVERIFY(!numberWidthSamples(mode).isEmpty());
    }
    QVERIFY(allUnitLabels().contains(QStringLiteral(" MB/s")));
}

QTEST_APPLESS_MAIN(TstSpeedFormat)

#include "tst_speed_format.moc"
