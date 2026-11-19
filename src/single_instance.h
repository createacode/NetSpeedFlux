// NetSpeedMon — 单实例（命名互斥 + 广播消息唤醒已有实例）
#pragma once

#include <QAbstractNativeEventFilter>
#include <QByteArray>
#include <QObject>

namespace nsm {

class SingleInstance : public QObject, public QAbstractNativeEventFilter {
    Q_OBJECT

public:
    explicit SingleInstance(QObject* parent = nullptr);
    ~SingleInstance() override;

    // true 表示本进程抢到了唯一实例；false 表示已有实例在运行。
    bool tryAcquire();

    // 通知已有实例把主界面拉到前台（尽力而为）。
    void notifyExisting() const;

    bool nativeEventFilter(const QByteArray& eventType, void* message, qintptr* result) override;

signals:
    void activationRequested();

private:
    void* m_mutex = nullptr;
    unsigned int m_messageId = 0;
};

}  // namespace nsm
