#include "config.h"

#include "theme.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QRegularExpression>
#include <QSaveFile>

#include <utility>

namespace nsm {
namespace {

constexpr int kConfigVersion = 2;

const QString kDefaultTheme = QStringLiteral("霓虹");

bool isHexColor(const QString& text) {
    static const QRegularExpression pattern(QStringLiteral("^#[0-9a-fA-F]{6}([0-9a-fA-F]{2})?$"));
    return pattern.match(text).hasMatch();
}

}  // namespace

Config Config::defaults() {
    Config config;
    config.windowGeometry = QRect(0, 0, 720, 400);
    return config;
}

void Config::clamp() {
    fontSize = qBound(8, fontSize, 48);

    fontFamily = fontFamily.trimmed();
    if (fontFamily.isEmpty()) fontFamily = defaults().fontFamily;

    if (theme != QStringLiteral("自定") && !isBuiltinTheme(theme)) theme = kDefaultTheme;

    customColor = customColor.trimmed();
    if (!isHexColor(customColor)) customColor = defaults().customColor;

    if (!hasWindowGeometry) {
        windowGeometry = defaults().windowGeometry;
    } else {
        if (windowGeometry.width() <= 0 || windowGeometry.height() <= 0) {
            windowGeometry = defaults().windowGeometry;
        } else {
            windowGeometry.setWidth(qMax(windowGeometry.width(), kMinWindowWidth));
            windowGeometry.setHeight(qMax(windowGeometry.height(), kMinWindowHeight));
        }
    }
}

QString anchorToString(OverlayAnchor anchor) {
    switch (anchor) {
    case OverlayAnchor::Left: return QStringLiteral("left");
    case OverlayAnchor::Center: return QStringLiteral("center");
    case OverlayAnchor::Right:
    default: return QStringLiteral("right");
    }
}

OverlayAnchor anchorFromString(const QString& text, OverlayAnchor fallback) {
    if (text == QLatin1String("left")) return OverlayAnchor::Left;
    if (text == QLatin1String("center")) return OverlayAnchor::Center;
    if (text == QLatin1String("right")) return OverlayAnchor::Right;
    return fallback;
}

QString fullscreenToString(FullscreenAction action) {
    return action == FullscreenAction::Show ? QStringLiteral("show") : QStringLiteral("hide");
}

FullscreenAction fullscreenFromString(const QString& text, FullscreenAction fallback) {
    if (text == QLatin1String("show")) return FullscreenAction::Show;
    if (text == QLatin1String("hide")) return FullscreenAction::Hide;
    return fallback;
}

ConfigStore::ConfigStore(QString filePath) : m_path(std::move(filePath)) {}

ConfigLoadResult ConfigStore::load() const {
    ConfigLoadResult result;
    result.config = Config::defaults();

    QFile file(m_path);
    if (!file.exists()) {
        result.fileMissing = true;
        return result;
    }
    if (!file.open(QIODevice::ReadOnly)) {
        result.error = QStringLiteral("无法读取配置文件 %1：%2").arg(m_path, file.errorString());
        return result;
    }

    const QByteArray payload = file.readAll();
    file.close();
    if (payload.trimmed().isEmpty()) {
        result.parseError = true;
        result.error = QStringLiteral("配置文件为空：%1").arg(m_path);
        return result;
    }

    QJsonParseError parseError{};
    const QJsonDocument document = QJsonDocument::fromJson(payload, &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isObject()) {
        result.parseError = true;
        result.error = QStringLiteral("配置文件格式错误（%1）：%2")
                           .arg(parseError.errorString(), m_path);
        return result;
    }

    const QJsonObject object = document.object();
    Config config = Config::defaults();

    config.fontFamily = object.value(QStringLiteral("ff")).toString(config.fontFamily);
    config.fontSize = object.value(QStringLiteral("fs")).toInt(config.fontSize);
    config.bold = object.value(QStringLiteral("fb")).toBool(config.bold);
    config.italic = object.value(QStringLiteral("fi")).toBool(config.italic);
    config.customColor = object.value(QStringLiteral("fc")).toString(config.customColor);
    config.theme = object.value(QStringLiteral("theme")).toString(config.theme);
    config.showTray = object.value(QStringLiteral("sti")).toBool(config.showTray);
    config.showOverlay = object.value(QStringLiteral("sto")).toBool(config.showOverlay);
    config.anchor = anchorFromString(object.value(QStringLiteral("tp")).toString(),
                                     config.anchor);
    config.unit = unitModeFromString(object.value(QStringLiteral("um")).toString(), config.unit);
    config.fullscreen = fullscreenFromString(object.value(QStringLiteral("fsc")).toString(),
                                             config.fullscreen);
    config.autoStart = object.value(QStringLiteral("as")).toBool(config.autoStart);

    // 旧版本的几何信息是四个独立键 wx/wy/ww/wh。
    const int wx = object.value(QStringLiteral("wx")).toInt(-1);
    const int wy = object.value(QStringLiteral("wy")).toInt(-1);
    const int ww = object.value(QStringLiteral("ww")).toInt(0);
    const int wh = object.value(QStringLiteral("wh")).toInt(0);
    if (wx != -1 || wy != -1 || ww > 0 || wh > 0) {
        config.hasWindowGeometry = true;
        config.windowGeometry = QRect(wx, wy, ww > 0 ? ww : 720, wh > 0 ? wh : 400);
    }

    config.clamp();
    result.config = config;
    return result;
}

bool ConfigStore::save(const Config& config, QString* error) const {
    const QFileInfo info(m_path);
    const QDir dir = info.absoluteDir();
    if (!dir.exists() && !dir.mkpath(QStringLiteral("."))) {
        if (error) *error = QStringLiteral("无法创建配置目录：%1").arg(dir.absolutePath());
        return false;
    }

    QJsonObject object;
    object[QStringLiteral("cfgVersion")] = kConfigVersion;
    object[QStringLiteral("ff")] = config.fontFamily;
    object[QStringLiteral("fs")] = config.fontSize;
    object[QStringLiteral("fb")] = config.bold;
    object[QStringLiteral("fi")] = config.italic;
    object[QStringLiteral("fc")] = config.customColor;
    object[QStringLiteral("theme")] = config.theme;
    object[QStringLiteral("sti")] = config.showTray;
    object[QStringLiteral("sto")] = config.showOverlay;
    object[QStringLiteral("tp")] = anchorToString(config.anchor);
    object[QStringLiteral("um")] = unitModeToString(config.unit);
    object[QStringLiteral("fsc")] = fullscreenToString(config.fullscreen);
    object[QStringLiteral("as")] = config.autoStart;

    // 继续写旧键，保证降级回旧版本时位置还能恢复。
    // 没有几何信息时写 -1/0 作为哨兵，避免下次启动把窗口按 (0,0) 还原。
    const QRect geometry = config.hasWindowGeometry ? config.windowGeometry
                                                     : QRect(-1, -1, 0, 0);
    object[QStringLiteral("wx")] = geometry.x();
    object[QStringLiteral("wy")] = geometry.y();
    object[QStringLiteral("ww")] = geometry.width();
    object[QStringLiteral("wh")] = geometry.height();

    QSaveFile file(m_path);
    if (!file.open(QIODevice::WriteOnly)) {
        if (error) *error = QStringLiteral("无法写入配置文件 %1：%2").arg(m_path, file.errorString());
        return false;
    }
    const QByteArray payload = QJsonDocument(object).toJson(QJsonDocument::Indented);
    if (file.write(payload) != payload.size()) {
        if (error) *error = QStringLiteral("写入配置文件失败：%1").arg(file.errorString());
        file.cancelWriting();
        return false;
    }
    if (!file.commit()) {
        if (error) *error = QStringLiteral("提交配置文件失败：%1").arg(file.errorString());
        return false;
    }
    return true;
}

}  // namespace nsm
