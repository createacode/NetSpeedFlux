#include "logger.h"

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QMutex>
#include <QMutexLocker>
#include <QTextStream>

#include <cstdio>

namespace nsm {
namespace {

QMutex g_mutex;
QString g_directory;
bool g_pruned = false;
bool g_reportedFailure = false;

constexpr int kKeepDays = 30;

// 调用方需持有 g_mutex。
void reportOnce(const QString& message) {
    if (g_reportedFailure) return;
    g_reportedFailure = true;
    std::fprintf(stderr, "[NetSpeedMon] %s\n", qPrintable(message));
}

}  // namespace

void Log::init(const QString& directory) {
    QMutexLocker locker(&g_mutex);
    g_directory = directory;
    g_pruned = false;
    g_reportedFailure = false;
}

QString Log::directory() {
    QMutexLocker locker(&g_mutex);
    return g_directory;
}

// 调用方需持有 g_mutex。
void Log::prune(const QString& directory) {
    QDir dir(directory);
    if (!dir.exists()) return;

    const QDateTime cutoff = QDateTime::currentDateTime().addDays(-kKeepDays);
    const QFileInfoList files =
        dir.entryInfoList({QStringLiteral("*.log")}, QDir::Files, QDir::Time);
    for (const QFileInfo& info : files) {
        if (info.lastModified() < cutoff) QFile::remove(info.absoluteFilePath());
    }
}

void Log::write(const QString& level, const QString& message) {
    QMutexLocker locker(&g_mutex);

    if (g_directory.isEmpty()) {
        std::fprintf(stderr, "[NetSpeedMon] %s\n", qPrintable(message));
        return;
    }

    QDir dir(g_directory);
    if (!dir.exists() && !dir.mkpath(QStringLiteral("."))) {
        reportOnce(QStringLiteral("无法创建日志目录：%1").arg(g_directory));
        std::fprintf(stderr, "[NetSpeedMon] %s\n", qPrintable(message));
        return;
    }

    if (!g_pruned) {
        g_pruned = true;
        prune(g_directory);
    }

    const QDateTime now = QDateTime::currentDateTime();
    const QString path =
        dir.filePath(now.toString(QStringLiteral("yyyy-MM-dd")) + QStringLiteral(".log"));

    QFile file(path);
    if (!file.open(QIODevice::Append | QIODevice::Text)) {
        reportOnce(QStringLiteral("无法写入日志文件：%1").arg(path));
        std::fprintf(stderr, "[NetSpeedMon] %s\n", qPrintable(message));
        return;
    }

    QTextStream out(&file);
    out << '[' << now.toString(QStringLiteral("yyyy-MM-dd hh:mm:ss")) << "] [" << level << "] "
        << message << '\n';
    file.flush();
}

void Log::info(const QString& message) {
    write(QStringLiteral("I"), message);
}

void Log::warning(const QString& message) {
    write(QStringLiteral("W"), message);
}

void Log::error(const QString& message) {
    write(QStringLiteral("E"), message);
}

}  // namespace nsm
