#include "app_paths.h"

#include <QDir>
#include <QFile>
#include <QStandardPaths>
#include <QTemporaryFile>

namespace nsm {

AppPaths& AppPaths::instance() {
    static AppPaths paths;
    return paths;
}

bool AppPaths::probeWritable(const QString& directory) {
    QDir dir(directory);
    if (!dir.exists() && !dir.mkpath(QStringLiteral("."))) return false;
    QTemporaryFile probe(dir.filePath(QStringLiteral("nsm-write-probe-XXXXXX")));
    if (!probe.open()) return false;
    probe.write("ok", 2);
    probe.flush();
    const bool ok = probe.error() == QFileDevice::NoError;
    probe.close();
    return ok;
}

void AppPaths::init(const QString& applicationDir) {
    const QDir exeDir(applicationDir);
    const QString legacyConfig = exeDir.filePath(QStringLiteral("config/settings.json"));

    // 便携优先：exe 目录可写就沿用历史行为（绿色版）。
    if (probeWritable(applicationDir)) {
        m_portable = true;
        m_configFile = legacyConfig;
        m_logDir = exeDir.filePath(QStringLiteral("logs"));
        return;
    }

    // 装到 Program Files 之类的只读目录：改用用户目录。
    m_portable = false;
    const QString configDir = QStandardPaths::writableLocation(QStandardPaths::AppConfigLocation);
    const QString dataDir = QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation);

    m_configFile = QDir(configDir).filePath(QStringLiteral("settings.json"));
    m_logDir = QDir(dataDir).filePath(QStringLiteral("logs"));

    // 旧便携配置迁移：只在目标不存在时复制一次，绝不覆盖用户已有配置。
    if (QFile::exists(legacyConfig) && !QFile::exists(m_configFile)) {
        QDir().mkpath(configDir);
        if (QFile::copy(legacyConfig, m_configFile)) {
            m_warning = QStringLiteral("已把便携配置迁移到 %1").arg(m_configFile);
        } else {
            m_warning = QStringLiteral("检测到旧配置 %1，但迁移失败，将使用默认配置").arg(legacyConfig);
        }
    }
}

}  // namespace nsm
