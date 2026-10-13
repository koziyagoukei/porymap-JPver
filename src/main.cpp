#include "mainwindow.h"
#include "loadingscreen.h"

#include <QApplication>
#include <QTranslator>

int main(int argc, char *argv[])
{
    QGuiApplication::setHighDpiScaleFactorRoundingPolicy(Qt::HighDpiScaleFactorRoundingPolicy::Round);
    QCoreApplication::setAttribute(Qt::AA_UseStyleSheetPropagationInWidgetStyles, true);

    QApplication a(argc, argv);
    QTranslator qtJapanese;
    if (qtJapanese.load(QStringLiteral(":/translations/qtbase_ja.qm")))
        a.installTranslator(&qtJapanese);
    QTranslator japanese;
    if (japanese.load(QStringLiteral(":/translations/porymap_ja.qm")))
        a.installTranslator(&japanese);
    a.setStyle("fusion");

    porysplash = new PorymapLoadingScreen;

    QObject::connect(&a, &QCoreApplication::aboutToQuit, [=]() { delete porysplash; });

    MainWindow w(nullptr);
    w.initialize();

    return a.exec();
}
