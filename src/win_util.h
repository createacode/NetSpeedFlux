// NetSpeedMon — Win32 辅助：任务栏几何、全屏检测、颜色采样、开机自启
#pragma once

#include <QColor>
#include <QRect>
#include <QString>

#include "theme.h"

namespace nsm::win {

// 任务栏顶层窗口（Shell_TrayWnd）。找不到时返回 nullptr。
void* taskbarWindow();

// 以下矩形均为物理像素；未找到对应窗口时返回空 QRect。
QRect taskbarRectPhysical();
QRect trayNotifyRectPhysical();
QRect startButtonRectPhysical();

bool isFullscreenApplicationActive();

// 从屏幕 DC 取一个像素（窗口 DC 拿不到 DWM 合成结果）。
QColor screenColorAt(int x, int y);

// 任务栏明暗：优先按屏幕采样结果判断，采样失败才退回系统浅色/深色设置。
TaskbarBrightness taskbarBrightness();

// "D:\path\app.exe" --min
QString autoStartCommand();
bool isAutoStartEnabled();
bool setAutoStart(bool enable, QString* error = nullptr);

}  // namespace nsm::win
