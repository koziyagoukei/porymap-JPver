#ifndef VISUALEVENTEDITOR_H
#define VISUALEVENTEDITOR_H

#include <QDialog>
#include "visualeventscript.h"

class Project;
class Map;
class QListWidget;
class QComboBox;
class QPlainTextEdit;
class QLineEdit;
class QCheckBox;
class QLabel;
class QPushButton;
class QUndoStack;

class VisualEventEditor : public QDialog {
    Q_OBJECT
public:
    VisualEventEditor(Project *project, Map *map, const QString &scriptLabel, QWidget *parent = nullptr);
    QString scriptLabel() const { return m_document.label; }

private:
    VisualEvent::ScriptFile m_file;
    VisualEvent::Document m_document;
    QListWidget *m_library, *m_commands;
    QLineEdit *m_label;
    QComboBox *m_argument, *m_value, *m_comparison;
    QPlainTextEdit *m_text, *m_readOnlySource;
    QCheckBox *m_lock;
    QLabel *m_status, *m_firstLabel, *m_secondLabel, *m_textHint;
    QPushButton *m_save, *m_delete, *m_duplicate, *m_up, *m_down;
    QUndoStack *m_history;
    Project *m_project;
    bool m_updating = false;
    int m_mergeEpoch = 0;
    void setDocument(const VisualEvent::Document &document);
    void change(const VisualEvent::Document &document, const QString &mergeKey = QString());
    void refreshDetails();
    void updateCommand();
    void addCommand();
    void removeCommand();
    void duplicateCommand();
    void moveCommand(int delta);
    void preview(bool save);
    void reject() override;
};
#endif
