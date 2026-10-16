#include "visualeventeditor.h"
#include "project.h"
#include "map.h"
#include "config.h"
#include "editor.h"

#include <QtWidgets>
#include <functional>

using namespace VisualEvent;
namespace {
class CommandListWidget : public QListWidget {
public:
    std::function<void()> orderChanged;
protected:
    void dropEvent(QDropEvent *event) override {
        QListWidget::dropEvent(event);
        if (event->isAccepted() && orderChanged) orderChanged();
    }
};
class DocumentChange : public QUndoCommand {
public:
    DocumentChange(Document before, Document after, QString key, std::function<void(const Document &)> apply)
        : before(before), after(after), key(key), apply(apply) { setText(QCoreApplication::translate("Porymap", "Edit Event Commands")); }
    int id() const override { return key.isEmpty() ? -1 : 1001; }
    bool mergeWith(const QUndoCommand *other) override {
        const auto next=dynamic_cast<const DocumentChange *>(other);
        if(!next||key!=next->key) return false;
        after=next->after;return true;
    }
    void undo() override {apply(before);}
    void redo() override {apply(after);}
private:
    Document before,after;QString key;std::function<void(const Document &)> apply;
};
int blockEnd(const Document &d,int row) {
    if(row<0||row>=d.commands.size()) return row;
    if(d.commands[row].id!="ifflag"&&d.commands[row].id!="ifvar") return row;
    int depth=0;
    for(int i=row;i<d.commands.size();++i) {
        const auto id=d.commands[i].id;
        if(id=="ifflag"||id=="ifvar") ++depth;
        if(id=="endif"&&--depth==0) return i;
    }
    return row;
}
}

