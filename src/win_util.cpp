// Win32 头必须最先包含，且禁用 min/max 宏，避免污染 Qt 头文件。
#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <shellapi.h>

#include "win_util.h"

#include <QCoreApplication>
#include <QDir>
#include <QVector>

#include <algorithm>

namespace nsf::win {
namespace {

constexpr wchar_t kRunKey[] = L"Software\\Microsoft\\Windows\\CurrentVersion\\Run";
constexpr wchar_t kValueName[] = L"NetSpeedFlux";
// 更名前的启动项名字，安装/启用时顺手清掉，避免留下指向旧 exe 的死项
constexpr wchar_t kLegacyValueName[] = L"NetSpeedMon";

QString readRegistryString(HKEY root, const wchar_t* subKey, const wchar_t* value) {
    wchar_t buffer[2048] = {};
    DWORD size = sizeof(buffer);
    DWORD type = 0;
    // RRF_RT_ANY：Run 项既可能是 REG_SZ 也可能是 REG_EXPAND_SZ，
    // 用具体类型标志组合容易踩到参数校验，这里读出来再判断类型。
    const LSTATUS status = RegGetValueW(root, subKey, value, RRF_RT_ANY, &type, buffer, &size);
    if (status != ERROR_SUCCESS) return QString();
    if (type != REG_SZ && type != REG_EXPAND_SZ) return QString();
    return QString::fromWCharArray(buffer);
}

DWORD readRegistryDword(HKEY root, const wchar_t* subKey, const wchar_t* value, bool* ok) {
    DWORD data = 0;
    DWORD size = sizeof(data);
    DWORD type = 0;
    const LSTATUS status =
        RegGetValueW(root, subKey, value, RRF_RT_REG_DWORD, &type, &data, &size);
    if (ok) *ok = (status == ERROR_SUCCESS);
    return data;
}

QRect windowRectPhysical(void* hwnd) {
    if (!hwnd) return QRect();
    RECT rect = {};
    if (!GetWindowRect(static_cast<HWND>(hwnd), &rect)) return QRect();
    const QRect result(rect.left, rect.top, rect.right - rect.left, rect.bottom - rect.top);
    return result.isEmpty() ? QRect() : result;
}

void* findTrayChild(const wchar_t* className) {
    void* taskbar = taskbarWindow();
    if (!taskbar) return nullptr;
    return FindWindowExW(static_cast<HWND>(taskbar), nullptr, className, nullptr);
}


}  // namespace

void* taskbarWindow() {
    return FindWindowW(L"Shell_TrayWnd", nullptr);
}

QRect taskbarRectPhysical() {
    return windowRectPhysical(taskbarWindow());
}

QRect trayNotifyRectPhysical() {
    void* notify = findTrayChild(L"TrayNotifyWnd");
    if (!notify) notify = FindWindowW(L"TrayNotifyWnd", nullptr);
    if (!notify) return QRect();

    // 用通知区容器本身的左边界作锚点：它在"显示隐藏图标"箭头（chevron）的左侧，
    // 悬浮窗贴上去不会压住箭头。Win10 上的 ToolbarWindow32 只覆盖可见图标区、
    // 不含箭头，用它当锚点会导致悬浮窗压到箭头上，所以只作兜底。
    const QRect rect = windowRectPhysical(notify);
    if (!rect.isEmpty()) return rect;
    if (void* toolbar = FindWindowExW(static_cast<HWND>(notify), nullptr, L"ToolbarWindow32", nullptr)) {
        return windowRectPhysical(toolbar);
    }
    return QRect();
}

QVector<QPair<int, int>> taskbarContentRanges(const QRect& taskbar) {
    QVector<QPair<int, int>> ranges;
    const int width = taskbar.width();
    const int height = taskbar.height();
    if (width <= 0 || height <= 4) return ranges;

    HDC screen = GetDC(nullptr);
    if (!screen) return ranges;
    HDC memory = CreateCompatibleDC(screen);

    BITMAPINFO info = {};
    info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    info.bmiHeader.biWidth = width;
    info.bmiHeader.biHeight = -height;  // 负数 = 自上而下，行序与屏幕一致
    info.bmiHeader.biPlanes = 1;
    info.bmiHeader.biBitCount = 32;
    info.bmiHeader.biCompression = BI_RGB;

    void* bits = nullptr;
    HBITMAP bitmap = CreateDIBSection(screen, &info, DIB_RGB_COLORS, &bits, nullptr, 0);
    if (bitmap != nullptr && bits != nullptr) {
        const HGDIOBJ previous = SelectObject(memory, bitmap);
        if (BitBlt(memory, 0, 0, width, height, screen, taskbar.left(), taskbar.top(), SRCCOPY)) {
            const auto* pixels = static_cast<const quint32*>(bits);
            // 参考行取第 3 行：避开任务栏顶部的高光/描边
            const int referenceRow = 3;
            bool occupied = false;
            int start = 0;
            for (int column = 0; column < width; ++column) {
                const QRgb background = pixels[referenceRow * width + column];
                int difference = 0;
                for (int row = 4; row < height; ++row) {
                    const QRgb sample = pixels[row * width + column];
                    difference = qMax(difference, qAbs(qRed(sample) - qRed(background)) +
                                                     qAbs(qGreen(sample) - qGreen(background)) +
                                                     qAbs(qBlue(sample) - qBlue(background)));
                }
                const bool here = difference > 40;  // 任务栏背景是平滑渐变，40 足够区分图标/文字
                if (here && !occupied) {
                    occupied = true;
                    start = column;
                } else if (!here && occupied) {
                    occupied = false;
                    if (column - start >= 3) ranges.append({start + taskbar.left(), column + taskbar.left()});
                }
            }
            if (occupied) ranges.append({start + taskbar.left(), width + taskbar.left()});
        }
        SelectObject(memory, previous);
        DeleteObject(bitmap);
    }
    DeleteDC(memory);
    ReleaseDC(nullptr, screen);

    // 合并相邻区间之间的小空隙（图标间距约 8px，不该被当成可用空位）
    QVector<QPair<int, int>> merged;
    for (const auto& range : ranges) {
        if (!merged.isEmpty() && range.first - merged.last().second <= 8) {
            merged.last().second = range.second;
        } else {
            merged.append(range);
        }
    }
    return merged;
}


bool isFullscreenApplicationActive() {
    QUERY_USER_NOTIFICATION_STATE state = QUNS_NOT_PRESENT;
    if (FAILED(SHQueryUserNotificationState(&state))) return false;
    return state == QUNS_BUSY || state == QUNS_RUNNING_D3D_FULL_SCREEN ||
           state == QUNS_PRESENTATION_MODE;
}

QColor screenColorAt(int x, int y) {
    HDC screen = GetDC(nullptr);
    if (!screen) return QColor();
    const COLORREF pixel = GetPixel(screen, x, y);
    ReleaseDC(nullptr, screen);
    if (pixel == CLR_INVALID) return QColor();
    return QColor(GetRValue(pixel), GetGValue(pixel), GetBValue(pixel));
}

TaskbarBrightness taskbarBrightness() {
    // 任务栏由 DWM 合成，窗口 DC 采不到真实颜色，必须从屏幕 DC 取。
    // 只采任务栏上下边缘的空白条带：中间是图标区，采到图标会把中位数拉暗，
    // 进而把"浅色任务栏"误判成深色（会把文字越调越淡）。
    const QRect taskbar = taskbarRectPhysical();
    if (!taskbar.isEmpty()) {
        QVector<int> luminances;
        luminances.reserve(10);
        const int rows[2] = {taskbar.top() + 3, taskbar.bottom() - 4};
        for (int row = 0; row < 2; ++row) {
            for (int i = 1; i <= 5; ++i) {
                const int x = taskbar.left() + taskbar.width() * i / 6;
                const QColor color = screenColorAt(x, rows[row]);
                if (color.isValid()) {
                    luminances.append(static_cast<int>(relativeLuminance(color) * 1000.0));
                }
            }
        }
        if (!luminances.isEmpty()) {
            std::sort(luminances.begin(), luminances.end());
            const int median = luminances.at(luminances.size() / 2);
            return median > 450 ? TaskbarBrightness::Light : TaskbarBrightness::Dark;
        }
    }

    // 采样失败时退回系统浅色/深色设置。
    static const wchar_t kPersonalize[] =
        L"Software\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize";
    bool ok = false;
    const DWORD light = readRegistryDword(HKEY_CURRENT_USER, kPersonalize, L"SystemUsesLightTheme", &ok);
    if (!ok) return TaskbarBrightness::Unknown;
    return light != 0 ? TaskbarBrightness::Light : TaskbarBrightness::Dark;
}
QString autoStartCommand() {
    const QString exe = QDir::toNativeSeparators(QCoreApplication::applicationFilePath());
    return QStringLiteral("\"%1\" --min").arg(exe);
}

bool isAutoStartEnabled() {
    const QString current = readRegistryString(HKEY_CURRENT_USER, kRunKey, kValueName);
    return !current.isEmpty() && current == autoStartCommand();
}

bool setAutoStart(bool enable, QString* error) {
    HKEY key = nullptr;
    const LSTATUS openStatus = RegOpenKeyExW(HKEY_CURRENT_USER, kRunKey, 0, KEY_READ | KEY_WRITE, &key);
    if (openStatus != ERROR_SUCCESS) {
        if (error) *error = QStringLiteral("无法打开注册表启动项（错误码 %1）").arg(openStatus);
        return false;
    }

    const QString command = autoStartCommand();
    const QString current = readRegistryString(HKEY_CURRENT_USER, kRunKey, kValueName);
    LSTATUS status = ERROR_SUCCESS;

    if (enable) {
        // 值一致就不写，避免每次启动都改注册表（也减少杀软误报）。
        if (current != command) {
            const DWORD bytes = static_cast<DWORD>((command.size() + 1) * sizeof(wchar_t));
            status = RegSetValueExW(key, kValueName, 0, REG_SZ,
                                    reinterpret_cast<const BYTE*>(command.utf16()), bytes);
        }
    } else if (!current.isEmpty()) {
        status = RegDeleteValueW(key, kValueName);
        if (status == ERROR_FILE_NOT_FOUND) status = ERROR_SUCCESS;
    }

    // 清理更名前遗留的启动项（只在 key 还开着的时候尝试一次）
    RegDeleteValueW(key, kLegacyValueName);

    RegCloseKey(key);

    if (status != ERROR_SUCCESS) {
        if (error) *error = QStringLiteral("写入注册表启动项失败（错误码 %1）").arg(status);
        return false;
    }
    return true;
}

}  // namespace nsf::win
