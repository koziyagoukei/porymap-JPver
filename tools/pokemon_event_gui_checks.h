#include "pokemoneventdialog.h"
#include "../tests/pokemon_engine_fixture.h"
#include <QtWidgets>
#include <QTimer>

static void pokemonEventGuiChecks(Project &project,Map &map,const QString &root,ObjectEvent *object,QPushButton *launch) {
    auto write=[&](const QString &path,const QByteArray &data){const QString full=root+"/"+path;QDir().mkpath(QFileInfo(full).absolutePath());QFile file(full);require(file.open(QIODevice::WriteOnly)&&file.write(data)==data.size(),"Pokemon GUI fixture write");};
    QFile existing(root+"/asm/macros/event.inc");require(existing.open(QIODevice::ReadOnly),"Pokemon GUI macros readable");auto macros=existing.readAll();existing.close();
    seedPokemonEngineFixture(write,macros);
    write("include/constants/species.h",QString::fromUtf8("#define SPECIES_BULBASAUR 1\n#define SPECIES_POOCHYENA 261\n#define SPECIES_TOGEPI 175\n").toUtf8());
    write("include/constants/opponents.h","#define TRAINER_CALVIN_1 318\n");write("include/constants/moves.h","#define MOVE_TACKLE 33\n#define MOVE_GROWL 45\n");write("include/constants/abilities.h","#define ABILITY_OVERGROW 65\n#define ABILITY_CHLOROPHYLL 34\n");write("include/constants/pokeball.h","enum { BALL_POKE = 1, BALL_GREAT = 2 };\n");
    write("src/data/pokemon/species_info/gen_1_families.h",QString::fromUtf8("[SPECIES_BULBASAUR] = { .genderRatio = PERCENT_FEMALE(12.5), .abilities = {ABILITY_OVERGROW, ABILITY_NONE, ABILITY_CHLOROPHYLL}, .speciesName = _(\"フシギダネ\"), },\n").toUtf8());
    project.speciesNames={"SPECIES_BULBASAUR","SPECIES_POOCHYENA","SPECIES_TOGEPI"};project.trainerTypes={"TRAINER_TYPE_NONE","TRAINER_TYPE_NORMAL"};
    QTimer watchdog;watchdog.setSingleShot(true);QObject::connect(&watchdog,&QTimer::timeout,[]{require(false,"Pokemon GUI test timed out");});watchdog.start(60000);
    auto fill=[&](QDialog *dialog,bool captureImage,bool finish=true) {
        require(dialog&&dialog->objectName()=="pokemonEventDialog","Pokemon settings dialog opens");
        for(const auto &key:QStringList{"before","win","caught","escaped","intro","defeat","insufficient","victory","beaten","message","failure","nickname"})if(auto text=dialog->findChild<QPlainTextEdit *>("pokemon_"+key))if(text->isEnabled())text->setPlainText(QString::fromUtf8("こんにちは!"));
        if(auto level=dialog->findChild<QSpinBox *>("pokemon_level"))level->setValue(12);
        if(auto flag=dialog->findChild<QComboBox *>("pokemon_flag"))flag->setCurrentText("FLAG_VISUAL_TEST");
        if(auto center=dialog->findChild<QCheckBox *>("pokemon_center"))center->setChecked(true);
        if(auto doubles=dialog->findChild<QCheckBox *>("pokemon_double"))doubles->setChecked(true);
        if(auto sight=dialog->findChild<QCheckBox *>("pokemon_npcSight")){sight->setChecked(true);dialog->findChild<QSpinBox *>("pokemon_npcRange")->setValue(5);}
        if(auto result=dialog->findChild<QComboBox *>("pokemon_result"))result->setCurrentIndex(result->findData("VAR_0x800A"));
        if(auto ability=dialog->findChild<QComboBox *>("pokemon_detail_abilityNum")){ability->setCurrentIndex(ability->findData("2"));dialog->findChild<QSpinBox *>("pokemon_detail_hpIv")->setValue(31);dialog->findChild<QSpinBox *>("pokemon_detail_spAtkEv")->setValue(252);dialog->findChild<QSpinBox *>("pokemon_detail_speedEv")->setValue(252);}
        if(auto operation=dialog->findChild<QComboBox *>("pokemon_operation")){operation->setCurrentIndex(operation->findData("move"));dialog->findChild<QSpinBox *>("pokemon_moveSlot")->setValue(2);}
        if(captureImage){
            auto tabs=dialog->findChild<QTabWidget *>();capture(*dialog,"smoke-results/pokemon-settings.png");
            if(dialog->findChild<QComboBox *>("pokemon_detail_abilityNum")){tabs->setCurrentIndex(2);capture(*dialog,"smoke-results/pokemon-creation-details.png");tabs->setCurrentIndex(3);capture(*dialog,"smoke-results/pokemon-ivs-evs.png");}
        }
        if(finish)dialog->findChild<QDialogButtonBox *>()->button(QDialogButtonBox::Ok)->click();
    };
    VisualEventEditor editor(&project,&map,"",nullptr,true);auto library=editor.findChild<QListWidget *>("visualCommandLibrary");auto list=editor.findChild<QListWidget *>("visualCommands");auto history=editor.findChild<QUndoStack *>();
    auto select=[&](const QString &id){for(int i=0;i<library->count();++i)if(library->item(i)->data(Qt::UserRole)==id){require(library->item(i)->flags()&Qt::ItemIsEnabled,"verified Pokemon command enabled");library->setCurrentRow(i);}};
    for(const auto &id:QStringList{"wildpokemon","trainerpokemon","trainerstatus","healpokemon","giftpokemon","eggPokemon","partypokemon"}){
        const auto before=editor.commandDocument().toJson();select(id);list->setCurrentRow(list->count()-1);editor.findChild<QPushButton *>("visualAddCommand")->click();const auto added=editor.commandDocument().toJson();
        history->undo();require(editor.commandDocument().toJson()==before,"Pokemon command add undo");history->redo();require(editor.commandDocument().toJson()==added,"Pokemon command add redo");
        QTimer::singleShot(0,[&]{fill(qobject_cast<QDialog *>(QApplication::activeModalWidget()),id=="giftpokemon");});editor.findChild<QPushButton *>("visualEditPokemon")->click();const auto configured=editor.commandDocument().toJson();require(configured!=added,"Pokemon settings reach document");
        history->undo();require(editor.commandDocument().toJson()==added,"Pokemon settings and NPC metadata undo");history->redo();require(editor.commandDocument().toJson()==configured,"Pokemon settings and NPC metadata redo");
        if(id=="trainerpokemon"){editor.findChild<QPushButton *>("visualDelete")->click();require(editor.commandDocument().npcTrainer["type"]=="TRAINER_TYPE_NONE","deleting trainer disables generated sight settings");history->undo();require(editor.commandDocument().toJson()==configured,"trainer deletion undo restores sight settings");}
        history->undo();history->undo();require(editor.commandDocument().toJson()==before,"Pokemon GUI test restores prior document");
    }
    VisualEventEditor nonNpc(&project,&map,"",nullptr,false);auto nonNpcLibrary=nonNpc.findChild<QListWidget *>("visualCommandLibrary");for(int i=0;i<nonNpcLibrary->count();++i)if(nonNpcLibrary->item(i)->data(Qt::UserRole)=="trainerpokemon")require(!(nonNpcLibrary->item(i)->flags()&Qt::ItemIsEnabled)&&!nonNpcLibrary->item(i)->toolTip().isEmpty(),"non-NPC trainer battle disabled with reason");
    select("partypokemon");editor.findChild<QPushButton *>("visualAddCommand")->click();
    for(const auto &operation:QStringList{"status","tera"}){
        const auto before=editor.commandDocument().toJson();
        QTimer::singleShot(0,[&,operation]{auto d=qobject_cast<QDialog *>(QApplication::activeModalWidget());fill(d,false,false);auto combo=d->findChild<QComboBox *>("pokemon_operation");combo->setCurrentIndex(combo->findData(operation));d->findChild<QComboBox *>("pokemon_teraType")->setCurrentText("TYPE_GRASS");d->findChild<QDialogButtonBox *>()->button(QDialogButtonBox::Ok)->click();});
        editor.findChild<QPushButton *>("visualEditPokemon")->click();const auto after=editor.commandDocument().toJson();require(after!=before,"status/Tera operations configurable");history->undo();require(editor.commandDocument().toJson()==before,"status/Tera operation undo");history->redo();require(editor.commandDocument().toJson()==after,"status/Tera operation redo");
    }
    history->undo();history->undo();history->undo();
    for(const auto &id:QStringList{"wildpokemon","trainerpokemon","healpokemon","giftpokemon","once","eggPokemon","partypokemon"}){
        const auto before=editor.commandDocument().toJson();
        QTimer::singleShot(0,[&,id]{auto picker=qobject_cast<QDialog *>(QApplication::activeModalWidget());require(picker&&picker->objectName()=="pokemonTemplatePicker","Pokemon template picker opens");auto type=picker->findChild<QComboBox *>("pokemonTemplateType");type->setCurrentIndex(type->findData(id));QTimer::singleShot(0,[&]{fill(qobject_cast<QDialog *>(QApplication::activeModalWidget()),false);});picker->findChild<QDialogButtonBox *>()->button(QDialogButtonBox::Ok)->click();});
        editor.findChild<QPushButton *>("visualPokemonTemplates")->click();const auto after=editor.commandDocument().toJson();require(after!=before,"all Pokemon templates insert editable commands");history->undo();require(editor.commandDocument().toJson()==before,"Pokemon template undo");history->redo();require(editor.commandDocument().toJson()==after,"Pokemon template redo");history->undo();
    }
    QTimer::singleShot(0,[&]{auto picker=qobject_cast<QDialog *>(QApplication::activeModalWidget());require(picker&&picker->objectName()=="visualConstantPicker","Pokemon species picker opens");picker->findChild<QLineEdit *>("constantSearch")->setText(QString::fromUtf8("フシギダネ"));auto table=picker->findChild<QTableView *>("constantResults");require(table->model()->rowCount()==1,"Japanese species names searchable");table->selectRow(0);capture(*picker,"smoke-results/pokemon-species-search.png");picker->findChild<QDialogButtonBox *>()->button(QDialogButtonBox::Ok)->click();});
    require(VisualEventTools::chooseConstant(&project,VisualEvent::Field::Species,"",&editor)=="SPECIES_BULBASAUR","Japanese species selection preserves constant identifier");
    const QList<QPair<VisualEvent::Field,QString>> lookups{{VisualEvent::Field::Move,"MOVE_TACKLE"},{VisualEvent::Field::Ability,"ABILITY_OVERGROW"},{VisualEvent::Field::Trainer,"TRAINER_CALVIN_1"},{VisualEvent::Field::Ball,"BALL_POKE"}};
    for(const auto &lookup:lookups){QTimer::singleShot(0,[&,lookup]{auto d=qobject_cast<QDialog *>(QApplication::activeModalWidget());d->findChild<QLineEdit *>("constantSearch")->setText(lookup.second);auto table=d->findChild<QTableView *>("constantResults");require(table->model()->rowCount()==1,"Pokemon constants use actual definition headers");table->selectRow(0);d->findChild<QDialogButtonBox *>()->button(QDialogButtonBox::Ok)->click();});require(VisualEventTools::chooseConstant(&project,lookup.first,"",&editor)==lookup.second,"Pokemon constant lookup returns identifier");}
    // Existing pending state is saved first; then exercise actual NPC frame integration.
    require(project.saveMap(&map,true),"pending baseline changes save before trainer integration");
    QTimer::singleShot(0,[&]{auto d=qobject_cast<VisualEventEditor *>(QApplication::activeModalWidget());require(d,"trainer NPC frame launch");auto commands=d->findChild<QListWidget *>("visualCommandLibrary");for(int i=0;i<commands->count();++i)if(commands->item(i)->data(Qt::UserRole)=="trainerpokemon")commands->setCurrentRow(i);d->findChild<QPushButton *>("visualAddCommand")->click();QTimer::singleShot(0,[&]{fill(qobject_cast<QDialog *>(QApplication::activeModalWidget()),false);});d->findChild<QPushButton *>("visualEditPokemon")->click();QTimer::singleShot(0,[]{auto review=qobject_cast<QDialog *>(QApplication::activeModalWidget());require(review,"trainer script review");review->accept();});d->findChild<QPushButton *>("visualSave")->click();});
    launch->click();require(object->getTrainerType()=="TRAINER_TYPE_NORMAL"&&object->getSightRadiusBerryTreeID()=="5","trainer type and sight range reach NPC");require(project.saveMap(&map,true),"trainer script and NPC settings save together");
    QFile file(map.getJsonFilepath());require(file.open(QIODevice::ReadOnly),"trainer map JSON readable");const auto data=QJsonDocument::fromJson(file.readAll()).object()["object_events"].toArray().first().toObject();file.close();require(data["trainer_type"]=="TRAINER_TYPE_NORMAL"&&data["trainer_sight_or_berry_tree_id"]=="5","trainer NPC JSON retains native schema");
    VisualEvent::ScriptFile source;require(source.open(root,map.getScriptsFilepath(),object->getScript(),{map.getScriptsFilepath()},false),"trainer script reopens");QString error;const auto code=source.preview(source.document,&error);require(error.isEmpty()&&code.contains(object->getScript()+"::\n\ttrainerbattle_double"),"trainer battle is first native opcode for sight detection");
    watchdog.stop();QFile report("smoke-results/pokemon-event-gui.txt");require(report.open(QIODevice::WriteOnly),"Pokemon GUI report");report.write("PASS: seven Pokemon commands/settings undo-redo; seven templates undo-redo; status/Tera operations; extended creation UI; Japanese species search; move/ability/trainer/ball headers; NPC trainer type and sight; deletion clears sight; non-NPC trainer blocked; linked map/script save; first native trainer opcode.\n");
}
