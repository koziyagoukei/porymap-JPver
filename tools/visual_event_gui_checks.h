#include "visualeventeditor.h"
#include "eventframes.h"
#include <QTemporaryDir>
#include <QTimer>
#include <QMessageBox>
#include <QUndoStack>

static void visualEventGuiChecks() {
    QTemporaryDir temp(QDir::currentPath()+"/visual-gui-XXXXXX");
    require(temp.isValid(),"GUI fixture directory");
    auto write=[&](QString path,QByteArray content){
        path=temp.path()+"/"+path;QDir().mkpath(QFileInfo(path).absolutePath());QFile f(path);
        require(f.open(QIODevice::WriteOnly)&&f.write(content)==content.size(),"GUI fixture write");
    };
    write("data/maps/Demo/scripts.inc","Demo_MapScripts::\n\t.byte 0\nUnknown::\n\tcustom_macro 1\n\tend\n");
    write("data/event_scripts.s","@ test scripts\n");
    write("charmap.txt",QString::fromUtf8("'こ' = 01\n'ん' = 02\n'に' = 03\n'ち' = 04\n'は' = 05\n'!' = 06\n'\\n' = FE\n").toUtf8());
    QByteArray macros;
    for(const auto &s:QStringList{"end","lockall","releaseall","msgbox","checkflag","goto_if_eq","goto"}) macros+=".macro "+s.toUtf8()+"\n.endm\n";
    write("asm/macros/event.inc",macros);
    projectConfig.reset(BaseGameVersion::pokeemerald);projectConfig.setRoot(temp.path());
    Project project;project.setRoot(temp.path());project.flagNames={"FLAG_TEMP_1","FLAG_TEMP_2"};project.varNames={"VAR_RESULT","VAR_TEMP_1"};
    Map map;map.setName("Demo");map.setIsPersistedToFile(true);
    // MainWindow::markMapEdited normally performs this when Map::modified fires.
    QObject::connect(&map,&Map::modified,[&]{map.setHasUnsavedDataChanges(true);});
    auto object=new ObjectEvent;map.addEvent(object);object->setScript("NULL");
    auto frame=static_cast<ObjectFrame *>(object->getEventFrame());frame->populate(&project);frame->connectSignals(nullptr);
    frame->combo_script->setTextItem("NULL");
    auto launch=frame->findChild<QPushButton *>("editVisualEventCommands");
    require(launch&&launch->text()==QString::fromUtf8("イベントコマンド編集"),"translated NPC launch button");
    QTimer watchdog;watchdog.setSingleShot(true);QObject::connect(&watchdog,&QTimer::timeout,[]{require(false,"GUI test timed out");});watchdog.start(15000);
    QTimer::singleShot(0,[&]{
        auto dialog=qobject_cast<VisualEventEditor *>(QApplication::activeModalWidget());require(dialog,"NPC button opened visual editor");
        auto library=dialog->findChild<QListWidget *>("visualCommandLibrary");auto list=dialog->findChild<QListWidget *>("visualCommands");
        auto add=dialog->findChild<QPushButton *>("visualAddCommand");auto text=dialog->findChild<QPlainTextEdit *>("visualDialogue");
        require(library&&list&&add&&text,"three-pane editor controls");
        auto select=[&](const QString &id){for(int i=0;i<library->count();++i) if(library->item(i)->data(Qt::UserRole)==id) library->setCurrentRow(i);};
        select("message");add->click();require(list->count()==2,"GUI add message");text->setPlainText(QString::fromUtf8("こんにちは!"));
        dialog->findChild<QPushButton *>("visualDuplicate")->click();require(list->count()==3,"GUI duplicate");
        dialog->findChild<QPushButton *>("visualDelete")->click();require(list->count()==2,"GUI delete");
        auto history=dialog->findChild<QUndoStack *>();history->undo();require(list->count()==3,"GUI undo");history->redo();require(list->count()==2,"GUI redo");
        require(list->model()->moveRow(QModelIndex(),0,QModelIndex(),2),"internal list move");
        QApplication::processEvents();history->undo();list->setCurrentRow(0);
        require(list->item(0)->text().contains(QString::fromUtf8("文章の表示")),"drag model order reaches undo stack");
        select("ifflag");add->click();require(list->count()==5,"GUI adds complete condition block");
        dialog->findChild<QPushButton *>("visualDown")->click();history->undo();require(list->count()==5,"GUI move undo");
        list->setCurrentRow(0);capture(*dialog,"smoke-results/visual-event-editor.png");
        QTimer::singleShot(0,[&]{auto review=qobject_cast<QDialog *>(QApplication::activeModalWidget());
            require(review&&review!=dialog&&review->findChild<QPlainTextEdit *>(),"save opens code review before mutation");
            capture(*review,"smoke-results/visual-event-preview.png");review->accept();});
        dialog->findChild<QPushButton *>("visualSave")->click();
    });
    launch->click();watchdog.stop();
    const QString label=object->getScript();require(label!="NULL"&&!label.isEmpty(),"successful save links NPC Script property");
    require(map.hasUnsavedChanges(),"script reference marks map dirty");
    const auto json=object->buildEventJson(&project);require(json.at("script").string_value()==label,"event JSON serializes script reference");
    VisualEvent::ScriptFile source;require(source.open(temp.path(),map.getScriptsFilepath(),label,{map.getScriptsFilepath()},false),"GUI-created source can reopen");
    require(source.document.commands.first().text==QString::fromUtf8("こんにちは!"),"GUI Japanese text round trip");
    QFile savedFile(map.getScriptsFilepath());require(savedFile.open(QIODevice::ReadOnly),"saved script readable");const auto savedBytes=savedFile.readAll();savedFile.close();
    watchdog.start(15000);
    QTimer::singleShot(0,[&]{
        auto dialog=qobject_cast<VisualEventEditor *>(QApplication::activeModalWidget());require(dialog,"reopen saved GUI event");
        auto list=dialog->findChild<QListWidget *>("visualCommands");list->setCurrentRow(0);
        dialog->findChild<QPlainTextEdit *>("visualDialogue")->setPlainText(QString::fromUtf8("こんにちは"));
        QTimer::singleShot(0,[]{auto question=qobject_cast<QMessageBox *>(QApplication::activeModalWidget());require(question,"cancel confirms dirty buffer");question->button(QMessageBox::Discard)->click();});
        QMetaObject::invokeMethod(dialog,"reject",Qt::DirectConnection);
    });
    launch->click();watchdog.stop();
    require(savedFile.open(QIODevice::ReadOnly)&&savedFile.readAll()==savedBytes,"cancel preserves original file bytes");savedFile.close();
    require(object->getScript()==label,"cancel preserves Script reference");
    VisualEventEditor readOnly(&project,&map,"Unknown");
    require(!readOnly.findChild<QPushButton *>("visualSave")->isEnabled(),"unknown script editor cannot save");
    auto sign=new SignEvent;map.addEvent(sign);require(sign->getEventFrame()->findChild<QPushButton *>("editVisualEventCommands"),"sign launch button");
    auto trigger=new TriggerEvent;map.addEvent(trigger);require(trigger->getEventFrame()->findChild<QPushButton *>("editVisualEventCommands"),"coordinate trigger launch button");
    // Serialize/load through the existing event implementation, without a new map schema.
    ObjectEvent restored;require(restored.loadFromJson(QJsonDocument::fromJson(poryjson::Json(json).dump().toUtf8()).object(),&project),"event JSON reload");
    require(restored.getScript()==label,"reloaded event points to generated script");
    QFile report("smoke-results/visual-event-gui.txt");require(report.open(QIODevice::WriteOnly),"GUI report");
    report.write("PASS: NPC button -> GUI add/edit/duplicate/delete/move/undo/redo -> code review -> save -> Script reference -> event JSON reload; Japanese dialogue round trip; cancel byte preservation; unknown-script read-only; NPC/sign/trigger buttons.\n");
}
