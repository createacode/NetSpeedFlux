#include "speed_format.h"

#include <QStringList>

namespace nsm {
namespace {

constexpr double kKilo = 1024.0;
constexpr double kMega = 1024.0 * 1024.0;

// 数字精度：<10 两位，<100 一位，其余取整。
QString formatNumber(double value) {
    const int decimals = (value < 10.0) ? 2 : (value < 100.0 ? 1 : 0);
    return QString::number(value, 'f', decimals);
}

}  // namespace

UnitMode unitModeFromString(const QString& text, UnitMode fallback) {
    if (text == QLatin1String("auto")) return UnitMode::Auto;
    if (text == QLatin1String("KB")) return UnitMode::KB;
    if (text == QLatin1String("MB")) return UnitMode::MB;
    if (text == QLatin1String("short")) return UnitMode::Short;
    return fallback;
}

QString unitModeToString(UnitMode mode) {
    switch (mode) {
    case UnitMode::KB: return QStringLiteral("KB");
    case UnitMode::MB: return QStringLiteral("MB");
    case UnitMode::Short: return QStringLiteral("short");
    case UnitMode::Auto:
    default: return QStringLiteral("auto");
    }
}

QString unitModeDisplayName(UnitMode mode) {
    switch (mode) {
    case UnitMode::KB: return QStringLiteral("KB");
    case UnitMode::MB: return QStringLiteral("MB");
    case UnitMode::Short: return QStringLiteral("短 (K/M)");
    case UnitMode::Auto:
    default: return QStringLiteral("自动");
    }
}

SpeedParts formatSpeedParts(double bytesPerSec, UnitMode mode) {
    double value = bytesPerSec > 0.0 ? bytesPerSec : 0.0;
    QString unit;

    switch (mode) {
    case UnitMode::KB:
        value /= kKilo;
        unit = QStringLiteral(" KB/s");
        break;
    case UnitMode::MB:
        value /= kMega;
        unit = QStringLiteral(" MB/s");
        break;
    case UnitMode::Short:
        if (value < kKilo) {
            unit = QStringLiteral(" B/s");
        } else if (value < kMega) {
            value /= kKilo;
            unit = QStringLiteral(" K/s");
        } else {
            value /= kMega;
            unit = QStringLiteral(" M/s");
        }
        break;
    case UnitMode::Auto:
    default:
        if (value < kKilo) {
            unit = QStringLiteral(" B/s");
        } else if (value < kMega) {
            value /= kKilo;
            unit = QStringLiteral(" KB/s");
        } else {
            value /= kMega;
            unit = QStringLiteral(" MB/s");
        }
        break;
    }

    SpeedParts parts{formatNumber(value), unit};
    return parts;
}

QString formatSpeed(double bytesPerSec, UnitMode mode) {
    const SpeedParts parts = formatSpeedParts(bytesPerSec, mode);
    return parts.number + parts.unit;
}

QStringList numberWidthSamples(UnitMode mode) {
    // 覆盖各档小数位数，以及该单位模式下的量级上限（约 10 Gbps）。
    QStringList samples{QStringLiteral("8.88"), QStringLiteral("88.8")};
    switch (mode) {
    case UnitMode::KB:
        // 10 Gbps ≈ 1220703 KB/s
        samples << QStringLiteral("999") << QStringLiteral("9999999");
        break;
    case UnitMode::MB:
    case UnitMode::Short:
    case UnitMode::Auto:
    default:
        // 10 Gbps ≈ 1192 MB/s
        samples << QStringLiteral("999") << QStringLiteral("9999");
        break;
    }
    return samples;
}

QStringList allUnitLabels() {
    return {QStringLiteral(" B/s"), QStringLiteral(" KB/s"), QStringLiteral(" MB/s"),
            QStringLiteral(" K/s"), QStringLiteral(" M/s")};
}

}  // namespace nsm
