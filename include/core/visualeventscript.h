#ifndef VISUALEVENTSCRIPT_H
#define VISUALEVENTSCRIPT_H

#include <QByteArray>
#include <QJsonObject>
#include <QSet>
#include <QSize>
#include <QMap>
#include <QStringList>
#include <QVector>
#include <functional>

// Presentation and command definitions are independent of the project and dialog.
namespace VisualEvent {
struct FileChange {
    QString path;
    QByteArray before, after;
    bool existed = false;
};
struct ScriptPatch {
    FileChange file;
    QByteArray block;
    int start = -1, length = 0;
};
bool combinePatches(const QVector<ScriptPatch> &, QVector<FileChange> *, QString *error);
bool commitChanges(const QString &root, const QVector<FileChange> &, QString *error,
                   QStringList *backups = nullptr, const std::function<bool(int)> &permitCommit = {});
enum class Field { None, Text, Flag, Variable, Number, Item, Sound, Music, Comparison, Boolean, Object, Map, Species, Move, Trainer, Nature, Ability, Ball, Type };
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
    QJsonObject options;
};
struct Document {
    QString label;
    bool lockPlayer = true;
    QVector<Command> commands;
    QString objectFlag; // Optional template request, applied to the selected NPC by Porymap.
    QJsonObject npcTrainer;
    QJsonObject toJson() const;
    static bool fromJson(const QJsonObject &, Document *out);
};
const QVector<CommandSpec> &commandSpecs();
const CommandSpec *commandSpec(const QString &id);
QString commandName(const QString &id);
Command defaultCommand(const QString &id);
QString summary(const Command &command);
QStringList movementActions(const Command &, QString *error = nullptr);
QVector<Command> templateCommands(const QString &id, const QString &flag, const QString &item,
                                  const QString &text, const QString &target);

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
    bool prepare(const Document &, ScriptPatch *, QString *error) const;
    void setMapSizes(const QMap<QString, QSize> &sizes) { m_mapSizes = sizes; }
    Document document;

private:
    QString m_root, m_path, m_reason, m_originalLabel;
    QStringList m_sources;
    QMap<QString, QSize> m_mapSizes;
    QByteArray m_original;
    bool m_exists = false, m_editable = false, m_pory = false, m_generated = false;
    int m_start = -1, m_length = 0;
    QString validate(const Document &) const;
    QSet<QString> otherLabels(QString *error = nullptr) const;
    QByteArray block(const Document &) const;
    QString generate(const Document &, bool native = false) const;
};
}
#endif
