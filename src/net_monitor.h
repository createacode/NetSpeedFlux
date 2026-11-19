// NetSpeedFlux — 网速采样（GetIfTable2，64 位计数器 + 按接口差分 + 真实耗时归一化）
#pragma once

#include <QElapsedTimer>
#include <QHash>
#include <QObject>
#include <QTimer>

namespace nsf {

class NetMonitor : public QObject {
    Q_OBJECT

public:
    explicit NetMonitor(QObject* parent = nullptr);

    void start();
    void stop();

    quint64 uploadBytesPerSecond() const { return m_up; }
    quint64 downloadBytesPerSecond() const { return m_down; }

signals:
    void updated(quint64 up, quint64 down);

private slots:
    void poll();

private:
    struct Counters {
        quint64 in = 0;
        quint64 out = 0;
    };

    QTimer m_timer;
    QElapsedTimer m_clock;
    QHash<quint64, Counters> m_previous;
    quint64 m_up = 0;
    quint64 m_down = 0;
    bool m_firstSample = true;
    int m_consecutiveErrors = 0;
};

}  // namespace nsf
