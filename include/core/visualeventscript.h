#ifndef VISUALEVENTSCRIPT_H
#define VISUALEVENTSCRIPT_H

#include <QByteArray>
#include <QJsonObject>
#include <QSet>
#include <QStringList>
#include <QVector>

// Presentation and command definitions are independent of the project and dialog.
namespace VisualEvent {
enum class Field { None, Text, Flag, Variable, Number, Item, Sound, Music, Comparison, Boolean };
struct CommandSpec {
    QString id;
    const char *name;
    Field first = Field::None;
    Field second = Field::None;
};
struct Command {
    QString id;
    QString argument;
    QString value;
    QString text;
    QString comparison = "==";
};
struct Document {
    QString label;
    bool lockPlayer = true;
    QVector<Command> commands;
    QJsonObject toJson() const;
    static bool fromJson(const QJsonObject &, Document *out);
};
const QVector<CommandSpec> &commandSpecs();
const CommandSpec *commandSpec(const QString &id);
QString commandName(const QString &id);
Command defaultCommand(const QString &id);
QString summary(const Command &command);

// Only an exactly reproducible owned block can be replaced. Other bytes are opaque.
class ScriptFile {
public:
    bool open(const QString &projectRoot, const QString &mapScriptPath, const QString &label,
              const QStringList &scriptPaths, bool usesPoryscript);
    bool isEditable() const { return m_editable; }
    QString reason() const { return m_reason; }
    QString path() const { return m_path; }
    QString source() const;
    bool isPoryscript() const { return m_pory; }
    bool isGeneratedSource() const { return m_generated; }
    QString uniqueLabel(const QString &base) const;
    QString preview(const Document &document, QString *error = nullptr) const;
    bool save(const Document &document, QString *error, QString *backupPath = nullptr);
    Document document;

private:
    QString m_root, m_path, m_reason, m_originalLabel;
    QStringList m_sources;
    QByteArray m_original;
    bool m_exists = false, m_editable = false, m_pory = false, m_generated = false;
    int m_start = -1, m_length = 0;
    QString validate(const Document &) const;
    QSet<QString> otherLabels(QString *error = nullptr) const;
    QByteArray block(const Document &) const;
    QString generate(const Document &) const;
};
}
#endif
