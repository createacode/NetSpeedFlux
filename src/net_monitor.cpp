// Win32 头必须最先包含。
#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
// netioapi.h 的用户态部分整段包在 #ifdef _WS2IPDEF_ 里，必须先包含 ws2ipdef.h，
// 否则 MIB_IF_ROW2 / GetIfTable2 都不会被声明。
#include <winsock2.h>
#include <ws2ipdef.h>
#include <windows.h>

#include <iphlpapi.h>
#include <netioapi.h>  // MIB_IF_TABLE2 / MIB_IF_ROW2 / GetIfTable2 / FreeMibTable

#include "net_monitor.h"

#include "logger.h"

namespace nsm {
namespace {

bool isInterestingInterface(const MIB_IF_ROW2& row) {
    if (row.Type == IF_TYPE_SOFTWARE_LOOPBACK) return false;
    if (row.Type == IF_TYPE_TUNNEL) return false;  // Teredo / ISATAP / VPN 隧道，避免重复计数
    if (row.OperStatus != IfOperStatusUp) return false;
    return true;
}

}  // namespace

NetMonitor::NetMonitor(QObject* parent) : QObject(parent) {
    m_timer.setInterval(1000);
    m_timer.setTimerType(Qt::CoarseTimer);
    connect(&m_timer, &QTimer::timeout, this, &NetMonitor::poll);
}

void NetMonitor::start() {
    m_previous.clear();
    m_firstSample = true;
    m_consecutiveErrors = 0;
    m_clock.start();
    m_timer.start();
    poll();
}

void NetMonitor::stop() {
    m_timer.stop();
}

void NetMonitor::poll() {
    const qint64 elapsedMs = m_clock.restart();

    MIB_IF_TABLE2* table = nullptr;
    const ULONG status = GetIfTable2(&table);
    if (status != NO_ERROR || table == nullptr) {
        // 采样失败时保持上一次结果，不把界面清零，避免闪烁。
        if (++m_consecutiveErrors <= 3) {
            Log::warning(QStringLiteral("GetIfTable2 失败，错误码 %1").arg(status));
        }
        if (table != nullptr) FreeMibTable(table);
        return;
    }

    m_consecutiveErrors = 0;

    quint64 bytesIn = 0;
    quint64 bytesOut = 0;
    QHash<quint64, Counters> current;
    current.reserve(static_cast<int>(table->NumEntries));

    for (ULONG index = 0; index < table->NumEntries; ++index) {
        const MIB_IF_ROW2& row = table->Table[index];
        if (!isInterestingInterface(row)) continue;

        const quint64 key = row.InterfaceLuid.Value;
        const Counters now{row.InOctets, row.OutOctets};

        const auto previous = m_previous.constFind(key);
        if (previous != m_previous.constEnd() && !m_firstSample) {
            // 计数器回退说明接口被重置，这一帧按 0 处理，避免报出巨大峰值。
            if (now.in >= previous->in) bytesIn += now.in - previous->in;
            if (now.out >= previous->out) bytesOut += now.out - previous->out;
        }
        current.insert(key, now);
    }

    FreeMibTable(table);
    m_previous = current;

    if (m_firstSample) {
        m_firstSample = false;
        m_up = 0;
        m_down = 0;
        emit updated(m_up, m_down);
        return;
    }

    if (elapsedMs > 0) {
        const double seconds = static_cast<double>(elapsedMs) / 1000.0;
        m_down = static_cast<quint64>(static_cast<double>(bytesIn) / seconds);
        m_up = static_cast<quint64>(static_cast<double>(bytesOut) / seconds);
    }

    emit updated(m_up, m_down);
}

}  // namespace nsm
