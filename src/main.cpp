#include "mainwindow.h"

#include <KAboutData>
#include <KLocalizedString>

#include <QApplication>
#include <QLocalServer>
#include <QLocalSocket>
#include <QWindow>

#include <unistd.h>

// 单实例 socket 名，按用户区分，避免多用户同时登录时冲突。
// 当第二次启动（例如从 KDE 系统设置的 KCM 自动唤起）连接到该 socket 时，
// 正在运行的实例会把自己的窗口提到前台，而不是再开一个重复窗口。
static QString instanceSocketName()
{
    return QStringLiteral("miryu-toolkit-%1").arg(::getuid());
}

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);
    KLocalizedString::setApplicationDomain("miryu-toolkit");

    KAboutData aboutData(QStringLiteral("miryu-toolkit"),
                         i18n("Miryu Toolkit"),
                         QStringLiteral("0.1.0"),
                         i18n("Miryu system utility toolkit"),
                         KAboutLicense::GPL_V3,
                         i18n("(c) 2026 Miryu"));
    aboutData.setDesktopFileName(QStringLiteral("miryu-toolkit"));
    KAboutData::setApplicationData(aboutData);

    // 单实例守卫：若已有实例在运行，则通知它把自己的窗口置顶，然后立即退出，
    // 不再打开第二个窗口（KCM 自动唤起时由这里拦截）。
    const QString socketName = instanceSocketName();
    QLocalSocket probe;
    probe.connectToServer(socketName);
    if (probe.waitForConnected(200)) {
        probe.disconnectFromServer();
        return 0;
    }

    // 第一个实例：占用 socket，后续启动者连进来时把本窗口提到前台。
    QLocalServer::removeServer(socketName);
    QLocalServer server;
    server.listen(socketName);

    MainWindow window;
    window.show();

    QObject::connect(&server, &QLocalServer::newConnection, &server, [&window, &server]() {
        // 取走挂起的连接，连接事件本身就是“把已有窗口提到前台”的信号。
        if (QLocalSocket *s = server.nextPendingConnection())
            s->deleteLater();
        window.showNormal();
        window.raise();
        window.activateWindow();
        if (QWindow *w = window.windowHandle())
            w->requestActivate();
    });

    return app.exec();
}
