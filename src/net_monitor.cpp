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

namespace nsf {
namespace {

bool isInterestingInterface(const MIB_IF_ROW2& row) {
    if (row.Type == IF_TYPE_SOFTWARE_LOOPBACK) return false;
    if (row.Type == IF_TYPE_TUNNEL) return false;  // Teredo / ISATAP / VPN 隧道，避免重复计数
    if (row.OperStatus != IfOperStatusUp) return false;
    return true;
}

// GetIfTable2 会把每张网卡上挂的 NDIS 轻量筛选器（WFP / QoS Packet Scheduler）也列成
// 独立"接口"，而且它们的字节计数和物理网卡完全一样 —— 按接口求和会把这些流量算 3~4 遍
// （实测：一个 94MB/s 的下载被读成 4×94）。
// 因此优先只统计 HardwareInterface 为真的物理网卡；只有在找不到任何硬件接口时
// （极少数纯虚拟环境）才退回"全部可用接口"的旧逻辑。
bool isHardwareInterface(const MIB_IF_ROW2& row) {
    return row.InterfaceAndOperStatusFlags.HardwareInterface != FALSE;
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

    // 先判断是否存在物理网卡；有就只统计物理网卡，避免把筛选器接口重复计入
    bool hasHardwareInterface = false;
    for (ULONG index = 0; index < table->NumEntries; ++index) {
        const MIB_IF_ROW2& row = table->Table[index];
        if (isInterestingInterface(row) && isHardwareInterface(row)) {
            hasHardwareInterface = true;
            break;
        }
    }

    quint64 bytesIn = 0;
    quint64 bytesOut = 0;
    QHash<quint64, Counters> current;
    current.reserve(static_cast<int>(table->NumEntries));
    QStringList verboseParts;

    for (ULONG index = 0; index < table->NumEntries; ++index) {
        const MIB_IF_ROW2& row = table->Table[index];
        if (!isInterestingInterface(row)) continue;
        // 有物理网卡时忽略虚拟/筛选器接口（它们的计数与物理网卡重复）
        if (hasHardwareInterface && !isHardwareInterface(row)) continue;

        const quint64 key = row.InterfaceLuid.Value;
        const Counters now{row.InOctets, row.OutOctets};

        const auto previous = m_previous.constFind(key);
        if (previous != m_previous.constEnd() && !m_firstSample) {
            // 计数器回退说明接口被重置，这一帧按 0 处理，避免报出巨大峰值。
            const quint64 deltaIn = now.in >= previous->in ? now.in - previous->in : 0;
            const quint64 deltaOut = now.out >= previous->out ? now.out - previous->out : 0;
            bytesIn += deltaIn;
            bytesOut += deltaOut;
            if (m_verbose) {
                verboseParts << QStringLiteral("%1[type=%2] in=%3KB out=%4KB")
                                    .arg(QString::fromWCharArray(row.Alias))
                                    .arg(row.Type)
                                    .arg(deltaIn / 1024)
                                    .arg(deltaOut / 1024);
            }
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

    if (m_verbose) {
        Log::info(QStringLiteral("net-diag elapsed=%1ms  sumIn=%2KB/s  sumOut=%3KB/s  |  %4")
                      .arg(elapsedMs)
                      .arg(bytesIn / 1024)
                      .arg(bytesOut / 1024)
                      .arg(verboseParts.join(QStringLiteral(", "))));
    }

    emit updated(m_up, m_down);
}

}  // namespace nsf
