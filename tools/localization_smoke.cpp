#include <QApplication>
#include <QTranslator>
#include <QDir>
#include <QComboBox>
#include <QPushButton>
#include <QDialogButtonBox>
#include <QFile>
#include <QFontDatabase>
#include <cstdlib>
#include <QTextStream>
#include "ui_mainwindow.h"
#include "preferenceeditor.h"
#include "projectsettingseditor.h"
#include "regionmappropertiesdialog.h"
#include "neweventtoolbutton.h"
#include "events.h"

static void require(bool ok, const char *message) {
    if (!ok) {
        QFile failure("smoke-results/failure.txt");
        failure.open(QIODevice::WriteOnly);
        failure.write(message);
        failure.close();
        std::exit(1);
    }
}
static void capture(QWidget &widget, const QString &path) {
    widget.show();
    QApplication::processEvents();
    require(widget.grab().save(path), "Cannot save screenshot");
    widget.hide();
}
int main(int argc, char **argv) {
    QApplication app(argc, argv);
    QFontDatabase::addApplicationFont("C:/Windows/Fonts/meiryo.ttc");
    app.setFont(QFont("Meiryo", 9));
    app.setStyle("fusion");
    QTranslator qt, ja;
    require(qt.load(":/translations/qtbase_ja.qm"), "Missing Qt Japanese catalog");
    require(ja.load(":/translations/porymap_ja.qm"), "Missing Porymap Japanese catalog");
    app.installTranslator(&qt);
    app.installTranslator(&ja);
    QDir().mkpath("smoke-results");
    QFile::remove("smoke-results/failure.txt");
    QFile::remove("smoke-results/roundtrip.txt");
    QMainWindow main;
    Ui::MainWindow ui;
    ui.setupUi(&main);
    require(ui.menuFile->title().contains(QString::fromUtf8("ファイル")), "File menu is not Japanese");
    capture(main, "smoke-results/main.png");
    PreferenceEditor preferences;
    require(preferences.windowTitle() == QString::fromUtf8("環境設定"), "Preferences title untranslated");
    capture(preferences, "smoke-results/preferences.png");
    ProjectSettingsEditor settings(nullptr, nullptr);
    capture(settings, "smoke-results/project-settings.png");
    RegionMapPropertiesDialog region;
    // Display translation must never leak into serialized configuration.
    for (const QString &tileFormat : {QString("plain"), QString("4bpp"), QString("8bpp")}) {
        for (const QString &layoutFormat : {QString("C array"), QString("binary")}) {
            poryjson::Json input = poryjson::Json::object{
                {"alias", "TestRegion"},
                {"tilemap", poryjson::Json::object{{"format", tileFormat}, {"width", 32}, {"height", 32}}},
                {"layout", poryjson::Json::object{{"format", layoutFormat}, {"width", 16}, {"height", 16}}}
            };
            region.setProperties(input);
            auto output = region.saveToJson();
            QFile trace("smoke-results/roundtrip.txt");
            trace.open(QIODevice::WriteOnly | QIODevice::Append);
            trace.write((input.dump() + "\n" + output.dump() + "\n").toUtf8());
            trace.close();
            require(output["tilemap"]["format"].string_value() == tileFormat, "Tilemap format changed on round trip");
            require(output["layout"]["format"].string_value() == layoutFormat, "Layout format changed on round trip");
        }
    }
    capture(region, "smoke-results/region-properties.png");
    require(Event::typeToJsonKey(Event::Type::HiddenItem) == "hidden_item", "Event JSON key changed");
    require(Event::typeToString(Event::Type::HiddenItem) == QString::fromUtf8("隠しアイテム"), "Event label untranslated");
    QDialogButtonBox buttons(QDialogButtonBox::Save | QDialogButtonBox::Cancel);
    require(buttons.button(QDialogButtonBox::Cancel)->text().contains(QString::fromUtf8("キャンセル")), "Qt standard button untranslated");
    QFile result("smoke-results/result.txt");
    require(result.open(QIODevice::WriteOnly), "Cannot save test report");
    result.write("PASS: embedded catalogs, Japanese menus/events/buttons, all 6 region-map format round trips, unchanged event JSON keys.\n");
    return 0;
}
