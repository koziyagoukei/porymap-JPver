#include "visualeventeditor.h"
#include "eventframes.h"
#include "visualeventtools.h"
#include "pokemonevent.h"
#include "pokemon_event_gui_checks.h"
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
    write("data/maps/map_groups.json", "{\"group_order\":[\"gTest\"],\"gTest\":[\"Demo\"]}");
    write("data/maps/Demo/map.json", "{\"id\":\"MAP_DEMO\",\"name\":\"Demo\",\"layout\":\"LAYOUT_DEMO\",\"object_events\":[],\"warp_events\":[],\"coord_events\":[],\"bg_events\":[]}");
    write("data/layouts/layouts.json", "{\"layouts_table_label\":\"gMapLayouts\",\"layouts\":[{\"id\":\"LAYOUT_DEMO\",\"name\":\"Demo\",\"width\":20,\"height\":15,\"primary_tileset\":\"gTileset_General\",\"secondary_tileset\":\"gTileset_Petalburg\",\"border_filepath\":\"data/layouts/Demo/border.bin\",\"blockdata_filepath\":\"data/layouts/Demo/map.bin\"}]}");
    write("include/constants/flags.h", "#define FLAG_TEMP_1 1\n#define FLAG_TEMP_2 2\n#define FLAG_VISUAL_TEST 123\n");
    write("include/constants/vars.h", "enum { VAR_RESULT = 0x800D, VAR_TEST_NEXT };\n");
    write("include/constants/items.h", "#define ITEM_POTION (ITEM_BASE + 1)\n");
    write("include/constants/songs.h", "enum { SE_SELECT = 5, MUS_LITTLEROOT = 101 };\n");
    write("charmap.txt",QString::fromUtf8("'こ' = 01\n'ん' = 02\n'に' = 03\n'ち' = 04\n'は' = 05\n'!' = 06\n'\\n' = FE\n").toUtf8());
    QByteArray macros;
    for(const auto &s:QStringList{"end","lockall","releaseall","msgbox","checkflag","setflag","goto_if_eq","goto","applymovement","waitmovement","warp","waitstate","removeobject","giveitem","compare","goto_if_ne","goto_if_lt","goto_if_le","goto_if_gt","goto_if_ge"}) macros+=".macro "+s.toUtf8()+"\n.endm\n";
    write("asm/macros/event.inc",macros);
    write("asm/macros/movement.inc", "create_movement_action walk_down, 1\ncreate_movement_action walk_fast_right, 2\ncreate_movement_action face_up, 3\ncreate_movement_action delay_8, 4\ncreate_movement_action step_end, 5\n");
    projectConfig.reset(BaseGameVersion::pokeemerald);projectConfig.setRoot(temp.path());
    Project project;project.setRoot(temp.path());project.flagNames={"FLAG_TEMP_1","FLAG_TEMP_2","FLAG_VISUAL_TEST"};project.varNames={"VAR_RESULT","VAR_TEMP_1"};project.itemNames={"ITEM_POTION"};
    require(project.readMapLayouts() && project.readMapGroups(), "load fixture project map index and layouts");
    Map &map = *project.getMap("Demo");
    // Register the map without loading unrelated tileset assets in this UI fixture.
    map.setIsPersistedToFile(false); require(project.loadMap("Demo") == &map, "map registered with main save path");
    map.setIsPersistedToFile(true); map.setLayout(project.getLayout("LAYOUT_DEMO"));
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
    QFile stagedFile(map.getScriptsFilepath()); require(stagedFile.open(QIODevice::ReadOnly) && !stagedFile.readAll().contains("PORYMAP_VISUAL_BEGIN"), "apply does not write scripts early"); stagedFile.close();
    VisualEventEditor pendingEditor(&project,&map,label); require(pendingEditor.commandDocument().commands.first().text==QString::fromUtf8("こんにちは!"),"staged event reopens before map save");
    require(project.saveMap(&map, true), "main save commits staged script with map reference");
    require(!map.hasUnsavedChanges(), "successful main save marks map clean");
    QFile mapJson(map.getJsonFilepath()); require(mapJson.open(QIODevice::ReadOnly),"saved map JSON readable");
    require(QJsonDocument::fromJson(mapJson.readAll()).object()["object_events"].toArray().first().toObject()["script"]==label,"main save writes actual NPC script reference"); mapJson.close();
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
    // Each command must enter the document history and undo/redo without losing fields.
    VisualEventEditor options(&project,&map,label,nullptr,true);
    auto library=options.findChild<QListWidget *>("visualCommandLibrary");auto commands=options.findChild<QListWidget *>("visualCommands");auto history=options.findChild<QUndoStack *>();
    auto select=[&](const QString &id){for(int i=0;i<library->count();++i) if(library->item(i)->data(Qt::UserRole)==id) library->setCurrentRow(i);};
    for(const auto &spec:VisualEvent::commandSpecs()) {
        if (VisualEvent::isPokemonCommand(spec.id)) continue; // Dedicated engine-backed checks cover these commands.
        const auto before=options.commandDocument().toJson();select(spec.id);commands->setCurrentRow(commands->count()-1);
        options.findChild<QPushButton *>("visualAddCommand")->click();const auto after=options.commandDocument().toJson();
        require(after!=before,"each command changes GUI document");history->undo();require(options.commandDocument().toJson()==before,"each command undo");history->redo();require(options.commandDocument().toJson()==after,"each command redo");history->undo();
    }
    select("movement");commands->setCurrentRow(commands->count()-1);options.findChild<QPushButton *>("visualAddCommand")->click();
    const int movementRow=commands->currentRow(); const auto oldRoute=options.commandDocument().toJson();
    QTimer::singleShot(0,[&] {
        auto d=qobject_cast<QDialog *>(QApplication::activeModalWidget());require(d&&d->objectName()=="visualMovementEditor","movement GUI opens");
        auto direction=d->findChild<QComboBox *>("movementDirection");direction->setCurrentIndex(direction->findData("right"));
        auto speed=d->findChild<QComboBox *>("movementSpeed");speed->setCurrentIndex(speed->findData("fast"));
        d->findChild<QSpinBox *>("movementCount")->setValue(2);d->findChild<QPushButton *>("movementUpdate")->click();
        auto action=d->findChild<QComboBox *>("movementAction");action->setCurrentIndex(action->findData("face"));direction->setCurrentIndex(direction->findData("up"));d->findChild<QSpinBox *>("movementCount")->setValue(1);d->findChild<QPushButton *>("movementAdd")->click();
        action->setCurrentIndex(action->findData("wait"));d->findChild<QSpinBox *>("movementCount")->setValue(8);d->findChild<QPushButton *>("movementAdd")->click();
        d->findChild<QPushButton *>("movementUp")->click();d->findChild<QPushButton *>("movementDown")->click();
        auto target=d->findChild<QComboBox *>("movementTarget");target->setCurrentIndex(target->findData("LOCALID_PLAYER"));
        capture(*d,"smoke-results/visual-movement-route.png");d->findChild<QDialogButtonBox *>()->button(QDialogButtonBox::Ok)->click();
    });
    options.findChild<QPushButton *>("visualEditMovement")->click();const auto newRoute=options.commandDocument().toJson();
    require(oldRoute!=newRoute,"route settings reach command");history->undo();require(options.commandDocument().toJson()==oldRoute,"route undo restores full parameters");history->redo();require(options.commandDocument().toJson()==newRoute,"route redo restores full parameters");
    require(VisualEvent::movementActions(options.commandDocument().commands[movementRow])==QStringList({"walk_fast_right","walk_fast_right","face_up","delay_8"}),"movement direction facing speed wait and target");
    select("warp");commands->setCurrentRow(commands->count()-1);options.findChild<QPushButton *>("visualAddCommand")->click();const auto oldWarp=options.commandDocument().toJson();
    QTimer::singleShot(0,[&] {auto d=qobject_cast<QDialog *>(QApplication::activeModalWidget());require(d&&d->objectName()=="visualWarpEditor","warp GUI opens");d->findChild<QSpinBox *>("warpX")->setValue(6);d->findChild<QSpinBox *>("warpY")->setValue(7);capture(*d,"smoke-results/visual-warp.png");d->findChild<QDialogButtonBox *>()->button(QDialogButtonBox::Ok)->click();});
    options.findChild<QPushButton *>("visualEditWarp")->click();const auto newWarp=options.commandDocument().toJson();history->undo();require(options.commandDocument().toJson()==oldWarp,"warp undo");history->redo();require(options.commandDocument().toJson()==newWarp,"warp redo");
    for(const auto &id:QStringList{"item","once","disappear"}) {
        const auto before=options.commandDocument().toJson();
        QTimer::singleShot(0,[&,id] {auto d=qobject_cast<QDialog *>(QApplication::activeModalWidget());require(d&&d->objectName()=="visualTemplatePicker","template GUI opens");auto type=d->findChild<QComboBox *>("templateType");type->setCurrentIndex(type->findData(id));d->findChild<QComboBox *>("templateFlag")->setCurrentText("FLAG_VISUAL_TEST");d->findChild<QPlainTextEdit *>("templateText")->setPlainText(QString::fromUtf8("こんにちは!"));d->findChild<QDialogButtonBox *>()->button(QDialogButtonBox::Ok)->click();});
        options.findChild<QPushButton *>("visualTemplates")->click();const auto after=options.commandDocument().toJson();require(after!=before,"template insertion");history->undo();require(options.commandDocument().toJson()==before,"template undo includes NPC flag");history->redo();require(options.commandDocument().toJson()==after,"template redo includes NPC flag");
    }
    QTimer::singleShot(0,[&] {auto d=qobject_cast<QDialog *>(QApplication::activeModalWidget());require(d&&d->objectName()=="visualConstantPicker","constant picker opens");d->findChild<QLineEdit *>("constantSearch")->setText("VISUAL");auto table=d->findChild<QTableView *>("constantResults");require(table->model()->rowCount()==1,"constant filter searches project definitions");table->selectRow(0);capture(*d,"smoke-results/visual-constants.png");d->findChild<QDialogButtonBox *>()->button(QDialogButtonBox::Ok)->click();});
    require(VisualEventTools::chooseConstant(&project,VisualEvent::Field::Flag,"",&options)=="FLAG_VISUAL_TEST","constant selection returns unmodified identifier");
    const QList<QPair<VisualEvent::Field,QString>> constantCases{{VisualEvent::Field::Variable,"VAR_RESULT"},{VisualEvent::Field::Item,"ITEM_POTION"},{VisualEvent::Field::Sound,"SE_SELECT"},{VisualEvent::Field::Music,"MUS_LITTLEROOT"}};
    for (const auto &test : constantCases) {
        QTimer::singleShot(0,[&,test] {auto d=qobject_cast<QDialog *>(QApplication::activeModalWidget());require(d&&d->objectName()=="visualConstantPicker","all constant categories open");d->findChild<QLineEdit *>("constantSearch")->setText(test.second);auto table=d->findChild<QTableView *>("constantResults");require(table->model()->rowCount()==1,"define and enum constant search");if(test.first==VisualEvent::Field::Item) require(table->model()->index(0,1).data().toString()=="(ITEM_BASE + 1)","unresolved macro expression remains intact");table->selectRow(0);d->findChild<QDialogButtonBox *>()->button(QDialogButtonBox::Ok)->click();});
        require(VisualEventTools::chooseConstant(&project,test.first,"",&options)==test.second,"all constant categories preserve identifiers");
    }
    QTimer::singleShot(0,[&] {
        auto editor=qobject_cast<VisualEventEditor *>(QApplication::activeModalWidget());require(editor,"NPC template integration editor");
        QTimer::singleShot(0,[&] {auto d=qobject_cast<QDialog *>(QApplication::activeModalWidget());auto type=d->findChild<QComboBox *>("templateType");type->setCurrentIndex(type->findData("disappear"));d->findChild<QComboBox *>("templateFlag")->setCurrentText("FLAG_VISUAL_TEST");d->findChild<QPlainTextEdit *>("templateText")->setPlainText(QString::fromUtf8("こんにちは!"));capture(*d,"smoke-results/visual-template.png");d->findChild<QDialogButtonBox *>()->button(QDialogButtonBox::Ok)->click();});
        editor->findChild<QPushButton *>("visualTemplates")->click();
        QTimer::singleShot(0,[] {auto review=qobject_cast<QDialog *>(QApplication::activeModalWidget());require(review,"template review");review->accept();});
        editor->findChild<QPushButton *>("visualSave")->click();
    });
    launch->click();require(object->getFlag()=="FLAG_VISUAL_TEST","template applies persistent flag through NPC frame");
    require(project.saveMap(&map,true),"main save commits template and NPC flag together");
    require(mapJson.open(QIODevice::ReadOnly),"saved NPC flag map");require(QJsonDocument::fromJson(mapJson.readAll()).object()["object_events"].toArray().first().toObject()["flag"]=="FLAG_VISUAL_TEST","NPC template flag persists in original JSON schema");mapJson.close();
    // Sign and coordinate trigger references travel through the same main save.
    QString error;VisualEvent::ScriptFile signSource,triggerSource;
    require(signSource.open(temp.path(),map.getScriptsFilepath(),"Demo_Sign",{map.getScriptsFilepath()},false),"sign script source");
    require(triggerSource.open(temp.path(),map.getScriptsFilepath(),"Demo_Trigger",{map.getScriptsFilepath()},false),"trigger script source");
    require(project.stageVisualScript(&map,signSource,signSource.document,&error)&&project.stageVisualScript(&map,triggerSource,triggerSource.document,&error),"multiple pending event sources");
    sign->setScriptLabel("Demo_Sign");trigger->setScriptLabel("Demo_Trigger");
    require(project.saveMap(&map,true),"main save merges multiple labels in same source");
    require(mapJson.open(QIODevice::ReadOnly),"saved sign trigger map");auto savedMap=QJsonDocument::fromJson(mapJson.readAll()).object();mapJson.close();
    require(savedMap["bg_events"].toArray().first().toObject()["script"]=="Demo_Sign"&&savedMap["coord_events"].toArray().first().toObject()["script"]=="Demo_Trigger","sign and trigger Script references persist");
    // External source/map changes reject save while preserving the pending edit buffer.
    VisualEvent::ScriptFile stagedSource;require(stagedSource.open(temp.path(),map.getScriptsFilepath(),label,{map.getScriptsFilepath()},false),"open conflict fixture");
    auto changed=stagedSource.document;changed.commands[0].text=QString::fromUtf8("こんにちは");require(project.stageVisualScript(&map,stagedSource,changed,&error),"stage conflict fixture");
    require(savedFile.open(QIODevice::ReadOnly),"read source before conflict");auto beforeConflict=savedFile.readAll();savedFile.close();
    write("data/maps/Demo/scripts.inc",beforeConflict+"@ external modification\n");
    require(!project.saveMap(&map,true)&&map.hasUnsavedChanges(),"external source blocks main save and keeps dirty state");
    VisualEvent::Document pending;require(project.pendingVisualScript(label,&stagedSource,&pending)&&pending.toJson()==changed.toJson(),"failed save retains complete pending edits");
    write("data/maps/Demo/scripts.inc",beforeConflict);require(project.saveMap(&map,true),"main save retries preserved edits after source conflict resolved");
    require(stagedSource.open(temp.path(),map.getScriptsFilepath(),label,{map.getScriptsFilepath()},false),"refresh source snapshot for map conflict");
    require(project.stageVisualScript(&map,stagedSource,changed,&error),"stage external map fixture");
    require(mapJson.open(QIODevice::ReadOnly),"map conflict original");const auto beforeMapConflict=mapJson.readAll();mapJson.close();write("data/maps/Demo/map.json",beforeMapConflict+"\n ");
    require(!project.saveMap(&map,true)&&map.hasUnsavedChanges(),"external map modification rejects save");
    write("data/maps/Demo/map.json",beforeMapConflict);
    // Serialize/load through the existing event implementation, without a new map schema.
    ObjectEvent restored;require(restored.loadFromJson(QJsonDocument::fromJson(poryjson::Json(json).dump().toUtf8()).object(),&project),"event JSON reload");
    require(restored.getScript()==label,"reloaded event points to generated script");
    pokemonEventGuiChecks(project,map,temp.path(),object,launch);
    QFile report("smoke-results/visual-event-gui.txt");require(report.open(QIODevice::WriteOnly),"GUI report");
    report.write("PASS: all commands undo/redo; movement GUI; warp GUI; all templates and NPC flags undo/redo; project constant search; staged reopen; main map save with NPC/sign/trigger references; multiple labels in one file; source and map external-change rejection; failed save keeps pending edits; cancellation and unknown syntax protection; Japanese UI.\n");
}