VisualEventEditor::VisualEventEditor(Project *project,Map *map,const QString &label,QWidget *parent)
    :QDialog(parent),m_project(project) {
    setObjectName("visualEventEditor");
    setWindowTitle(tr("Visual Event Editor"));
    setWindowModality(Qt::ApplicationModal);
    resize(1120,740);
    auto outer=new QVBoxLayout(this);
    auto heading=new QFormLayout;
    m_label=new QLineEdit; m_label->setObjectName("visualScriptLabel");
    heading->addRow(tr("Script Label"),m_label);
    auto pathLabel=new QLabel;pathLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);
    heading->addRow(tr("Source File"),pathLabel);outer->addLayout(heading);
    m_status=new QLabel;m_status->setWordWrap(true);outer->addWidget(m_status);
    m_lock=new QCheckBox(tr("Lock player movement while this event runs"));outer->addWidget(m_lock);

    auto split=new QSplitter;outer->addWidget(split,1);
    auto libraryPane=new QWidget;auto libraryLayout=new QVBoxLayout(libraryPane);
    libraryLayout->addWidget(new QLabel(tr("Event Commands")));
    m_library=new QListWidget;m_library->setObjectName("visualCommandLibrary");libraryLayout->addWidget(m_library);
    for(const auto &spec:commandSpecs()) {auto item=new QListWidgetItem(commandName(spec.id),m_library);item->setData(Qt::UserRole,spec.id);}
    auto add=new QPushButton(tr("Add Command"));add->setObjectName("visualAddCommand");libraryLayout->addWidget(add);split->addWidget(libraryPane);
    auto commandPane=new QWidget;auto commandLayout=new QVBoxLayout(commandPane);
    commandLayout->addWidget(new QLabel(tr("Processing Order")));
    auto commandList=new CommandListWidget;m_commands=commandList;m_commands->setObjectName("visualCommands");
    m_commands->setDragDropMode(QAbstractItemView::InternalMove);m_commands->setDefaultDropAction(Qt::MoveAction);
    commandLayout->addWidget(m_commands,1);
    auto toolbar=new QHBoxLayout;
    auto button=[&](const char *text,const char *name){auto b=new QPushButton(tr(text));b->setObjectName(name);toolbar->addWidget(b);return b;};
    m_delete=button(QT_TRANSLATE_NOOP("VisualEventEditor", "Delete Command"),"visualDelete");
    m_duplicate=button(QT_TRANSLATE_NOOP("VisualEventEditor", "Duplicate Command"),"visualDuplicate");
    m_up=button(QT_TRANSLATE_NOOP("VisualEventEditor", "Move Up"),"visualUp");
    m_down=button(QT_TRANSLATE_NOOP("VisualEventEditor", "Move Down"),"visualDown");
    commandLayout->addLayout(toolbar);split->addWidget(commandPane);
    auto details=new QWidget;auto detailLayout=new QVBoxLayout(details);
    detailLayout->addWidget(new QLabel(tr("Command Settings")));
    m_firstLabel=new QLabel;m_argument=new QComboBox;m_argument->setEditable(true);m_argument->setInsertPolicy(QComboBox::NoInsert);m_argument->setObjectName("visualArgument");
    detailLayout->addWidget(m_firstLabel);detailLayout->addWidget(m_argument);
    m_comparison=new QComboBox;
    m_comparison->addItem(tr("Equal to"), "==");m_comparison->addItem(tr("Not equal to"), "!=");
    m_comparison->addItem(tr("Less than"), "<");m_comparison->addItem(tr("Greater than"), ">");
    m_comparison->addItem(tr("Less than or equal to"), "<=");m_comparison->addItem(tr("Greater than or equal to"), ">=");
    m_comparison->setObjectName("visualComparison");detailLayout->addWidget(m_comparison);
    m_secondLabel=new QLabel;m_value=new QComboBox;m_value->setEditable(true);m_value->setInsertPolicy(QComboBox::NoInsert);m_value->setObjectName("visualValue");
    detailLayout->addWidget(m_secondLabel);detailLayout->addWidget(m_value);
    m_text=new QPlainTextEdit;m_text->setObjectName("visualDialogue");detailLayout->addWidget(m_text,1);
    m_textHint=new QLabel(tr("Enter dialogue with line breaks. Use \\l and \\p to scroll or start a paragraph. Yes / No stores 1 / 0 in VAR_RESULT; branch with If Variable. Text is checked against the project's charmap.txt."));
    m_textHint->setWordWrap(true);detailLayout->addWidget(m_textHint);
    m_readOnlySource=new QPlainTextEdit;m_readOnlySource->setReadOnly(true);m_readOnlySource->setObjectName("visualReadOnlySource");detailLayout->addWidget(m_readOnlySource,1);
    detailLayout->addStretch();
    split->addWidget(details);split->setSizes({210,510,360});
    auto footer=new QHBoxLayout;outer->addLayout(footer);
    m_history=new QUndoStack(this);
    auto undo=m_history->createUndoAction(this,tr("Undo"));undo->setShortcut(QKeySequence::Undo);addAction(undo);
    auto redo=m_history->createRedoAction(this,tr("Redo"));redo->setShortcut(QKeySequence::Redo);addAction(redo);
    auto undoButton=new QPushButton(tr("Undo"));connect(undoButton,&QPushButton::clicked,m_history,&QUndoStack::undo);footer->addWidget(undoButton);
    auto redoButton=new QPushButton(tr("Redo"));connect(redoButton,&QPushButton::clicked,m_history,&QUndoStack::redo);footer->addWidget(redoButton);
    footer->addStretch();
    auto external=new QPushButton(tr("Open in External Editor"));footer->addWidget(external);
    auto previewButton=new QPushButton(tr("Preview Generated Script"));previewButton->setObjectName("visualPreview");footer->addWidget(previewButton);
    m_save=new QPushButton(tr("Review and Save"));m_save->setObjectName("visualSave");footer->addWidget(m_save);
    auto cancel=new QPushButton(tr("Cancel"));footer->addWidget(cancel);

    // Scan source files, including Poryscript even when autocomplete is restricted.
    QStringList paths=project->getAllEventScriptsFilepaths();
    QDirIterator it(project->root,{"*.inc","*.pory","*.s"},QDir::Files,QDirIterator::Subdirectories);
    while(it.hasNext()) {
        const QString path=it.next();const QString relative=QDir(project->root).relativeFilePath(path);
        if(!relative.startsWith(".git/")&&!relative.startsWith("build/")&&!relative.startsWith("tools/")
           &&!relative.startsWith("graphics/")&&!relative.startsWith("sound/")) paths.append(path);
    }
    const QString initial=(label=="NULL"||label=="0"||label=="0x0")?QString():label;
    m_file.open(project->root,map->getScriptsFilepath(),initial,paths,projectConfig.usePoryScript);
    if(!map->isPersistedToFile()) {m_status->setText(tr("Save this new map in Porymap before editing its script."));m_save->setEnabled(false);}
    else m_status->setText(m_file.isEditable()?tr("Edits stay in this window until you review and save. The map's script reference is saved with the map."):m_file.reason());
    const bool editable=m_file.isEditable()&&map->isPersistedToFile();
    libraryPane->setEnabled(editable);commandPane->setEnabled(editable);m_lock->setEnabled(editable);
    m_label->setReadOnly(!editable||(!m_file.document.commands.isEmpty()&&m_file.source().contains("PORYMAP_VISUAL_BEGIN "+m_file.document.label+" v1")));
    external->setEnabled(!m_file.isGeneratedSource()&&(!projectConfig.usePoryScript||m_file.isPoryscript()));
    m_save->setEnabled(editable);previewButton->setEnabled(editable);
    m_readOnlySource->setVisible(!editable);m_readOnlySource->setPlainText(m_file.source());
    pathLabel->setText(QDir(project->root).relativeFilePath(m_file.path()));
    setDocument(m_file.document);
    m_history->setClean();

    connect(add,&QPushButton::clicked,this,&VisualEventEditor::addCommand);
    connect(m_library,&QListWidget::itemDoubleClicked,this,[this]{addCommand();});
    connect(m_commands,&QListWidget::currentRowChanged,this,[this]{if(!m_updating){++m_mergeEpoch;refreshDetails();}});
    auto reorder=[this]{
        if(m_updating) return;
        auto d=m_document;d.commands.clear();
        for(int i=0;i<m_commands->count();++i) d.commands.append(m_document.commands.at(m_commands->item(i)->data(Qt::UserRole).toInt()));
        change(d);
    };
    commandList->orderChanged=reorder;
    connect(m_commands->model(),&QAbstractItemModel::rowsMoved,this,reorder,Qt::QueuedConnection);
    connect(m_delete,&QPushButton::clicked,this,&VisualEventEditor::removeCommand);
    connect(m_duplicate,&QPushButton::clicked,this,&VisualEventEditor::duplicateCommand);
    connect(m_up,&QPushButton::clicked,this,[this]{moveCommand(-1);});
    connect(m_down,&QPushButton::clicked,this,[this]{moveCommand(1);});
    connect(m_argument,&QComboBox::currentTextChanged,this,[this]{updateCommand();});
    connect(m_value,&QComboBox::currentTextChanged,this,[this]{updateCommand();});
    connect(m_comparison,&QComboBox::currentTextChanged,this,[this]{updateCommand();});
    connect(m_text,&QPlainTextEdit::textChanged,this,[this]{updateCommand();});
    connect(m_lock,&QCheckBox::toggled,this,[this](bool checked){if(!m_updating){auto d=m_document;d.lockPlayer=checked;change(d);}});
    connect(m_label,&QLineEdit::textChanged,this,[this](const QString &s){if(!m_updating){auto d=m_document;d.label=s;change(d,"label");}});
    connect(previewButton,&QPushButton::clicked,this,[this]{preview(false);});
    connect(m_save,&QPushButton::clicked,this,[this]{preview(true);});
    connect(cancel,&QPushButton::clicked,this,&VisualEventEditor::reject);
    connect(external,&QPushButton::clicked,this,[this]{Editor::openInTextEditor(m_file.path());});
}
void VisualEventEditor::setDocument(const Document &d) {
    const int selected=m_commands->currentRow();m_updating=true;m_document=d;
    if(m_label->text()!=d.label) m_label->setText(d.label);
    m_lock->setChecked(d.lockPlayer);
    m_commands->clear();int indent=0;
    for(int i=0;i<d.commands.size();++i) {
        const auto &c=d.commands[i];if(c.id=="else"||c.id=="endif") indent=qMax(0,indent-1);
        auto item=new QListWidgetItem(QString(indent*3,' ')+summary(c),m_commands);item->setData(Qt::UserRole,i);
        if(c.id=="ifvar"||c.id=="ifflag"||c.id=="else") ++indent;
    }
    m_commands->setCurrentRow(m_commands->count()?qBound(0,selected,m_commands->count()-1):-1);m_updating=false;refreshDetails();
}
void VisualEventEditor::change(const Document &d,const QString &mergeKey) {
    if(m_updating||d.toJson()==m_document.toJson()) return;
    m_history->push(new DocumentChange(m_document,d,mergeKey.isEmpty()?QString():QString::number(m_mergeEpoch)+mergeKey,
                                      [this](const Document &next){setDocument(next);}));
}
void VisualEventEditor::refreshDetails() {
    m_updating=true;const int row=m_commands->currentRow();
    const bool valid=m_file.isEditable()&&row>=0&&row<m_document.commands.size();
    m_delete->setEnabled(valid);m_duplicate->setEnabled(valid&&m_document.commands[row].id!="end");
    m_up->setEnabled(valid&&row>0);m_down->setEnabled(valid&&row+1<m_document.commands.size());
    const Command c=valid?m_document.commands[row]:Command();const auto spec=commandSpec(c.id);
    const auto first=spec?spec->first:Field::None;const auto second=spec?spec->second:Field::None;
    auto fill=[&](QComboBox *combo,Field field,const QString &value){
        QStringList suggestions;
        if(field==Field::Flag) suggestions=m_project->flagNames;
        if(field==Field::Variable) suggestions=m_project->varNames;
        if(field==Field::Item) suggestions=m_project->itemNames;
        if(field==Field::Music||field==Field::Sound) suggestions=m_project->songNames;
        // Do not reset the line edit and cursor for each keystroke.
        if(combo->property("field").toInt()!=int(field)||combo->property("initialized").toBool()==false) {
            combo->clear();combo->setEditable(field!=Field::Boolean);
            if(field==Field::Boolean) {combo->addItem(tr("ON"),"1");combo->addItem(tr("OFF"),"0");}
            else combo->addItems(suggestions);
            combo->setProperty("field",int(field));combo->setProperty("initialized",true);
        }
        if(field==Field::Boolean) combo->setCurrentIndex(combo->findData(value));
        else if(combo->currentText()!=value) combo->setCurrentText(value);
    };
    fill(m_argument,first,c.argument);fill(m_value,second,c.value);
    m_argument->setVisible(first!=Field::None&&first!=Field::Text);m_firstLabel->setVisible(m_argument->isVisibleTo(this));
    m_firstLabel->setText(first==Field::Flag?tr("Flag"):first==Field::Variable?tr("Variable"):first==Field::Item?tr("Item"):first==Field::Music?tr("BGM"):first==Field::Sound?tr("Sound Effect"):tr("Frames"));
    m_value->setVisible(second!=Field::None);m_secondLabel->setVisible(second!=Field::None);
    m_secondLabel->setText(second==Field::Boolean?tr("1 = ON, 0 = OFF"):c.id=="giveitem"?tr("Quantity"):tr("Value"));
    m_comparison->setVisible(c.id=="ifvar");m_comparison->setCurrentIndex(m_comparison->findData(c.comparison));
    m_text->setVisible(first==Field::Text);m_textHint->setVisible(first==Field::Text);
    if(m_text->toPlainText()!=c.text) m_text->setPlainText(c.text);
    m_updating=false;
}
void VisualEventEditor::updateCommand() {
    int row=m_commands->currentRow();if(m_updating||!m_file.isEditable()||row<0) return;
    auto d=m_document;auto &c=d.commands[row];const auto spec=commandSpec(c.id);if(!spec) return;
    if(spec->first==Field::Text) c.text=m_text->toPlainText();else if(spec->first!=Field::None) c.argument=m_argument->currentText();
    if(spec->second!=Field::None) c.value=spec->second==Field::Boolean?m_value->currentData().toString():m_value->currentText();
    if(c.id=="ifvar") c.comparison=m_comparison->currentData().toString();
    change(d,"command"+QString::number(row));
}
void VisualEventEditor::addCommand() {
    if(!m_file.isEditable()||!m_library->currentItem()) return;
    auto d=m_document;int row=m_commands->currentRow();
    row=row<0?d.commands.size():row+(d.commands[row].id=="end"?0:1);
    const QString id=m_library->currentItem()->data(Qt::UserRole).toString();
    d.commands.insert(row,defaultCommand(id));
    if(id=="ifflag"||id=="ifvar") {d.commands.insert(row+1,defaultCommand("else"));d.commands.insert(row+2,defaultCommand("endif"));}
    ++m_mergeEpoch;change(d);m_commands->setCurrentRow(row);
}
void VisualEventEditor::removeCommand() {
    int row=m_commands->currentRow();if(row<0||!m_file.isEditable()) return;
    auto d=m_document;const int last=blockEnd(d,row);d.commands.remove(row,last-row+1);++m_mergeEpoch;change(d);
}
void VisualEventEditor::duplicateCommand() {
    int row=m_commands->currentRow();if(row<0||!m_file.isEditable()||m_document.commands[row].id=="end") return;
    auto d=m_document;const int last=blockEnd(d,row);const auto copy=d.commands.mid(row,last-row+1);
    for(int i=0;i<copy.size();++i) d.commands.insert(last+1+i,copy[i]);
    ++m_mergeEpoch;change(d);
}
void VisualEventEditor::moveCommand(int delta) {
    int row=m_commands->currentRow();if(row<0||row+delta<0||row+delta>=m_document.commands.size()) return;
    auto d=m_document;d.commands.move(row,row+delta);++m_mergeEpoch;change(d);m_commands->setCurrentRow(row+delta);
}
void VisualEventEditor::preview(bool save) {
    QString error;const auto script=m_file.preview(m_document,&error);
    if(!error.isEmpty()) {QMessageBox::warning(this,tr("Visual Event Editor"),error);return;}
    QDialog review(this);review.setWindowTitle(tr("Preview Generated Script"));review.resize(820,620);
    auto layout=new QVBoxLayout(&review);
    auto description=new QLabel(tr("Only this event's visual editor block will be saved. A backup of the original file is created before replacement."));description->setWordWrap(true);layout->addWidget(description);
    auto code=new QPlainTextEdit(script);code->setReadOnly(true);code->setLineWrapMode(QPlainTextEdit::NoWrap);layout->addWidget(code,1);
    auto buttons=new QDialogButtonBox(save?QDialogButtonBox::Save|QDialogButtonBox::Cancel:QDialogButtonBox::Close);
    layout->addWidget(buttons);connect(buttons,&QDialogButtonBox::accepted,&review,&QDialog::accept);connect(buttons,&QDialogButtonBox::rejected,&review,&QDialog::reject);
    if(review.exec()!=QDialog::Accepted||!save) return;
    QString backup;
    if(!m_file.save(m_document,&error,&backup)) {QMessageBox::warning(this,tr("Visual Event Editor"),error);return;}
    m_history->setClean();accept();
}
void VisualEventEditor::reject() {
    if(!m_history->isClean()&&QMessageBox::question(this,tr("Visual Event Editor"),tr("Discard the unsaved event command edits?"),QMessageBox::Discard|QMessageBox::Cancel)!=QMessageBox::Discard) return;
    QDialog::reject();
}
