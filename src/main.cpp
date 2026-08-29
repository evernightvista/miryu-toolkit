#include "mainwindow.h"

#include <KAboutData>
#include <KLocalizedString>

#include <QApplication>

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

    MainWindow window;
    window.show();
    return app.exec();
}
