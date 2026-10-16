#include "visualeventscript.h"
#include "pokemonevent.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QLockFile>
#include <QRegularExpression>
#include <QSaveFile>
#include <QUuid>
#include <algorithm>
#include <memory>
#include <vector>

namespace VisualEvent {
static bool identifier(const QString &s) {
    static const QRegularExpression re("^[A-Za-z_][A-Za-z_0-9]*$");
    return re.match(s).hasMatch();
}
static bool operand(const QString &s) {
    // No directives, expressions, commas, newlines or source injection.
    static const QRegularExpression re("^(?:[A-Za-z_][A-Za-z_0-9]*|0[xX][0-9a-fA-F]{1,4}|[0-9]{1,5})$");
    if (!re.match(s).hasMatch()) return false;
    if (identifier(s)) return true;
    bool ok = false;
    uint value = s.toUInt(&ok, s.startsWith("0x", Qt::CaseInsensitive) ? 16 : 10);
    return ok && value <= 65535;
}
const QVector<CommandSpec> &commandSpecs() {
    static const QVector<CommandSpec> specs = {
        {"message", QT_TRANSLATE_NOOP("Porymap", "Show Message"), Field::Text},
        {"question", QT_TRANSLATE_NOOP("Porymap", "Ask Yes / No"), Field::Text},
        {"setflag", QT_TRANSLATE_NOOP("Porymap", "Turn Flag ON"), Field::Flag},
        {"clearflag", QT_TRANSLATE_NOOP("Porymap", "Turn Flag OFF"), Field::Flag},
        {"setvar", QT_TRANSLATE_NOOP("Porymap", "Assign Variable"), Field::Variable, Field::Number},
        {"addvar", QT_TRANSLATE_NOOP("Porymap", "Add to Variable"), Field::Variable, Field::Number},
        {"subvar", QT_TRANSLATE_NOOP("Porymap", "Subtract from Variable"), Field::Variable, Field::Number},
        {"ifflag", QT_TRANSLATE_NOOP("Porymap", "If Flag"), Field::Flag, Field::Boolean},
        {"ifvar", QT_TRANSLATE_NOOP("Porymap", "If Variable"), Field::Variable, Field::Number},
        {"else", QT_TRANSLATE_NOOP("Porymap", "Otherwise")},
        {"endif", QT_TRANSLATE_NOOP("Porymap", "End Condition")},
        {"giveitem", QT_TRANSLATE_NOOP("Porymap", "Give Item"), Field::Item, Field::Number},
        {"faceplayer", QT_TRANSLATE_NOOP("Porymap", "Face Player")},
        {"movement", QT_TRANSLATE_NOOP("Porymap", "Move NPC / Player"), Field::Object},
        {"warp", QT_TRANSLATE_NOOP("Porymap", "Transfer to Map"), Field::Map},
        {"removeobject", QT_TRANSLATE_NOOP("Porymap", "Hide NPC"), Field::Object},
        {"wildpokemon", QT_TRANSLATE_NOOP("Porymap", "Wild Pokemon Battle")},
        {"trainerpokemon", QT_TRANSLATE_NOOP("Porymap", "Trainer Battle")},
        {"trainerstatus", QT_TRANSLATE_NOOP("Porymap", "Check Trainer Defeated")},
        {"healpokemon", QT_TRANSLATE_NOOP("Porymap", "Heal Party Pokemon")},
        {"giftpokemon", QT_TRANSLATE_NOOP("Porymap", "Give Pokemon")},
        {"eggPokemon", QT_TRANSLATE_NOOP("Porymap", "Give Pokemon Egg")},
        {"partypokemon", QT_TRANSLATE_NOOP("Porymap", "Select / Modify Party Pokemon")},
        {"delay", QT_TRANSLATE_NOOP("Porymap", "Wait Frames"), Field::Number},
        {"playse", QT_TRANSLATE_NOOP("Porymap", "Play Sound Effect"), Field::Sound},
        {"playbgm", QT_TRANSLATE_NOOP("Porymap", "Play BGM"), Field::Music},
        {"end", QT_TRANSLATE_NOOP("Porymap", "End Script")},
    };
    return specs;
}
const CommandSpec *commandSpec(const QString &id) {
    for (const auto &spec : commandSpecs()) if (spec.id == id) return &spec;
    return nullptr;
}
QString commandName(const QString &id) {
    const auto spec = commandSpec(id);
    return spec ? QCoreApplication::translate("Porymap", spec->name) : id;
}
Command defaultCommand(const QString &id) {
    if (isPokemonCommand(id)) return pokemonCommand(id);
    Command c; c.id = id;
    const auto spec = commandSpec(id);
    if (!spec) return c;
    switch (spec->first) {
    case Field::Flag: c.argument = "FLAG_TEMP_1"; break;
    case Field::Variable: c.argument = id == "ifvar" ? "VAR_RESULT" : "VAR_TEMP_1"; break;
    case Field::Item: c.argument = "ITEM_POTION"; break;
    case Field::Sound: c.argument = "SE_SELECT"; break;
    case Field::Music: c.argument = "MUS_LITTLEROOT"; break;
    case Field::Number: c.argument = "30"; break;
    case Field::Object: c.argument = "VAR_LAST_TALKED"; break;
    default: break;
    }
    if (spec->second != Field::None) c.value = "1";
    if (id == "movement") c.options = {{"route", QJsonArray{QJsonObject{{"action", "walk"}, {"direction", "down"}, {"speed", "normal"}, {"count", 1}}}}};
    if (id == "warp") c.options = {{"x", 0}, {"y", 0}};
    return c;
}
QString summary(const Command &c) {
    if (isPokemonCommand(c.id)) return commandName(c.id) + ": " + c.options.value("species").toString(c.options.value("trainer").toString(c.options.value("operation").toString()));
    QString detail = c.text.isEmpty() ? c.argument : QString(c.text).replace('\n', " / ");
    if (c.id == "ifvar") detail += " " + c.comparison;
    if (!c.value.isEmpty()) detail += " " + (c.id=="ifflag" ? (c.value=="1"?"ON":"OFF") : c.value);
    if (c.id == "movement") detail += " (" + QString::number(c.options["route"].toArray().size()) + ")";
    if (c.id == "warp") detail += QString(" (%1, %2)").arg(c.options["x"].toInt()).arg(c.options["y"].toInt());
    return commandName(c.id) + (detail.isEmpty() ? "" : ": " + detail.left(100));
}
QJsonObject Document::toJson() const {
    QJsonArray list;
    for (const auto &c : commands) {
        QJsonObject object{{"id", c.id}, {"argument", c.argument}, {"value", c.value},
                           {"text", c.text}, {"comparison", c.comparison}};
        // Keep legacy blocks byte-for-byte reproducible. Extensions are optional.
        if (!c.options.isEmpty()) object["options"] = c.options;
        list.append(object);
    }
    QJsonObject result{{"version", 1}, {"label", label}, {"lockPlayer", lockPlayer}, {"commands", list}};
    if (!objectFlag.isEmpty()) result["objectFlag"] = objectFlag;
    if (!npcTrainer.isEmpty()) result["npcTrainer"] = npcTrainer;
    return result;
}
bool Document::fromJson(const QJsonObject &o, Document *out) {
    const QSet<QString> keys = {"version", "label", "lockPlayer", "commands", "objectFlag", "npcTrainer"};
    for (auto it=o.begin(); it!=o.end(); ++it) if (!keys.contains(it.key())) return false;
    if (o["version"].toInt() != 1 || !o["label"].isString() || !o["lockPlayer"].isBool()
        || !o["commands"].isArray() || o["commands"].toArray().size() > 1000) return false;
    Document d; d.label = o["label"].toString(); d.lockPlayer = o["lockPlayer"].toBool();
    if (o.contains("objectFlag") && (!o["objectFlag"].isString() || !identifier(o["objectFlag"].toString()))) return false;
    d.objectFlag = o["objectFlag"].toString();
    if (o.contains("npcTrainer")) {
        if (!o["npcTrainer"].isObject()) return false;
        d.npcTrainer = o["npcTrainer"].toObject();
        if (d.npcTrainer.size() != 2 || !QStringList{"TRAINER_TYPE_NONE", "TRAINER_TYPE_NORMAL"}.contains(d.npcTrainer["type"].toString())
            || !d.npcTrainer["sight"].isDouble() || d.npcTrainer["sight"].toDouble() != d.npcTrainer["sight"].toInt() || d.npcTrainer["sight"].toInt() < 1 || d.npcTrainer["sight"].toInt() > 16) return false;
    }
    for (const auto &v : o["commands"].toArray()) {
        if (!v.isObject()) return false;
        auto a = v.toObject();
        const QSet<QString> fields = {"id", "argument", "value", "text", "comparison"};
        if (a.size() != fields.size() + (a.contains("options") ? 1 : 0)) return false;
        if (a.contains("options") && !a["options"].isObject()) return false;
        for (const auto &key : fields) if (!a[key].isString()) return false;
        if (!commandSpec(a["id"].toString())) return false;
        d.commands.append({a["id"].toString(), a["argument"].toString(), a["value"].toString(),
                           a["text"].toString(), a["comparison"].toString(), a["options"].toObject()});
    }
    *out = d;
    return true;
}
QStringList movementActions(const Command &c, QString *error) {
    auto fail = [&]() { if (error) *error = QCoreApplication::translate("Porymap", "Choose a valid movement route (1 to 256 steps; waits of 1, 2, 4, 8 or 16 frames)."); return QStringList(); };
    if (c.options.size() != 1 || !c.options["route"].isArray()) return fail();
    const auto route = c.options["route"].toArray();
    if (route.isEmpty() || route.size() > 256) return fail();
    QStringList actions;
    for (const auto &value : route) {
        if (!value.isObject()) return fail();
        const auto step = value.toObject();
        if (step.size() != 4 || !step["action"].isString() || !step["direction"].isString() || !step["speed"].isString()
            || !step["count"].isDouble() || step["count"].toDouble() != step["count"].toInt()) return fail();
        const auto action = step["action"].toString(), direction = step["direction"].toString(), speed = step["speed"].toString();
        const int count = step["count"].toInt();
        if (!QStringList{"down", "up", "left", "right"}.contains(direction) || !QStringList{"slow", "normal", "fast"}.contains(speed)) return fail();
        if (action == "wait") {
            if (!QList<int>{1, 2, 4, 8, 16}.contains(count)) return fail();
            actions.append("delay_" + QString::number(count));
        } else if (action == "walk" || action == "face") {
            if (count < 1 || count > 64) return fail();
            const QString macro = action == "face" ? "face_" + direction : (speed == "normal" ? "walk_" : "walk_" + speed + "_") + direction;
            for (int i = 0; i < count; ++i) actions.append(macro);
        } else return fail();
        if (actions.size() > 256) return fail();
    }
    return actions;
}
QVector<Command> templateCommands(const QString &id, const QString &flag, const QString &item, const QString &text, const QString &target) {
    auto make = [](const QString &id, const QString &argument = QString(), const QString &value = QString()) {
        auto c = defaultCommand(id); c.argument = argument; c.value = value; return c;
    };
    if (!QStringList{"item", "once", "disappear"}.contains(id)) return {};
    QVector<Command> result{make("ifflag", flag, "0")};
    if (id == "item") {
        result.append(make("giveitem", item, "1"));
        // A full bag must not consume the item or set its flag.
        result.append(make("ifvar", "VAR_RESULT", "1"));
    } else { auto c = make("message"); c.text = text; result.append(c); }
    result.append(make("setflag", flag));
    if (id == "item" || id == "disappear") result.append(make("removeobject", target));
    if (id == "item") result.append(make("endif"));
    result.append(make("endif"));
    return result;
}

static QByteArray read(const QString &path, bool *ok = nullptr) {
    QFile file(path);
    const bool success = file.open(QIODevice::ReadOnly);
    if (ok) *ok = success;
    return success ? file.readAll() : QByteArray();
}
static QString normalized(QByteArray bytes) {
    if (bytes.startsWith("\xef\xbb\xbf")) bytes.remove(0, 3);
    return QString::fromUtf8(bytes).replace("\r\n", "\n");
}
static bool utf8(QByteArray bytes) {
    if (bytes.startsWith("\xef\xbb\xbf")) bytes.remove(0,3);
    return !bytes.contains('\0') && QString::fromUtf8(bytes).toUtf8() == bytes;
}
static bool inside(const QString &root, const QString &path) {
    const QString canonicalRoot = QFileInfo(root).canonicalFilePath();
    QFileInfo info(path);
    const QString canonical = info.exists() ? info.canonicalFilePath()
        : QFileInfo(info.absolutePath()).canonicalFilePath() + "/" + info.fileName();
    return !canonicalRoot.isEmpty() && canonical.startsWith(canonicalRoot + "/", Qt::CaseInsensitive);
}
static QString sourcePath(QString path) {
    if (path.endsWith(".inc", Qt::CaseInsensitive)) {
        QString pory = path.left(path.size()-4) + ".pory";
        if (QFileInfo::exists(pory)) return pory;
    }
    return path;
}
// Mask comments and strings without changing positions. Raw Poryscript sections
// remain visible so their assembly labels participate in collision detection.
static QString masked(QString s, bool pory) {
    bool quoted=false, escaped=false, line=false, comment=false;
    for (int i=0;i<s.size();++i) {
        QChar c=s[i]; QChar n=i+1<s.size()?s[i+1]:QChar();
        if (line) { if(c=='\n') line=false; else s[i]=' '; continue; }
        if (comment) { s[i]=' '; if(c=='*'&&n=='/') {s[++i]=' ';comment=false;} continue; }
        if (quoted) {
            if (!escaped && c=='"') quoted=false;
            if (c!='\n') s[i]=' ';
            escaped=(!escaped&&c=='\\');
            continue;
        }
        if(c=='"') {quoted=true;s[i]=' ';continue;}
        if((!pory&&c=='@') || (pory&&(c=='#'||(c=='/'&&n=='/')))) {line=true;s[i]=' ';continue;}
        if(pory&&c=='/'&&n=='*') {comment=true;s[i]=' ';s[++i]=' ';}
    }
    return s;
}
static QStringList labels(const QByteArray &bytes, bool pory) {
    QString text = masked(normalized(bytes),pory);
    static const QRegularExpression raw("^\\s*([A-Za-z_][A-Za-z_0-9]*)\\s*:{1,2}",QRegularExpression::MultilineOption);
    static const QRegularExpression high("\\b(?:script|text|movement|mart|mapscripts)(?:\\(\\s*(?:global|local)\\s*\\))?\\s+([A-Za-z_][A-Za-z_0-9]*)");
    QStringList out;
    auto it=raw.globalMatch(text);while(it.hasNext()) out.append(it.next().captured(1));
    if (pory) {it=high.globalMatch(text);while(it.hasNext()) out.append(it.next().captured(1));}
    return out;
}
static bool atTopLevel(const QByteArray &bytes, int stop, bool pory) {
    bool quote=false,escape=false,line=false,comment=false,raw=false;
    int depth=0;
    for(int i=0;i<stop;++i) {
        const char c=bytes[i],n=i+1<stop?bytes[i+1]:0;
        if(line) {if(c=='\n') line=false;continue;}
        if(comment) {if(c=='*'&&n=='/') {comment=false;++i;}continue;}
        if(raw) {if(c=='`') raw=false;continue;}
        if(quote) {if(!escape&&c=='"') quote=false;escape=(!escape&&c=='\\');continue;}
        if(c=='"') {quote=true;continue;}
        if(pory&&c=='`') {raw=true;continue;}
        if((!pory&&c=='@')||(pory&&(c=='#'||(c=='/'&&n=='/')))) {line=true;continue;}
        if(pory&&c=='/'&&n=='*') {comment=true;++i;continue;}
        if(pory&&c=='{') ++depth;
        if(pory&&c=='}'&&--depth<0) return false;
    }
    if(!pory) {
        // Cross-file open assembler conditionals/macros are not safe append points.
        const QString plain=masked(normalized(bytes.left(stop)),false);
        static const QRegularExpression re("^\\s*(?:\\.(macro|if|ifdef|ifndef|rept|irp|irpc|endm|endif|endr)|#\\s*(if|ifdef|ifndef|endif))\\b",QRegularExpression::MultilineOption);
        auto it=re.globalMatch(plain);
        while(it.hasNext()) {const auto m=it.next();const auto word=m.captured(1).isEmpty()?m.captured(2):m.captured(1);
            depth+=(word=="endm"||word=="endif"||word=="endr")?-1:1;if(depth<0) return false;}
    }
    return !quote&&!comment&&!raw&&depth==0;
}
QSet<QString> ScriptFile::otherLabels(QString *error) const {
    QSet<QString> out;
    for (const auto &path : m_sources) {
        if (path == m_path && !m_exists && !QFileInfo::exists(path)) continue;
        bool ok; auto bytes=read(path,&ok);
        if (!ok) {if(error) *error=QCoreApplication::translate("Porymap", "Cannot read a script source: %1").arg(path);return {};}
        if (path==m_path && m_start>=0) bytes.remove(m_start,m_length);
        for (const auto &label : labels(bytes,path.endsWith(".pory",Qt::CaseInsensitive))) out.insert(label);
    }
    return out;
}
QString ScriptFile::uniqueLabel(const QString &base) const {
    QString clean=base;clean.replace(QRegularExpression("[^A-Za-z_0-9]"),"_");
    if (!identifier(clean)) clean="Map_VisualEvent";
    const auto used=otherLabels();
    for (int i=1;i<100000;++i) {
        const QString candidate=clean+QString::number(i);
        if (!used.contains(candidate)) return candidate;
    }
    return clean+QUuid::createUuid().toString(QUuid::Id128);
}
bool ScriptFile::open(const QString &root, const QString &mapPath, const QString &label,
                      const QStringList &paths, bool usesPoryscript) {
    *this=ScriptFile();document.label=label;m_root=QFileInfo(root).canonicalFilePath();
    m_path=QDir::cleanPath(QFileInfo(sourcePath(mapPath)).absoluteFilePath());
    m_sources=paths;
    m_sources.append(m_path);
    for(auto &p:m_sources) p=QDir::cleanPath(QFileInfo(sourcePath(p)).absoluteFilePath());
    m_sources.removeDuplicates();
    // Resolve references in other files, including .pory when a generated .inc exists.
    if (identifier(label)) {
        QStringList matches;
        for(const auto &p:m_sources) if (labels(read(p),p.endsWith(".pory",Qt::CaseInsensitive)).contains(label)) matches.append(p);
        if(matches.size()==1) m_path=matches.first();
        else if(matches.size()>1) {m_reason=QCoreApplication::translate("Porymap", "The script label is defined more than once.");return false;}
    }
    m_pory=m_path.endsWith(".pory",Qt::CaseInsensitive);
    if (!inside(m_root,m_path) || (!m_pory&&!m_path.endsWith(".inc",Qt::CaseInsensitive))) {
        m_reason=QCoreApplication::translate("Porymap", "The script source must be an .inc or .pory file inside this project.");return false;
    }
    if(usesPoryscript&&!m_pory) {
        m_generated=true;
        m_reason=QCoreApplication::translate("Porymap", "Poryscript is enabled, but its .pory source was not found. Generated .inc files cannot be edited.");return false;
    }
    m_exists=QFileInfo::exists(m_path);
    bool ok=true;m_original=m_exists?read(m_path,&ok):QByteArray();
    if(!ok || !utf8(m_original)) {
        m_reason=QCoreApplication::translate("Porymap", "The source is unreadable or is not valid UTF-8. Use the external editor to preserve its encoding.");return false;
    }
    if(!m_pory&&QRegularExpression("^\\s*#(?:line)?\\s*\\d+\\s+\"[^\"\\n]*\\.pory\"",QRegularExpression::MultilineOption).match(normalized(m_original)).hasMatch()) {
        m_generated=true;
        m_reason=QCoreApplication::translate("Porymap", "This .inc was generated from Poryscript. Its .pory source is required for editing.");return false;
    }
    const QString prefix=m_pory?"// ":"@ ";
    const QByteArray begin=(prefix+"PORYMAP_VISUAL_BEGIN "+label+" v1").toUtf8();
    const QByteArray end=(prefix+"PORYMAP_VISUAL_END "+label).toUtf8();
    // Match only complete, top-level comment lines; strings cannot impersonate metadata.
    const auto lines=m_original.split('\n');int position=0,beginCount=0,endCount=0,endPos=-1,dataPos=-1;
    for(const auto &line:lines) {
        QByteArray trimmed=line; if(trimmed.endsWith('\r')) trimmed.chop(1);
        if(trimmed==begin) {m_start=position;++beginCount;dataPos=position+line.size()+1;}
        if(trimmed==end) {endPos=position+line.size()+1;++endCount;}
        position+=line.size()+1;
    }
    if(beginCount||endCount) {
        if(beginCount!=1||endCount!=1||endPos<=dataPos) {m_reason=QCoreApplication::translate("Porymap", "The visual editor block is incomplete or duplicated.");return false;}
        m_length=qMin(endPos,int(m_original.size()))-m_start;
        if(!atTopLevel(m_original,m_start,m_pory)) {
            m_reason=QCoreApplication::translate("Porymap", "The script is not at a safe top-level editing position.");return false;
        }
        QByteArray dataLine=m_original.mid(dataPos).split('\n').first().trimmed();
        const QByteArray dataPrefix=(prefix+"PORYMAP_VISUAL_DATA ").toUtf8();
        QJsonParseError parseError;
        const auto json=QJsonDocument::fromJson(QByteArray::fromBase64(dataLine.mid(dataPrefix.size())),&parseError);
        if(!dataLine.startsWith(dataPrefix)||parseError.error!=QJsonParseError::NoError||!json.isObject()
           ||!Document::fromJson(json.object(),&document)||document.label!=label) {
            m_reason=QCoreApplication::translate("Porymap", "This visual editor data version is unsupported or damaged.");return false;
        }
        // Do not overwrite hand-edited, injected, or unknown commands in an owned block.
        if(normalized(m_original.mid(m_start,m_length))!=normalized(block(document))) {
            m_reason=QCoreApplication::translate("Porymap", "This script was changed outside the visual editor. It is read-only to preserve those changes.");return false;
        }
        m_originalLabel=label;
    } else {
        if(!atTopLevel(m_original,m_original.size(),m_pory)) {
            m_reason=QCoreApplication::translate("Porymap", "The script is not at a safe top-level editing position.");return false;
        }
        if(identifier(label)) {
            for(const auto &p:m_sources) if(labels(read(p),p.endsWith(".pory",Qt::CaseInsensitive)).contains(label)) {
                m_reason=QCoreApplication::translate("Porymap", "This existing script is not a visual editor script. Open it in the external editor.");return false;
            }
            document.label=label;
        } else document.label=uniqueLabel(QFileInfo(m_path).dir().dirName()+"_VisualEvent");
        document.commands={defaultCommand("end")};
    }
    m_editable=true;
    return true;
}
QString ScriptFile::source() const {return normalized(m_original);}

static QString textError(const QString &root,const QString &text) {
    bool ok; const auto bytes=read(root+"/charmap.txt",&ok);
    if(!ok||!utf8(bytes)) return QCoreApplication::translate("Porymap", "A UTF-8 charmap.txt is required to validate dialogue in this project.");
    QStringList tokens;
    QMap<QString,int> lengths;
    static const QRegularExpression quoted("^\\s*'(.+)'\\s*=\\s*([0-9a-fA-F]{2}(?:[ \\t]+[0-9a-fA-F]{2})*)",QRegularExpression::MultilineOption);
    static const QRegularExpression named("^\\s*([A-Z][A-Z_0-9]*)\\s*=\\s*([0-9a-fA-F]{2}(?:[ \\t]+[0-9a-fA-F]{2})*)",QRegularExpression::MultilineOption);
    auto it=quoted.globalMatch(QString::fromUtf8(bytes));
    while(it.hasNext()) {const auto m=it.next();const auto token=m.captured(1);tokens.append(token);lengths[token]=m.captured(2).split(QRegularExpression("\\s+"),Qt::SkipEmptyParts).size();}
    it=named.globalMatch(QString::fromUtf8(bytes));while(it.hasNext()) {const auto m=it.next();const auto token="{"+m.captured(1)+"}";tokens.append(token);lengths[token]=m.captured(2).split(QRegularExpression("\\s+"),Qt::SkipEmptyParts).size();}
    std::sort(tokens.begin(),tokens.end(),[](const QString&a,const QString&b){return a.size()>b.size();});
    const QString input=QString(text).replace('\n',"\\n");
    int encodedBytes=0;
    for(int i=0;i<input.size();) {
        if(input[i]=='$'||input[i]=='\r'||input[i].unicode()<0x20)
            return QCoreApplication::translate("Porymap", "Dialogue contains a reserved terminator or unsupported control character.");
        bool found=false;
        for(const auto &token:tokens) if(!token.isEmpty()&&input.mid(i,token.size())==token) {i+=token.size();encodedBytes+=lengths.value(token);found=true;break;}
        if(!found) return QCoreApplication::translate("Porymap", "Character or control code is missing from charmap.txt near: %1").arg(input.mid(i,16));
        if(encodedBytes>1000) return QCoreApplication::translate("Porymap", "Dialogue exceeds 1000 encoded bytes. Split it into multiple Show Message commands.");
    }
    return {};
}
QString ScriptFile::validate(const Document &d) const {
    if(!m_editable) return m_reason;
    if (!d.objectFlag.isEmpty() && !identifier(d.objectFlag)) return QCoreApplication::translate("Porymap", "Unsupported event command.");
    if(!identifier(d.label) || (!m_originalLabel.isEmpty()&&d.label!=m_originalLabel)) return QCoreApplication::translate("Porymap", "Use a valid script label. Existing labels cannot be renamed here.");
    if(d.commands.isEmpty()||d.commands.size()>1000||d.commands.last().id!="end") return QCoreApplication::translate("Porymap", "The last command must be End Script (maximum 1000 commands).");
    if(d.npcTrainer.value("type").toString()=="TRAINER_TYPE_NORMAL"&&d.commands.first().id!="trainerpokemon")return QCoreApplication::translate("PokemonEvent", "NPC sight detection requires Trainer Battle as the first command.");
    QString scanError;const auto used=otherLabels(&scanError);if(!scanError.isEmpty()) return scanError;
    // Reserve a namespace for local labels emitted by either backend/Poryscript.
    for(const auto &label:used) if(label==d.label||label.startsWith(d.label+"_")) return QCoreApplication::translate("Porymap", "This label or its generated labels conflict with another script.");
    const bool pokemon = std::any_of(d.commands.begin(), d.commands.end(), [](const auto &c) { return isPokemonCommand(c.id); });
    const auto engine = pokemon ? PokemonEngine::inspect(m_root) : PokemonEngine();
    QVector<bool> conditions;
    for(int i=0;i<d.commands.size();++i) {
        const auto &c=d.commands[i];const auto spec=commandSpec(c.id);
        if (pokemon && m_pory && c.text.contains('`')) return QCoreApplication::translate("PokemonEvent", "Pokemon dialogue is too long or contains a raw Poryscript delimiter.");
        if(!spec) return QCoreApplication::translate("Porymap", "Unsupported event command.");
        if (isPokemonCommand(c.id)) {
            const auto issue = validatePokemon(c, engine); if (!issue.isEmpty()) return issue;
            if (c.id == "trainerpokemon" && (i != 0 || d.lockPlayer)) return QCoreApplication::translate("PokemonEvent", "Trainer Battle must be the first command, with automatic player locking disabled, so NPC sight detection can read the native battle opcode.");
            for (const auto &text : pokemonTexts(c)) {
                if (text.size() > 8192 || (m_pory && text.contains('`'))) return QCoreApplication::translate("PokemonEvent", "Pokemon dialogue is too long or contains a raw Poryscript delimiter.");
                const auto issue = textError(m_root, text); if (!issue.isEmpty()) return issue;
            }
            continue;
        }
        if (c.id == "movement") {
            QString error; const auto actions = movementActions(c, &error); if (!error.isEmpty()) return error;
            bool ok; const auto movement = read(m_root + "/asm/macros/movement.inc", &ok);
            if (!ok) return QCoreApplication::translate("Porymap", "Cannot read this project's movement macros.");
            auto required = actions; required.append("step_end");
            for (const auto &name : required) if (!QRegularExpression("(?:create_movement_action\\s+|\\.macro\\s+)" + name + "(?:[,\\s]|$)").match(QString::fromUtf8(movement)).hasMatch())
                return QCoreApplication::translate("Porymap", "A required movement is missing from this project: %1").arg(name);
        } else if (c.id == "warp") {
            const auto size = m_mapSizes.value(c.argument);
            if (!identifier(c.argument) || size.isEmpty() || c.options.size() != 2
                || !c.options["x"].isDouble() || !c.options["y"].isDouble()
                || c.options["x"].toDouble() != c.options["x"].toInt() || c.options["y"].toDouble() != c.options["y"].toInt()
                || c.options["x"].toInt() < 0 || c.options["x"].toInt() >= qMin(size.width(), 65536)
                || c.options["y"].toInt() < 0 || c.options["y"].toInt() >= qMin(size.height(), 65536))
                return QCoreApplication::translate("Porymap", "Choose a known destination map and coordinates within its layout.");
        } else if (!c.options.isEmpty()) return QCoreApplication::translate("Porymap", "Unsupported event command.");
        if ((spec->first==Field::None||spec->first==Field::Text) && !c.argument.isEmpty()) return QCoreApplication::translate("Porymap", "Unsupported event command.");
        if (spec->second==Field::None && !c.value.isEmpty()) return QCoreApplication::translate("Porymap", "Unsupported event command.");
        if (spec->first!=Field::Text && !c.text.isEmpty()) return QCoreApplication::translate("Porymap", "Unsupported event command.");
        if(spec->first!=Field::None&&spec->first!=Field::Text&&!operand(c.argument)) return QCoreApplication::translate("Porymap", "Choose a constant name or an integer from 0 to 65535.");
        if(spec->second!=Field::None&&!operand(c.value)) return QCoreApplication::translate("Porymap", "Choose a constant name or an integer from 0 to 65535.");
        if(spec->first==Field::Text) {
            if(c.text.isEmpty()||c.text.size()>8192) return QCoreApplication::translate("Porymap", "Enter dialogue text (up to 8192 characters).");
            const QString error=textError(m_root,c.text);if(!error.isEmpty()) return error;
        }
        if(c.id=="ifflag" && c.value!="0" && c.value!="1") return QCoreApplication::translate("Porymap", "Select ON or OFF for the flag condition.");
        if(c.id=="ifvar"&&!QStringList({"==","!=","<",">","<=",">="}).contains(c.comparison)) return QCoreApplication::translate("Porymap", "Select a valid comparison.");
        if(c.id=="ifflag"||c.id=="ifvar") conditions.append(false);
        if(conditions.size()>16) return QCoreApplication::translate("Porymap", "Conditions may be nested at most 16 levels deep.");
        if(c.id=="else") {
            if(conditions.isEmpty()||conditions.last()) return QCoreApplication::translate("Porymap", "Otherwise must occur once inside a condition.");
            conditions.last()=true;
        }
        if(c.id=="endif") {
            if(conditions.isEmpty()) return QCoreApplication::translate("Porymap", "End Condition has no matching condition.");
            conditions.removeLast();
        }
        if(c.id=="end"&&(i!=d.commands.size()-1||!conditions.isEmpty())) return QCoreApplication::translate("Porymap", "End Script must occur once, outside conditions, at the end.");
    }
    if(!conditions.isEmpty()) return QCoreApplication::translate("Porymap", "Close every condition with End Condition.");
    // Check target macro availability instead of assuming all forks share commands.
    bool macroOk;const auto macros=read(m_root+"/asm/macros/event.inc",&macroOk);
    if(!macroOk) return QCoreApplication::translate("Porymap", "Cannot verify this project's asm/macros/event.inc commands.");
    QSet<QString> required={"end"};
    if (!d.commands.isEmpty() && d.commands.first().id == "trainerpokemon") required.insert("releaseall");
    if(d.lockPlayer) required.unite({"lockall","releaseall"});
    for(const auto &c:d.commands) {
        if (isPokemonCommand(c.id)) continue;
        if(c.id=="message"||c.id=="question") required.insert("msgbox");
        else if(c.id=="ifflag") {
            required.unite({"checkflag","goto_if_eq","goto"});
            if(m_pory) required.unite({"goto_if_set","goto_if_unset"});
        }
        else if(c.id=="ifvar") required.unite({"compare","goto_if_eq","goto_if_ne","goto_if_lt","goto_if_le","goto_if_gt","goto_if_ge","goto"});
        else if(c.id=="movement") required.unite({"applymovement", "waitmovement"});
        else if(c.id=="warp") required.unite({"warp", "waitstate"});
        else if(c.id!="else"&&c.id!="endif") required.insert(c.id);
    }
    for(const auto &name:required) if(!QRegularExpression("\\.macro\\s+"+name+"(?:\\s|$)").match(QString::fromUtf8(macros)).hasMatch())
        return QCoreApplication::translate("Porymap", "A required script command is missing from this project: %1").arg(name);
    return {};
}
static QString quotedText(QString s) {return s.replace('"',"\\\"").replace('\n',"\\n");}
QString ScriptFile::generate(const Document &d, bool native) const {
    // Raw is Poryscript's native escape hatch. Keep trainerbattle as the first opcode,
    // and preserve optional/named macro parameters without changing the game engine.
    const bool pokemon = std::any_of(d.commands.begin(), d.commands.end(), [](const auto &c) { return isPokemonCommand(c.id); });
    if (m_pory && pokemon && !native) return "raw `\n" + generate(d, true) + "`\n";
    const bool pory = m_pory && !native;
    QString out;QStringList texts, movements;
    struct Branch {QString alternate,done;bool hasElse=false;};QVector<Branch> branches;
    int sequence=0,textIndex=0,movementIndex=0,pokemonIndex=0,depth=1;
    auto line=[&](const QString &s){out+=QString(depth,'\t')+s+"\n";};
    out=pory?"script "+d.label+" {\n":d.label+"::\n";
    if(d.lockPlayer) line("lockall");
    for(const auto &c:d.commands) {
        if (isPokemonCommand(c.id)) out += generatePokemon(c, d.label + "_Pokemon_" + QString::number(++pokemonIndex), &texts);
        else if(c.id=="message"||c.id=="question") {
            QString type=c.id=="question"?"MSGBOX_YESNO":"MSGBOX_DEFAULT";
            if(pory) line("msgbox(\""+quotedText(c.text)+"\", "+type+")");
            else {
                QString textLabel=d.label+"_Text_"+QString::number(++textIndex);
                line("msgbox "+textLabel+", "+type);
                texts.append(textLabel+":\n\t.string \""+quotedText(c.text)+"$\"\n");
            }
        } else if (c.id == "movement") {
            const QString label = d.label + "_Movement_" + QString::number(++movementIndex);
            const auto actions = movementActions(c);
            line(pory ? "applymovement(" + c.argument + ", " + label + ")" : "applymovement " + c.argument + ", " + label);
            line(pory ? "waitmovement(" + c.argument + ")" : "waitmovement " + c.argument);
            movements.append(pory ? "movement " + label + " {\n\t" + actions.join("\n\t") + "\n}\n"
                                    : label + ":\n\t" + actions.join("\n\t") + "\n\tstep_end\n");
        } else if (c.id == "warp") {
            // Coordinates use the existing map,x,y macro overload (WARP_ID_NONE).
            const QString args = c.argument + ", " + QString::number(c.options["x"].toInt()) + ", " + QString::number(c.options["y"].toInt());
            line(pory ? "warp(" + args + ")" : "warp " + args);
            line("waitstate");
        } else if(c.id=="ifflag"||c.id=="ifvar") {
            if(pory) {
                const QString cond=c.id=="ifflag"?"flag("+c.argument+") == "+(c.value=="1"?"true":"false")
                    :"var("+c.argument+") "+c.comparison+" "+c.value;
                line("if ("+cond+") {");++depth;branches.append({{}, {}, false});
            } else {
                QString base=d.label+"_Branch_"+QString::number(++sequence);
                branches.append({base+"_Else",base+"_Done",false});
                if(c.id=="ifflag") {line("checkflag "+c.argument);line("goto_if_eq "+QString(c.value=="1"?"FALSE":"TRUE")+", "+branches.last().alternate);}
                else {
                    static const QMap<QString,QString> inverse={{"==","ne"},{"!=","eq"},{"<","ge"},{">","le"},{"<=","gt"},{">=","lt"}};
                    line("compare "+c.argument+", "+c.value);line("goto_if_"+inverse.value(c.comparison)+" "+branches.last().alternate);
                }
            }
        } else if(c.id=="else") {
            if(branches.isEmpty()) continue;
            auto &branch=branches.last();branch.hasElse=true;
            if(pory) {--depth;line("} else {");++depth;}
            else {line("goto "+branch.done);out+=branch.alternate+":\n";}
        } else if(c.id=="endif") {
            if(branches.isEmpty()) continue;
            auto branch=branches.takeLast();
            if(pory) {--depth;line("}");}
            else out+=(branch.hasElse?branch.done:branch.alternate)+":\n";
        } else {
            if(c.id=="end"&&(d.lockPlayer || (!d.commands.isEmpty() && d.commands.first().id=="trainerpokemon"))) line("releaseall");
            QStringList args;if(!c.argument.isEmpty()) args.append(c.argument);if(!c.value.isEmpty()) args.append(c.value);
            if(c.id=="playbgm") args.append("0");
            line(c.id+(args.isEmpty()?"":pory?"("+args.join(", ")+")":" "+args.join(", ")));
        }
    }
    if(pory) out+="}\n";
    if(!texts.isEmpty()) out+="\n"+texts.join("\n");
    if(!movements.isEmpty()) out+="\n"+movements.join("\n");
    return out;
}
QByteArray ScriptFile::block(const Document &d) const {
    const QString p=m_pory?"// ":"@ ";
    const auto data=QJsonDocument(d.toJson()).toJson(QJsonDocument::Compact).toBase64();
    return (p+"PORYMAP_VISUAL_BEGIN "+d.label+" v1\n"+p+"PORYMAP_VISUAL_DATA "+QString::fromLatin1(data)+"\n"
            +generate(d)+p+"PORYMAP_VISUAL_END "+d.label+"\n").toUtf8();
}
QString ScriptFile::preview(const Document &d,QString *error) const {
    const QString issue=validate(d);if(error) *error=issue;
    return issue.isEmpty()?normalized(block(d)):QString();
}
bool ScriptFile::prepare(const Document &d,ScriptPatch *patch,QString *error) const {
    const QString issue=validate(d);
    if(!issue.isEmpty()) {if(error) *error=issue;return false;}
    if(!inside(m_root,m_path)) {if(error) *error=QCoreApplication::translate("Porymap", "The script path is outside the project.");return false;}
    patch->file={m_path,m_original,{},m_exists};
    patch->start=m_start;patch->length=m_length;patch->block=block(d);
    if(m_original.contains("\r\n")) patch->block.replace("\n","\r\n");
    return true;
}
bool combinePatches(const QVector<ScriptPatch> &patches,QVector<FileChange> *changes,QString *error) {
    QMap<QString,QVector<ScriptPatch>> groups;
    for(const auto &patch:patches) groups[patch.file.path].append(patch);
    for(auto it=groups.begin();it!=groups.end();++it) {
        auto entries=it.value();const auto base=entries.first().file;
        for(const auto &patch:entries) if(patch.file.before!=base.before||patch.file.existed!=base.existed) {
            if(error) *error=QCoreApplication::translate("Porymap", "Pending scripts use different source versions. Reopen the editor before saving.");
            return false;
        }
        std::stable_sort(entries.begin(),entries.end(),[](const ScriptPatch&a,const ScriptPatch&b){return a.start>b.start;});
        QByteArray output=base.before;int boundary=base.before.size();
        for(const auto &patch:entries) {
            if(patch.start>=0) {
                if(patch.start+patch.length>boundary||patch.length<=0) {if(error) *error=QCoreApplication::translate("Porymap", "Visual script editing ranges overlap.");return false;}
                output.replace(patch.start,patch.length,patch.block);boundary=patch.start;
            } else {
                const QByteArray newline=base.before.contains("\r\n")?"\r\n":"\n";
                if(!output.isEmpty()&&!output.endsWith('\n')) output+=newline;
                output+=newline;output+=patch.block;
            }
        }
        changes->append({base.path,base.before,output,base.existed});
    }
    return true;
}
bool commitChanges(const QString &root,const QVector<FileChange> &input,QString *error,
                   QStringList *backups,const std::function<bool(int)> &permitCommit) {
    auto fail=[&](QString message){if(error) *error=message;return false;};
    QVector<FileChange> changes;QSet<QString> targets;
    for(const auto &change:input) {
        const QString key=QFileInfo(change.path).absoluteFilePath().toLower();
        if(!inside(root,change.path)||targets.contains(key)) return fail(QCoreApplication::translate("Porymap", "The transaction contains an unsafe or duplicate file path."));
        targets.insert(key);changes.append(change);
    }
    auto unchanged=[](const FileChange &c) {bool ok=false;const bool exists=QFileInfo::exists(c.path);
        const auto data=exists?read(c.path,&ok):QByteArray();return exists==c.existed&&(!exists||(ok&&data==c.before));};
    std::vector<std::unique_ptr<QLockFile>> locks;
    // Stable ordering avoids two saves acquiring the same locks in opposite orders.
    QStringList paths;for(const auto &c:changes) paths.append(c.path);paths.sort();
    for(const auto &path:paths) {
        auto lock=std::make_unique<QLockFile>(path+".porymap-visual.lock");
        if(!lock->tryLock(0)) return fail(QCoreApplication::translate("Porymap", "Another visual editor is saving this script."));
        locks.push_back(std::move(lock));
    }
    for(const auto &c:changes) if(!unchanged(c)) return fail(QCoreApplication::translate("Porymap", "The source file changed after opening. Close and reopen the editor before saving."));
    std::vector<std::unique_ptr<QSaveFile>> files;
    QStringList recovery;
    for(const auto &c:changes) {
        if(c.existed) {
            const QString backup=c.path+".porymap-visual-"+QDateTime::currentDateTimeUtc().toString("yyyyMMdd-HHmmsszzz")+"-"+QUuid::createUuid().toString(QUuid::Id128)+".bak";
            if(!QFile::copy(c.path,backup)) return fail(QCoreApplication::translate("Porymap", "Could not create the backup. Nothing was saved."));
            recovery.append(backup);
        }
        auto file=std::make_unique<QSaveFile>(c.path);file->setDirectWriteFallback(false);
        if(!file->open(QIODevice::WriteOnly)||file->write(c.after)!=c.after.size())
            return fail(QCoreApplication::translate("Porymap", "Could not prepare the save. The original files are preserved."));
        files.push_back(std::move(file));
    }
    int committed=0;
    for(int i=0;i<changes.size();++i) {
        if(!inside(root,changes[i].path)||!unchanged(changes[i])||(permitCommit&&!permitCommit(i))||!files[i]->commit()) break;
        ++committed;
    }
    if(committed!=changes.size()) {
        bool restored=true;
        for(int i=committed-1;i>=0;--i) {
            const auto &c=changes[i];
            if(!inside(root,c.path)||read(c.path)!=c.after) {restored=false;continue;}
            if(!c.existed) {if(!QFile::remove(c.path)) restored=false;continue;}
            QSaveFile rollback(c.path);rollback.setDirectWriteFallback(false);
            if(!rollback.open(QIODevice::WriteOnly)||rollback.write(c.before)!=c.before.size()||!rollback.commit()) restored=false;
        }
        if(backups) *backups=recovery;
        return fail(restored?QCoreApplication::translate("Porymap", "Save failed. Original files were restored; your edits remain unsaved.")
                    :QCoreApplication::translate("Porymap", "Save failed and automatic recovery was incomplete. Keep the unsaved edits and recover from these backups: %1").arg(recovery.join("\n")));
    }
    if(backups) *backups=recovery;
    return true;
}
bool ScriptFile::save(const Document &d,QString *error,QString *backupPath) {
    ScriptPatch patch;QVector<FileChange> changes;
    if(!prepare(d,&patch,error)||!combinePatches({patch},&changes,error)) return false;
    QStringList backups;
    if(!commitChanges(m_root,changes,error,&backups)) return false;
    if(backupPath) *backupPath=backups.value(0);
    document=d;return true;
}
}
