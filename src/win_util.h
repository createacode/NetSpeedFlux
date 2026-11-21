// NetSpeedFlux — Win32 辅助：任务栏几何、全屏检测、颜色采样、开机自启
#pragma once

#include <QColor>
#include <QPair>
#include <QRect>
#include <QString>
#include <QVector>

#include "theme.h"

namespace nsf::win {

// 任务栏顶层窗口（Shell_TrayWnd）。找不到时返回 nullptr。
void* taskbarWindow();

// 以下矩形均为物理像素；未找到对应窗口时返回空 QRect。
QRect taskbarRectPhysical();
QRect trayNotifyRectPhysical();

// 任务栏上"有内容"的水平区间（物理像素、绝对 x 坐标，横向任务栏）。
// 用屏幕 DC 逐列扫描得到：比较每列与背景参考行的最大色差，所以连 Win11 里
// XAML 直接绘制的组件（天气小组件、居中任务栏的图标）也能识别 —— 这些没有独立 HWND，
// 只靠 FindWindow 是发现不了的。扫描失败时返回空列表。
QVector<QPair<int, int>> taskbarContentRanges(const QRect& taskbarPhysical);

bool isFullscreenApplicationActive();

// 从屏幕 DC 取一个像素（窗口 DC 拿不到 DWM 合成结果）。
QColor screenColorAt(int x, int y);

// 任务栏明暗：优先按屏幕采样结果判断，采样失败才退回系统浅色/深色设置。
TaskbarBrightness taskbarBrightness();

// "D:\path\app.exe" --min
QString autoStartCommand();
bool isAutoStartEnabled();
bool setAutoStart(bool enable, QString* error = nullptr);

}  // namespace nsf::win
