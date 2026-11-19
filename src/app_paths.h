// NetSpeedMon — 运行期路径策略（便携优先，不可写时回退到用户目录）
#pragma once

#include <QString>

namespace nsm {

class AppPaths {
public:
    static AppPaths& instance();

    // 必须在 QApplication 创建、并设置 organizationName/applicationName 之后调用。
    void init(const QString& applicationDir);

    QString configFile() const { return m_configFile; }
    QString logDir() const { return m_logDir; }
    bool portable() const { return m_portable; }
    // 初始化过程中的提醒（例如从旧位置迁移了配置），可直接写日志。
    QString warning() const { return m_warning; }

    static bool probeWritable(const QString& directory);

private:
    AppPaths() = default;

    QString m_configFile;
    QString m_logDir;
    bool m_portable = false;
    QString m_warning;
};

}  // namespace nsm
