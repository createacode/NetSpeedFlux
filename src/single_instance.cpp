// Win32 头必须最先包含。
#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include "single_instance.h"

#include <QCoreApplication>

#include "logger.h"

namespace nsf {
namespace {

constexpr wchar_t kMutexName[] = L"NetSpeedFlux_SingleInstance_v2";
constexpr wchar_t kActivateMessageName[] = L"NetSpeedFlux_Activate_v2";

}  // namespace

SingleInstance::SingleInstance(QObject* parent) : QObject(parent) {
    m_messageId = RegisterWindowMessageW(kActivateMessageName);
    if (QCoreApplication::instance()) {
        QCoreApplication::instance()->installNativeEventFilter(this);
    }
}

SingleInstance::~SingleInstance() {
    if (QCoreApplication::instance()) {
        QCoreApplication::instance()->removeNativeEventFilter(this);
    }
    if (m_mutex) {
        CloseHandle(static_cast<HANDLE>(m_mutex));
        m_mutex = nullptr;
    }
}

bool SingleInstance::tryAcquire() {
    SetLastError(0);
    m_mutex = CreateMutexW(nullptr, FALSE, kMutexName);
    if (!m_mutex) {
        // 创建互斥体失败时不要拦住用户，宁可允许多开。
        Log::warning(QStringLiteral("创建单实例互斥体失败，错误码 %1").arg(GetLastError()));
        return true;
    }
    return GetLastError() != ERROR_ALREADY_EXISTS;
}

void SingleInstance::notifyExisting() const {
    if (m_messageId == 0) return;

    // 允许已有实例把窗口切到前台（受前台锁限制，属于尽力而为）。
    AllowSetForegroundWindow(ASFW_ANY);

    // 已有实例可能还在创建窗口，短时间重复广播几次，避免“双击没反应”。
    for (int attempt = 0; attempt < 3; ++attempt) {
        PostMessageW(HWND_BROADCAST, m_messageId, 0, 0);
        Sleep(100);
    }
}

bool SingleInstance::nativeEventFilter(const QByteArray& eventType, void* message, qintptr* result) {
    Q_UNUSED(eventType)
    Q_UNUSED(result)

    if (m_messageId == 0) return false;

    MSG* msg = static_cast<MSG*>(message);
    if (msg && msg->message == m_messageId) {
        emit activationRequested();
    }
    return false;
}

}  // namespace nsf
