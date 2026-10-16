#include "pokemoneventdialog.h"
#include "visualeventtools.h"
#include "project.h"
#include <QtWidgets>

namespace PokemonEventDialog {
using namespace VisualEvent;
class Text { Q_DECLARE_TR_FUNCTIONS(PokemonEventDialog) };
bool edit(Project *project, Map *, Command *command, QJsonObject *npcSettings, bool isNpc, QWidget *parent, bool requireFlag) {
    if(command->id=="trainerpokemon"&&!isNpc){QMessageBox::warning(parent,Text::tr("Pokemon Event Settings"),Text::tr("Trainer battles require an NPC event so the engine can identify the trainer object."));return false;}
    const auto engine = PokemonEngine::inspect(project->root);
    const auto unavailable = engine.unavailable(command->id);
    if (!unavailable.isEmpty()) { QMessageBox::warning(parent, Text::tr("Pokemon Event Settings"), unavailable); return false; }
    QDialog dialog(parent); dialog.setObjectName("pokemonEventDialog"); dialog.setWindowTitle(commandName(command->id)); dialog.resize(820, 720);
    auto layout = new QVBoxLayout(&dialog); auto tabs = new QTabWidget; layout->addWidget(tabs, 1);
    auto addTab = [&](const QString &name) { auto page = new QWidget; auto form = new QFormLayout(page); form->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow); tabs->addTab(page, name); return form; };
    auto basic = addTab(Text::tr("Basic Settings"));
    QMap<QString, std::function<QJsonValue()>> readers;
    const auto initial = command->options;
    auto stringField = [&](QFormLayout *form, const QString &key, const QString &label, Field field) {
        auto row = new QHBoxLayout; auto input = new QComboBox; input->setEditable(true); input->setObjectName("pokemon_" + key); input->setInsertPolicy(QComboBox::NoInsert);
        if (field == Field::Species) input->addItems(project->speciesNames);
        if (field == Field::Item) input->addItems(project->itemNames);
        if (field == Field::Flag) input->addItems(project->flagNames);
        input->setCurrentText(initial[key].toString()); row->addWidget(input, 1);
        auto search = new QPushButton(Text::tr("Search")); search->setObjectName("pokemon_search_" + key); row->addWidget(search);
        QObject::connect(search, &QPushButton::clicked, &dialog, [=, &dialog] { auto chosen = VisualEventTools::chooseConstant(project, field, input->currentText(), &dialog); if (!chosen.isEmpty()) input->setCurrentText(chosen); });
        form->addRow(label, row); readers[key] = [=] { return input->currentText().trimmed(); }; return input;
    };
    auto spin = [&](QFormLayout *form, const QString &key, const QString &label, int low, int high) {
        auto input = new QSpinBox; input->setObjectName("pokemon_" + key); input->setRange(low, high); input->setValue(initial[key].toInt()); form->addRow(label, input); readers[key] = [=] { return input->value(); }; return input;
    };
    auto check = [&](QFormLayout *form, const QString &key, const QString &label) {
        auto input = new QCheckBox(label); input->setObjectName("pokemon_" + key); input->setChecked(initial[key].toBool()); form->addRow(input); readers[key] = [=] { return input->isChecked(); }; return input;
    };
    auto dialogue = [&](QFormLayout *form, const QString &key, const QString &label) {
        auto input = new QPlainTextEdit(initial[key].toString()); input->setObjectName("pokemon_" + key); input->setMinimumHeight(65); input->setMaximumHeight(110); form->addRow(label, input); readers[key] = [=] { return input->toPlainText(); }; return input;
    };
    auto choice = [&](QFormLayout *form, const QString &key, const QString &label, const QList<QPair<QString,QString>> &items) {
        auto input = new QComboBox; input->setObjectName("pokemon_" + key); for (const auto &item : items) input->addItem(item.first, item.second);
        input->setCurrentIndex(qMax(0, input->findData(initial[key].toString()))); form->addRow(label, input); readers[key] = [=] { return input->currentData().toString(); }; return input;
    };
    auto resultField = [&] { choice(basic, "result", Text::tr("Result Variable"), {{"VAR_0x8008","VAR_0x8008"},{"VAR_0x8009","VAR_0x8009"},{"VAR_0x800A","VAR_0x800A"},{"VAR_0x800B","VAR_0x800B"}}); };
    QComboBox *species = nullptr;
    if (initial.contains("species")) species = stringField(basic, "species", Text::tr("Pokemon Species"), Field::Species);
    if (initial.contains("level")) spin(basic, "level", Text::tr("Level"), 1, 100);
    if (initial.contains("item")) stringField(basic, "item", Text::tr("Held Item"), Field::Item);
    if (initial.contains("trainer")) stringField(basic, "trainer", Text::tr("Trainer ID"), Field::Trainer);
    if (initial.contains("result")) resultField();
    QCheckBox *sight = nullptr; QSpinBox *range = nullptr;
    if (command->id == "wildpokemon") {
        dialogue(basic, "before", Text::tr("Message Before Battle"));
        auto after = addTab(Text::tr("After Battle")); dialogue(after, "win", Text::tr("After Winning")); dialogue(after, "caught", Text::tr("After Catching")); dialogue(after, "escaped", Text::tr("After Running Away"));
        auto note = new QLabel(Text::tr("The result variable receives B_OUTCOME_* from GetBattleOutcome. Add If Variable for further processing. A total defeat normally causes a whiteout, so this event cannot run a defeat branch.")); note->setWordWrap(true); after->addRow(note);
    } else if (command->id == "trainerpokemon") {
        check(basic, "double", Text::tr("Double Battle")); auto rematch = check(basic, "rematch", Text::tr("Use Official Rematch Rules"));
        dialogue(basic, "intro", Text::tr("Trainer's Intro Dialogue")); dialogue(basic, "defeat", Text::tr("Trainer's Defeat Dialogue"));
        auto after = addTab(Text::tr("After Battle")); dialogue(after, "insufficient", Text::tr("Not Enough Usable Pokemon")); auto victory = dialogue(after, "victory", Text::tr("First Victory Dialogue")); dialogue(after, "beaten", Text::tr("Already Defeated / Rematch Dialogue"));
        QObject::connect(rematch, &QCheckBox::toggled, &dialog, [=](bool on) { victory->setEnabled(!on); }); victory->setEnabled(!rematch->isChecked());
        auto npc = addTab(Text::tr("NPC Sight Detection")); sight = new QCheckBox(Text::tr("Start Automatically When the Player Enters Sight")); sight->setObjectName("pokemon_npcSight"); sight->setEnabled(isNpc && project->trainerTypes.contains("TRAINER_TYPE_NORMAL")); sight->setChecked(npcSettings->value("type").toString() == "TRAINER_TYPE_NORMAL"); npc->addRow(sight);
        range = new QSpinBox; range->setObjectName("pokemon_npcRange"); range->setRange(1,16); range->setValue(npcSettings->value("sight").toInt(3)); npc->addRow(Text::tr("Sight Range (Tiles)"), range);
        auto note = new QLabel(Text::tr("Trainer Battle must be first. The engine handles locking, defeated flags, usable-party checks and whiteouts. Result: 1 = first victory, 0 = already defeated, 2 = official rematch flow. Rematches need the project's rematch table.")); note->setWordWrap(true); npc->addRow(note);
    } else if (command->id == "healpokemon") {
        check(basic, "center", Text::tr("Pokemon Center Style Fade and Healing Fanfare")); dialogue(basic, "message", Text::tr("Message After Healing"));
        auto note = new QLabel(Text::tr("HealPlayerParty restores HP, PP and status using the engine. The center-style effect uses a fade and fanfare; it does not require a healing-machine object. Individual healing is unavailable without a verified engine helper.")); note->setWordWrap(true); basic->addRow(note);
    } else if (command->id == "giftpokemon" || command->id == "eggPokemon") {
        stringField(basic, "flag", requireFlag ? Text::tr("Required Unused Persistent Flag") : Text::tr("Success Flag (Optional, Once Only)"), Field::Flag);
        auto messages = addTab(Text::tr("Messages and Results")); dialogue(messages, "message", Text::tr("Message on Successful Receipt")); dialogue(messages, "failure", Text::tr("Message When Party and PC Are Full"));
        if (command->id == "giftpokemon") {
            auto nickname = dialogue(messages, "nickname", Text::tr("Ask About Nickname (Blank to Skip)")); nickname->setEnabled(engine.nickname);
            if (!engine.nickname) nickname->setToolTip(Text::tr("A safe party/PC nickname implementation was not found."));
        }
        auto note = new QLabel(Text::tr("Result: 0 = party, 1 = PC, 2 = cannot receive, 3 = already received. The success flag is set only after receipt. Existing party slots are never overwritten.")); note->setWordWrap(true); messages->addRow(note);
        if (command->id == "giftpokemon" && engine.giftDialect == "expanded") {
            auto details = addTab(Text::tr("Creation Details")); auto stats = addTab(Text::tr("IVs and EVs")); auto moves = addTab(Text::tr("Moves"));
            const auto saved = initial["details"].toObject(); QMap<QString,QComboBox *> fields; QMap<QString,QSpinBox *> numbers;
            auto detail = [&](const QString &key, const QString &label, Field kind) {
                auto row = new QHBoxLayout; auto input = new QComboBox; input->setEditable(true); input->setObjectName("pokemon_detail_" + key); input->addItem(Text::tr("Engine Default"), ""); input->setCurrentText(saved.value(key).toString()); row->addWidget(input, 1);
                auto search = new QPushButton(Text::tr("Search")); row->addWidget(search); QObject::connect(search, &QPushButton::clicked, &dialog, [=,&dialog] { const auto value = VisualEventTools::chooseConstant(project, kind, input->currentText(), &dialog); if(!value.isEmpty()) input->setCurrentText(value); }); details->addRow(label, row); fields[key] = input;
            };
            detail("ball", Text::tr("Poke Ball"), Field::Ball); detail("nature", Text::tr("Nature"), Field::Nature); detail("teraType", Text::tr("Tera Type"), Field::Type);
            auto ability = new QComboBox; ability->setObjectName("pokemon_detail_abilityNum"); ability->addItem(Text::tr("Engine Default"), ""); for (int i=0;i<3;++i) ability->addItem(Text::tr("Ability Slot %1").arg(i+1), QString::number(i)); ability->setCurrentIndex(qMax(0,ability->findData(saved.value("abilityNum").toString()))); details->addRow(Text::tr("Ability Slot"),ability); fields["abilityNum"]=ability;
            auto abilitySearch = new QPushButton(Text::tr("Search Species Abilities")); details->addRow(abilitySearch);
            QObject::connect(abilitySearch,&QPushButton::clicked,&dialog,[=,&dialog] { const auto value=VisualEventTools::chooseConstant(project,Field::Ability,"",&dialog);if(value.isEmpty()) return;const auto info=speciesDetails(project->root,species->currentText());const int slot=info.abilities.indexOf(value);if(slot<0) QMessageBox::warning(&dialog,Text::tr("Pokemon Event Settings"),Text::tr("This ability could not be verified for the selected species."));else ability->setCurrentIndex(slot+1); });
            auto gender = new QComboBox; gender->setObjectName("pokemon_detail_gender"); gender->addItem(Text::tr("Engine Default"), ""); gender->addItem(Text::tr("Male"), "MON_MALE"); gender->addItem(Text::tr("Female"), "MON_FEMALE"); gender->addItem(Text::tr("Genderless"), "MON_GENDERLESS");gender->setCurrentIndex(qMax(0,gender->findData(saved.value("gender").toString())));details->addRow(Text::tr("Gender"),gender);fields["gender"]=gender;
            auto shiny = new QComboBox;shiny->setObjectName("pokemon_detail_shinyMode");shiny->addItem(Text::tr("Engine Default / Random"), "");shiny->addItem(Text::tr("Always Shiny"),"1");shiny->addItem(Text::tr("Never Shiny"),"0");shiny->setCurrentIndex(qMax(0,shiny->findData(saved.value("shinyMode").toString())));details->addRow(Text::tr("Shininess"),shiny);fields["shinyMode"]=shiny;
            auto number = [&](QFormLayout *form,const QString &key,const QString &label,int max) {auto input=new QSpinBox;input->setObjectName("pokemon_detail_"+key);input->setRange(-1,max);input->setSpecialValueText(Text::tr("Engine Default"));input->setValue(saved.contains(key)?saved[key].toString().toInt():-1);form->addRow(label,input);numbers[key]=input;};
            number(details,"dmaxLevel",Text::tr("Dynamax Level"),10);number(details,"gmaxFactor",Text::tr("Gigantamax Factor (0 / 1)"),1);
            for(const auto &stat:QList<QPair<QString,QString>>{{"hp",Text::tr("HP")},{"atk",Text::tr("Attack")},{"def",Text::tr("Defense")},{"speed",Text::tr("Speed")},{"spAtk",Text::tr("Sp. Atk")},{"spDef",Text::tr("Sp. Def")}}) {number(stats,stat.first+"Iv",Text::tr("%1 IV").arg(stat.second),31);number(stats,stat.first+"Ev",Text::tr("%1 EV").arg(stat.second),252);}
            auto evNote=new QLabel(Text::tr("Blank/default IVs use engine generation. EVs are limited to 252 per stat and 510 total. Species ability slots, fixed genders and Gigantamax eligibility are checked when metadata is available."));evNote->setWordWrap(true);stats->addRow(evNote);
            for(int i=1;i<=4;++i) {const QString key="move"+QString::number(i);auto input=new QComboBox;input->setEditable(true);input->setObjectName("pokemon_detail_"+key);input->setCurrentText(saved.value(key).toString());auto row=new QHBoxLayout;row->addWidget(input,1);auto search=new QPushButton(Text::tr("Search"));row->addWidget(search);QObject::connect(search,&QPushButton::clicked,&dialog,[=,&dialog]{const auto value=VisualEventTools::chooseConstant(project,Field::Move,input->currentText(),&dialog);if(!value.isEmpty())input->setCurrentText(value);});moves->addRow(Text::tr("Move %1").arg(i),row);fields[key]=input;}
            readers["details"]=[=]{QJsonObject result;for(auto it=fields.begin();it!=fields.end();++it){const auto combo=it.value();QString value=combo->isEditable()?combo->currentText().trimmed():combo->currentData().toString();if(value==Text::tr("Engine Default"))value.clear();if(!value.isEmpty())result[it.key()]=value;}for(auto it=numbers.begin();it!=numbers.end();++it)if(it.value()->value()>=0)result[it.key()]=QString::number(it.value()->value());return result;};
        }
    } else if (command->id == "partypokemon") {
        choice(basic,"target",Text::tr("Target Selection"),{{Text::tr("First Party Pokemon"),"first"},{Text::tr("Specified Party Slot"),"slot"},{Text::tr("Let Player Choose"),"select"},{Text::tr("Find First Matching Species"),"find"}});
        auto slot=spin(basic,"slot",Text::tr("Party Position (1 to 6)"),1,6);slot->setValue(initial["slot"].toInt()+1);readers["slot"]=[=]{return slot->value()-1;};
        auto operation=choice(basic,"operation",Text::tr("Operation"),{{Text::tr("Read Species"),"species"},{Text::tr("Replace Move"),"move"},{Text::tr("Cure Status Condition"),"status"},{Text::tr("Change Tera Type"),"tera"}});
        if(!engine.partyStatus)qobject_cast<QStandardItemModel *>(operation->model())->item(operation->findData("status"))->setEnabled(false);
        if(!engine.partyTera)qobject_cast<QStandardItemModel *>(operation->model())->item(operation->findData("tera"))->setEnabled(false);
        for(const auto &id:QStringList{"status","tera"})operation->setItemData(operation->findData(id),Text::tr("Enabled only when this project's macro and C implementation are verified."),Qt::ToolTipRole);
        for(const auto &label:{QT_TRANSLATE_NOOP("PokemonEventDialog","Heal Selected Pokemon"),QT_TRANSLATE_NOOP("PokemonEventDialog","Change Held Item / Level / Nature"),QT_TRANSLATE_NOOP("PokemonEventDialog","Change IVs / EVs / Ability / Friendship")}) {operation->addItem(Text::tr(label));auto model=qobject_cast<QStandardItemModel *>(operation->model());model->item(operation->count()-1)->setEnabled(false);operation->setItemData(operation->count()-1,Text::tr("This operation needs a separately implemented game-side helper."),Qt::ToolTipRole);}
        stringField(basic,"move",Text::tr("New Move"),Field::Move);auto moveSlot=spin(basic,"moveSlot",Text::tr("Move Position (1 to 4)"),1,4);moveSlot->setValue(initial["moveSlot"].toInt()+1);readers["moveSlot"]=[=]{return moveSlot->value()-1;};
        stringField(basic,"teraType",Text::tr("Tera Type"),Field::Type);
        auto note=new QLabel(Text::tr("VAR_0x8009 receives the zero-based slot (6 = none/cancel/egg). Species reads return SPECIES_* or 0; other operations return 1 or 0. Missing targets and eggs are skipped. Unsupported changes require a separate game-side helper."));note->setWordWrap(true);basic->addRow(note);
    }
    auto box=new QDialogButtonBox(QDialogButtonBox::Ok|QDialogButtonBox::Cancel);layout->addWidget(box);
    QObject::connect(box,&QDialogButtonBox::rejected,&dialog,&QDialog::reject);
    QObject::connect(box,&QDialogButtonBox::accepted,&dialog,[&] {
        auto draft=*command;for(auto it=readers.begin();it!=readers.end();++it)draft.options[it.key()]=it.value()();
        if(draft.id=="giftpokemon") {draft.options["engine"]=engine.giftDialect;draft.options["waitNickname"]=!engine.automaticWait.contains("ChangePokemonNickname");}
        if(draft.id=="partypokemon")draft.options["waitSelect"]=!engine.automaticWait.contains("ChoosePartyMon");
        if(draft.id=="trainerpokemon"&&draft.options["rematch"].toBool())draft.options["victory"]="";
        QString error=validatePokemon(draft,engine);if(requireFlag&&draft.options["flag"].toString().isEmpty())error=Text::tr("This template requires an unused persistent flag.");
        if(!error.isEmpty()){QMessageBox::warning(&dialog,Text::tr("Pokemon Event Settings"),error);return;}
        if(sight&&sight->isEnabled())*npcSettings={{"type",sight->isChecked()?"TRAINER_TYPE_NORMAL":"TRAINER_TYPE_NONE"},{"sight",range->value()}};
        *command=draft;dialog.accept();
    });
    return dialog.exec()==QDialog::Accepted;
}
bool insertTemplate(Project *project,Map *map,Document *document,bool isNpc,QWidget *parent) {
    QDialog picker(parent);picker.setObjectName("pokemonTemplatePicker");picker.setWindowTitle(Text::tr("Pokemon Event Templates"));auto layout=new QVBoxLayout(&picker);auto list=new QComboBox;list->setObjectName("pokemonTemplateType");layout->addWidget(list);
    const auto engine=PokemonEngine::inspect(project->root);
    const QList<QPair<QString,QString>> entries{{Text::tr("Static Wild Battle"),"wildpokemon"},{Text::tr("Trainer Battle"),"trainerpokemon"},{Text::tr("Pokemon Center Style Healing"),"healpokemon"},{Text::tr("Receive Pokemon from NPC"),"giftpokemon"},{Text::tr("One-time Gift Pokemon"),"once"},{Text::tr("Receive Pokemon Egg"),"eggPokemon"},{Text::tr("Choose and Operate on Party Pokemon"),"partypokemon"}};
    for(const auto &entry:entries){list->addItem(entry.first,entry.second);auto reason=engine.unavailable(entry.second=="once"?"giftpokemon":entry.second);if(entry.second=="trainerpokemon"&&!isNpc)reason=Text::tr("Trainer battles require an NPC event so the engine can identify the trainer object.");if(!reason.isEmpty()){qobject_cast<QStandardItemModel *>(list->model())->item(list->count()-1)->setEnabled(false);list->setItemData(list->count()-1,reason,Qt::ToolTipRole);}}
    auto box=new QDialogButtonBox(QDialogButtonBox::Ok|QDialogButtonBox::Cancel);layout->addWidget(box);QObject::connect(box,&QDialogButtonBox::accepted,&picker,&QDialog::accept);QObject::connect(box,&QDialogButtonBox::rejected,&picker,&QDialog::reject);
    if(picker.exec()!=QDialog::Accepted)return false;
    const QString id=list->currentData().toString();auto command=pokemonCommand(id=="once"?"giftpokemon":id,&engine);if(id=="healpokemon")command.options["center"]=true;
    auto npc=document->npcTrainer;if(!edit(project,map,&command,&npc,isNpc,parent,id=="once"))return false;
    int row=document->commands.size();if(row&&document->commands.last().id=="end")--row;
    if(id=="trainerpokemon"){row=0;document->lockPlayer=false;document->npcTrainer=npc;}
    document->commands.insert(row++,command);
    auto condition=[&](const QString &value,const QString &comparison,bool otherwise=false){auto test=defaultCommand("ifvar");test.argument=command.options["result"].toString();test.value=value;test.comparison=comparison;document->commands.insert(row++,test);if(otherwise)document->commands.insert(row++,defaultCommand("else"));document->commands.insert(row++,defaultCommand("endif"));};
    if(id=="wildpokemon")for(const auto &outcome:QStringList{"B_OUTCOME_WON","B_OUTCOME_CAUGHT","B_OUTCOME_RAN"})condition(outcome,"==");
    if(id=="trainerpokemon"&&!command.options["rematch"].toBool())condition("1","==");
    if(id=="giftpokemon"||id=="once"||id=="eggPokemon")condition("1","<=",true);
    if(id=="partypokemon")condition("0","!=",true);
    return true;
}
}
