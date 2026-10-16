#include "visualeventtools.h"
#include "pokemonevent.h"
#include "project.h"
#include "config.h"
#include <QtWidgets>

namespace VisualEventTools {
using namespace VisualEvent;
class Text { Q_DECLARE_TR_FUNCTIONS(VisualEventTools) };
static QDialogButtonBox *buttons(QDialog &dialog, QBoxLayout *layout) {
    auto box = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    layout->addWidget(box);
    QObject::connect(box, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    QObject::connect(box, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    return box;
}
QString chooseConstant(Project *project, Field field, const QString &current, QWidget *parent) {
    QDialog dialog(parent); dialog.setObjectName("visualConstantPicker"); dialog.setWindowTitle(Text::tr("Search Project Constants")); dialog.resize(700, 500);
    auto layout = new QVBoxLayout(&dialog);
    auto search = new QLineEdit; search->setObjectName("constantSearch"); search->setPlaceholderText(Text::tr("Filter by name or value")); layout->addWidget(search);
    auto model = new QStandardItemModel(&dialog); model->setHorizontalHeaderLabels({Text::tr("Constant"), Text::tr("Value / Expression"), Text::tr("Definition File")});
    // Reuse Porymap's parser, configured paths, regular expressions and global defines.
    project->parser.clearFileCache();
    const auto fileId = field == Field::Species ? ProjectFilePath::constants_species : field == Field::Flag ? ProjectFilePath::constants_flags : field == Field::Variable || field == Field::Number ? ProjectFilePath::constants_vars
        : field == Field::Item ? ProjectFilePath::constants_items : ProjectFilePath::constants_songs;
    QString file = projectConfig.getFilePath(fileId);
    if(field==Field::Move)file="include/constants/moves.h";
    if(field==Field::Ability)file="include/constants/abilities.h";
    if(field==Field::Trainer)file="include/constants/opponents.h";
    if(field==Field::Trainer&&!QFileInfo::exists(project->root+"/"+file))file="include/constants/trainers.h";
    if(field==Field::Nature||field==Field::Ball)file=projectConfig.getFilePath(ProjectFilePath::constants_pokemon);
    if(field==Field::Ball&&QFileInfo::exists(project->root+"/include/constants/pokeball.h"))file="include/constants/pokeball.h";
    if(field==Field::Type)file="include/constants/pokemon.h";
    const QString pattern = field == Field::Species ? "^SPECIES_" : field==Field::Move ? "^MOVE_" : field==Field::Ability ? "^ABILITY_" : field==Field::Trainer ? "^TRAINER_" : field==Field::Nature ? "^NATURE_" : field==Field::Ball ? "^BALL_" : field==Field::Type ? "^TYPE_" : field == Field::Flag ? projectConfig.getIdentifier(ProjectIdentifier::regex_flags)
        : field == Field::Variable || field == Field::Number ? projectConfig.getIdentifier(ProjectIdentifier::regex_vars)
        : field == Field::Item ? projectConfig.getIdentifier(ProjectIdentifier::regex_items) : (field == Field::Sound ? "^SE_" : "^MUS_");
    QString error;
    // ParseUtil is noncopyable; use the project's parser with its existing define context.
    const auto values = project->parser.readCDefineExpressions(file, {pattern}, &error);
    auto names = values.keys(); names.sort(Qt::CaseInsensitive);
    if(field==Field::Type){names.removeAll("TYPE_NONE");names.removeAll("TYPE_MYSTERY");}
    if(field==Field::Trainer)names.removeAll("TRAINER_NONE");
    const auto displayNames=pokemonDisplayNames(project->root,field);
    if(!displayNames.isEmpty())model->setHorizontalHeaderLabels({Text::tr("Constant"),Text::tr("Value / Expression"),Text::tr("Definition File"),Text::tr("Display Name")});
    for (const auto &name : names) {
        QList<QStandardItem *> row{new QStandardItem(name),new QStandardItem(values[name]),new QStandardItem(file)};
        if(!displayNames.isEmpty())row.append(new QStandardItem(displayNames.value(name)));
        model->appendRow(row);
    }
    auto proxy = new QSortFilterProxyModel(&dialog); proxy->setSourceModel(model); proxy->setFilterCaseSensitivity(Qt::CaseInsensitive); proxy->setFilterKeyColumn(-1);
    auto table = new QTableView; table->setObjectName("constantResults"); table->setModel(proxy); table->setSelectionBehavior(QAbstractItemView::SelectRows); table->setSelectionMode(QAbstractItemView::SingleSelection);
    table->setEditTriggers(QAbstractItemView::NoEditTriggers); table->horizontalHeader()->setStretchLastSection(true); table->resizeColumnsToContents(); layout->addWidget(table, 1);
    if(!displayNames.isEmpty())table->horizontalHeader()->moveSection(3,0);
    auto note = new QLabel(error.isEmpty() ? Text::tr("Values are source expressions, not evaluated numbers. Only constant identifiers are inserted; the project's build tools resolve them.") : error); note->setWordWrap(true); layout->addWidget(note);
    auto box = buttons(dialog, layout); box->button(QDialogButtonBox::Ok)->setEnabled(false);
    QObject::connect(search, &QLineEdit::textChanged, proxy, &QSortFilterProxyModel::setFilterFixedString);
    QObject::connect(table->selectionModel(), &QItemSelectionModel::selectionChanged, &dialog, [=] { box->button(QDialogButtonBox::Ok)->setEnabled(table->currentIndex().isValid()); });
    QObject::connect(table, &QTableView::doubleClicked, &dialog, &QDialog::accept);
    search->setText(current); if (proxy->rowCount()) table->selectRow(0);
    if (dialog.exec() != QDialog::Accepted || !table->currentIndex().isValid()) return {};
    return proxy->index(table->currentIndex().row(), 0).data().toString();
}
static QComboBox *targets(Map *map) {
    auto combo = new QComboBox; combo->setEditable(true); combo->setObjectName("movementTarget");
    combo->addItem(Text::tr("Player"), "LOCALID_PLAYER"); combo->addItem(Text::tr("NPC being spoken to"), "VAR_LAST_TALKED");
    for (const auto event : map->getEvents(Event::Group::Object)) if (auto object = dynamic_cast<ObjectEvent *>(event)) {
        const auto id = object->getIdName().isEmpty() ? QString::number(map->getIndexOfEvent(object) + 1) : object->getIdName();
        combo->addItem(id, id);
    }
    return combo;
}
static void setTarget(QComboBox *combo, const QString &value) {
    const int index = combo->findData(value); if (index >= 0) combo->setCurrentIndex(index); else combo->setEditText(value);
}
static QString target(QComboBox *combo) {
    return combo->currentIndex() >= 0 && combo->itemText(combo->currentIndex()) == combo->currentText() ? combo->currentData().toString() : combo->currentText();
}
bool editMovement(Project *, Map *map, Command *command, QWidget *parent) {
    QDialog dialog(parent); dialog.setObjectName("visualMovementEditor"); dialog.setWindowTitle(Text::tr("Movement Route")); dialog.resize(630, 540);
    auto layout = new QVBoxLayout(&dialog); auto form = new QFormLayout;
    auto object = targets(map); setTarget(object, command->argument); form->addRow(Text::tr("Target"), object); layout->addLayout(form);
    auto list = new QListWidget; list->setObjectName("movementSteps"); layout->addWidget(list, 1);
    QJsonArray route = command->options["route"].toArray();
    auto refresh = [&] {
        const int row = list->currentRow(); list->clear();
        for (const auto &value : route) {
            const auto s = value.toObject();
            const QString action = s["action"].toString(), direction = s["direction"].toString(), speed = s["speed"].toString();
            const QString facing = direction == "down" ? Text::tr("Down") : direction == "up" ? Text::tr("Up") : direction == "left" ? Text::tr("Left") : Text::tr("Right");
            list->addItem(action == "wait" ? Text::tr("Wait %1 frames").arg(s["count"].toInt())
                : (action == "face" ? Text::tr("Face %1").arg(facing) : Text::tr("Walk %1 (%2)").arg(facing, speed == "slow" ? Text::tr("Slow") : speed == "fast" ? Text::tr("Fast") : Text::tr("Normal"))) + " × " + QString::number(s["count"].toInt()));
        }
        list->setCurrentRow(qBound(0, row, list->count() - 1));
    };
    auto fields = new QFormLayout;
    auto action = new QComboBox; action->setObjectName("movementAction"); action->addItem(Text::tr("Walk"), "walk"); action->addItem(Text::tr("Change Facing"), "face"); action->addItem(Text::tr("Wait"), "wait"); fields->addRow(Text::tr("Action"), action);
    auto direction = new QComboBox; direction->setObjectName("movementDirection");
    direction->addItem(Text::tr("Down"), "down"); direction->addItem(Text::tr("Up"), "up"); direction->addItem(Text::tr("Left"), "left"); direction->addItem(Text::tr("Right"), "right"); fields->addRow(Text::tr("Direction"), direction);
    auto speed = new QComboBox; speed->setObjectName("movementSpeed"); speed->addItem(Text::tr("Slow"), "slow"); speed->addItem(Text::tr("Normal"), "normal"); speed->addItem(Text::tr("Fast"), "fast"); fields->addRow(Text::tr("Speed"), speed); speed->setCurrentIndex(1);
    auto count = new QSpinBox; count->setObjectName("movementCount"); count->setRange(1, 64); fields->addRow(Text::tr("Steps / Wait Frames"), count); layout->addLayout(fields);
    auto rowButtons = new QHBoxLayout; layout->addLayout(rowButtons);
    auto addButton = [&](const char *text, const char *name, auto callback) { auto b = new QPushButton(Text::tr(text)); b->setObjectName(name); rowButtons->addWidget(b); QObject::connect(b, &QPushButton::clicked, &dialog, callback); };
    auto step = [&] { return QJsonObject{{"action", action->currentData().toString()}, {"direction", direction->currentData().toString()}, {"speed", speed->currentData().toString()}, {"count", count->value()}}; };
    addButton(QT_TRANSLATE_NOOP("VisualEventTools", "Add Step"), "movementAdd", [&] { if (route.size() < 256) { route.append(step()); refresh(); list->setCurrentRow(route.size() - 1); } });
    addButton(QT_TRANSLATE_NOOP("VisualEventTools", "Update Step"), "movementUpdate", [&] { if (list->currentRow() >= 0) { route[list->currentRow()] = step(); refresh(); } });
    addButton(QT_TRANSLATE_NOOP("VisualEventTools", "Delete Step"), "movementDelete", [&] { if (list->currentRow() >= 0) { route.removeAt(list->currentRow()); refresh(); } });
    for (int delta : {-1, 1}) addButton(delta < 0 ? QT_TRANSLATE_NOOP("VisualEventTools", "Move Up") : QT_TRANSLATE_NOOP("VisualEventTools", "Move Down"), delta < 0 ? "movementUp" : "movementDown", [&, delta] {
        const int row = list->currentRow(), next = row + delta; if (row >= 0 && next >= 0 && next < route.size()) { const auto value = route.takeAt(row); route.insert(next, value); refresh(); list->setCurrentRow(next); }
    });
    QObject::connect(list, &QListWidget::currentRowChanged, &dialog, [&](int row) {
        if (row < 0 || row >= route.size()) return;
        const auto s = route[row].toObject();
        action->setCurrentIndex(action->findData(s["action"].toString())); direction->setCurrentIndex(direction->findData(s["direction"].toString())); speed->setCurrentIndex(speed->findData(s["speed"].toString())); count->setValue(s["count"].toInt());
    });
    QObject::connect(action, &QComboBox::currentIndexChanged, &dialog, [&] { const auto id = action->currentData().toString(); direction->setEnabled(id != "wait"); speed->setEnabled(id == "walk"); });
    auto note = new QLabel(Text::tr("Movement completes before the next command. Wait frames: 1, 2, 4, 8 or 16. Blocked tiles can prevent movement; check the route in game.")); note->setWordWrap(true); layout->addWidget(note);
    buttons(dialog, layout); refresh(); list->setCurrentRow(0);
    // Validate before accepting while preserving the dialog's edit buffer on error.
    auto box = dialog.findChild<QDialogButtonBox *>(); QObject::disconnect(box, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    QObject::connect(box, &QDialogButtonBox::accepted, &dialog, [&] {
        auto draft = *command; draft.argument = target(object); draft.options = {{"route", route}};
        QString error; movementActions(draft, &error);
        if (!error.isEmpty()) { QMessageBox::warning(&dialog, Text::tr("Movement Route"), error); return; }
        *command = draft; dialog.accept();
    });
    return dialog.exec() == QDialog::Accepted;
}
bool editWarp(Project *project, Command *command, QWidget *parent) {
    QDialog dialog(parent); dialog.setObjectName("visualWarpEditor"); dialog.setWindowTitle(Text::tr("Transfer to Map")); dialog.resize(520, 260);
    auto layout = new QVBoxLayout(&dialog); auto form = new QFormLayout;
    const auto sizes = project->visualMapSizes();
    auto map = new QComboBox; map->setObjectName("warpMap");
    for (auto it = sizes.begin(); it != sizes.end(); ++it) if (!it.value().isEmpty()) map->addItem(project->mapConstantsToMapNames.value(it.key()) + " (" + it.key() + ")", it.key());
    form->addRow(Text::tr("Destination Map"), map);
    auto x = new QSpinBox; x->setObjectName("warpX"); auto y = new QSpinBox; y->setObjectName("warpY"); form->addRow(Text::tr("X Coordinate"), x); form->addRow(Text::tr("Y Coordinate"), y);
    auto update = [&] { const auto size = sizes.value(map->currentData().toString()); x->setRange(0, qMax(0, qMin(size.width(), 65536) - 1)); y->setRange(0, qMax(0, qMin(size.height(), 65536) - 1)); };
    QObject::connect(map, &QComboBox::currentIndexChanged, &dialog, update); map->setCurrentIndex(qMax(0, map->findData(command->argument))); update(); x->setValue(command->options["x"].toInt()); y->setValue(command->options["y"].toInt()); layout->addLayout(form);
    auto note = new QLabel(Text::tr("Coordinates start at 0. Map transfer ends this event. Choose a walkable tile and check collision and story conditions in game.")); note->setWordWrap(true); layout->addWidget(note);
    auto box = buttons(dialog, layout); box->button(QDialogButtonBox::Ok)->setEnabled(map->count() > 0);
    if (dialog.exec() != QDialog::Accepted) return false;
    command->argument = map->currentData().toString(); command->options = {{"x", x->value()}, {"y", y->value()}}; return true;
}
bool insertTemplate(Project *project, Map *map, Document *document, bool isNpc, QWidget *parent) {
    QDialog dialog(parent); dialog.setObjectName("visualTemplatePicker"); dialog.setWindowTitle(Text::tr("Event Templates")); dialog.resize(620, 480);
    auto layout = new QVBoxLayout(&dialog); auto form = new QFormLayout;
    auto type = new QComboBox; type->setObjectName("templateType"); type->addItem(Text::tr("Pick Up Item"), "item"); type->addItem(Text::tr("One-time Conversation"), "once"); type->addItem(Text::tr("NPC Disappears"), "disappear"); form->addRow(Text::tr("Template"), type);
    auto flag = new QComboBox; flag->setEditable(true); flag->addItems(project->flagNames); flag->setObjectName("templateFlag");
    auto item = new QComboBox; item->setEditable(true); item->addItems(project->itemNames); item->setCurrentText("ITEM_POTION"); item->setObjectName("templateItem");
    auto flagRow = new QHBoxLayout; flagRow->addWidget(flag); auto search = new QPushButton(Text::tr("Search Constants")); flagRow->addWidget(search); form->addRow(Text::tr("Persistent Flag (choose an unused flag)"), flagRow);
    QObject::connect(search, &QPushButton::clicked, &dialog, [&] { const auto value = chooseConstant(project, Field::Flag, flag->currentText(), &dialog); if (!value.isEmpty()) flag->setCurrentText(value); });
    form->addRow(Text::tr("Item"), item); auto object = targets(map); setTarget(object, "VAR_LAST_TALKED"); form->addRow(Text::tr("Target"), object); layout->addLayout(form);
    auto text = new QPlainTextEdit; text->setObjectName("templateText"); text->setPlaceholderText(Text::tr("Dialogue")); layout->addWidget(text, 1);
    auto persistent = new QCheckBox(Text::tr("Assign this flag to the selected NPC to keep it hidden on return")); persistent->setChecked(isNpc); persistent->setEnabled(isNpc); layout->addWidget(persistent);
    auto note = new QLabel(Text::tr("Commands are inserted before End Script and can be edited afterwards. Use a persistent, unused flag. Item pickup sets the flag only when the bag accepts the item.")); note->setWordWrap(true); layout->addWidget(note);
    QObject::connect(type, &QComboBox::currentIndexChanged, &dialog, [&] { const bool pickup = type->currentData() == "item"; item->setEnabled(pickup); text->setEnabled(!pickup); object->setEnabled(type->currentData() != "once"); persistent->setEnabled(isNpc && type->currentData() != "once"); });
    text->setEnabled(false); auto box = buttons(dialog, layout); QObject::disconnect(box, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    QObject::connect(box, &QDialogButtonBox::accepted, &dialog, [&] {
        const QString flagName = flag->currentText();
        if (!QRegularExpression("^FLAG_[A-Za-z_0-9]+$").match(flagName).hasMatch() || flagName.startsWith("FLAG_TEMP_")
            || (type->currentData() != "item" && text->toPlainText().isEmpty())
            || (persistent->isChecked() && persistent->isEnabled() && target(object) != "VAR_LAST_TALKED")) {
            QMessageBox::warning(&dialog, Text::tr("Event Templates"), Text::tr("Choose a persistent FLAG_ constant and enter dialogue when needed. Automatic NPC flag assignment requires the NPC being spoken to as the target.")); return;
        }
        auto commands = templateCommands(type->currentData().toString(), flagName, item->currentText(), text->toPlainText(), target(object));
        int row = document->commands.size(); if (row && document->commands.last().id == "end") --row;
        for (const auto &command : commands) document->commands.insert(row++, command);
        if (persistent->isEnabled() && persistent->isChecked()) document->objectFlag = flagName;
        dialog.accept();
    });
    return dialog.exec() == QDialog::Accepted;
}
}
