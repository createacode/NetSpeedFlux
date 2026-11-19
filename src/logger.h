// NetSpeedMon — 按天分割的文件日志
#pragma once

#include <QString>

namespace nsm {

class Log {
public:
    static void init(const QString& directory);

    static void info(const QString& message);
    static void warning(const QString& message);
    static void error(const QString& message);

    static QString directory();

private:
    static void write(const QString& level, const QString& message);
    static void prune(const QString& directory);
};

}  // namespace nsm
