// NetSpeedFlux — 程序入口
#include <QApplication>
#include <QIcon>
#include <QString>

#include "app.h"
#include "app_paths.h"
#include "logger.h"
#include "single_instance.h"
#include "version.h"

int main(int argc, char* argv[]) {
    // 必须在 QApplication 构造之前设置。
    QApplication::setHighDpiScaleFactorRoundingPolicy(
        Qt::HighDpiScaleFactorRoundingPolicy::PassThrough);

    QApplication application(argc, argv);
    QApplication::setOrganizationName(QStringLiteral("XAF"));
    QApplication::setApplicationName(QStringLiteral("NetSpeedFlux"));
    QApplication::setApplicationVersion(QStringLiteral(NETSPEEDFLUX_VERSION));
    QApplication::setApplicationDisplayName(QStringLiteral("NetSpeedFlux"));
    QApplication::setWindowIcon(QIcon(QStringLiteral(":/app.ico")));
    // 托盘常驻程序：最后一个窗口隐藏不等于退出。
    QApplication::setQuitOnLastWindowClosed(false);

    // 单实例判断要在创建任何窗口之前完成。
    nsf::SingleInstance singleInstance;
    if (!singleInstance.tryAcquire()) {
        singleInstance.notifyExisting();
        return 0;
    }

    nsf::AppPaths::instance().init(QCoreApplication::applicationDirPath());
    nsf::Log::init(nsf::AppPaths::instance().logDir());

    bool startMinimized = false;
    for (int index = 1; index < argc; ++index) {
        const QString argument = QString::fromLocal8Bit(argv[index]);
        if (argument == QLatin1String("--min") || argument == QLatin1String("-m")) {
            startMinimized = true;
        }
    }

    nsf::App app;
    QObject::connect(&singleInstance, &nsf::SingleInstance::activationRequested, &app,
                     &nsf::App::activate);
    app.init();

    if (startMinimized) {
        app.showTrayOnly();
    } else {
        app.showMainWindow();
    }

    return application.exec();
}
