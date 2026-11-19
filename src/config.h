// NetSpeedFlux — 配置结构与持久化
#pragma once

#include <QRect>
#include <QString>

#include "speed_format.h"

namespace nsf {

enum class OverlayAnchor {
    Left,
    Center,
    Right,
};

enum class FullscreenAction {
    Show,
    Hide,
};

static constexpr int kMinWindowWidth = 640;
static constexpr int kMinWindowHeight = 380;

struct Config {
    QString fontFamily = QStringLiteral("Microsoft YaHei UI");
    int fontSize = 12;
    bool bold = true;   // 默认加粗：12pt 在透明层上偏软，加粗后更清晰
    bool italic = false;
    QString customColor = QStringLiteral("#00ff88");
    QString theme = QStringLiteral("霓虹");
    bool showTray = true;
    bool showOverlay = true;
    OverlayAnchor anchor = OverlayAnchor::Right;
    UnitMode unit = UnitMode::Auto;
    FullscreenAction fullscreen = FullscreenAction::Hide;
    bool autoStart = false;

    bool hasWindowGeometry = false;
    QRect windowGeometry;

    static Config defaults();
    // 把外部来源（配置文件、旧版本、手工编辑）的值收敛到合法范围。
    void clamp();
};

QString anchorToString(OverlayAnchor anchor);
OverlayAnchor anchorFromString(const QString& text, OverlayAnchor fallback = OverlayAnchor::Right);
QString fullscreenToString(FullscreenAction action);
FullscreenAction fullscreenFromString(const QString& text,
                                      FullscreenAction fallback = FullscreenAction::Hide);

struct ConfigLoadResult {
    Config config;
    bool fileMissing = false;
    bool parseError = false;
    QString error;
};

class ConfigStore {
public:
    explicit ConfigStore(QString filePath);

    QString filePath() const { return m_path; }

    // 读取失败时返回默认配置；parseError 为真表示文件内容不可用。
    ConfigLoadResult load() const;

    // 原子写入（QSaveFile）；失败时通过 error 返回原因。
    bool save(const Config& config, QString* error = nullptr) const;

private:
    QString m_path;
};

}  // namespace nsf
