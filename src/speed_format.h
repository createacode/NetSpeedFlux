// NetSpeedMon — 速度格式化（纯逻辑，可单测）
#pragma once

#include <QString>
#include <QStringList>

namespace nsm {

// 单位模式。配置文件里沿用历史字符串 "auto"/"KB"/"MB"/"short"。
enum class UnitMode {
    Auto,
    KB,
    MB,
    Short,
};

struct SpeedParts {
    QString number;  // 数字部分，例如 "12.34"
    QString unit;    // 单位部分，带前导空格，例如 " MB/s"
};

UnitMode unitModeFromString(const QString& text, UnitMode fallback = UnitMode::Auto);
QString unitModeToString(UnitMode mode);
QString unitModeDisplayName(UnitMode mode);

// 只做数值与单位换算，不关心刷新频率。
SpeedParts formatSpeedParts(double bytesPerSec, UnitMode mode);
QString formatSpeed(double bytesPerSec, UnitMode mode);

// 布局用：某个单位模式下数字部分可能达到的最大宽度样本。
QStringList numberWidthSamples(UnitMode mode);
// 布局用：所有可能的单位标签。
QStringList allUnitLabels();

}  // namespace nsm
