#include "visualeventscript.h"

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
    default: break;
    }
    if (spec->second != Field::None) c.value = "1";
    return c;
}
QString summary(const Command &c) {
    QString detail = c.text.isEmpty() ? c.argument : QString(c.text).replace('\n', " / ");
    if (c.id == "ifvar") detail += " " + c.comparison;
    if (!c.value.isEmpty()) detail += " " + (c.id=="ifflag" ? (c.value=="1"?"ON":"OFF") : c.value);
    return commandName(c.id) + (detail.isEmpty() ? "" : ": " + detail.left(100));
}
QJsonObject Document::toJson() const {
    QJsonArray list;
    for (const auto &c : commands)
        list.append(QJsonObject{{"id", c.id}, {"argument", c.argument}, {"value", c.value},
                                {"text", c.text}, {"comparison", c.comparison}});
    return {{"version", 1}, {"label", label}, {"lockPlayer", lockPlayer}, {"commands", list}};
}
bool Document::fromJson(const QJsonObject &o, Document *out) {
    const QSet<QString> keys = {"version", "label", "lockPlayer", "commands"};
    for (auto it=o.begin(); it!=o.end(); ++it) if (!keys.contains(it.key())) return false;
    if (o["version"].toInt() != 1 || !o["label"].isString() || !o["lockPlayer"].isBool()
        || !o["commands"].isArray() || o["commands"].toArray().size() > 1000) return false;
    Document d; d.label = o["label"].toString(); d.lockPlayer = o["lockPlayer"].toBool();
    for (const auto &v : o["commands"].toArray()) {
        if (!v.isObject()) return false;
        auto a = v.toObject();
        const QSet<QString> fields = {"id", "argument", "value", "text", "comparison"};
        if (a.size() != fields.size()) return false;
        for (const auto &key : fields) if (!a[key].isString()) return false;
        if (!commandSpec(a["id"].toString())) return false;
        d.commands.append({a["id"].toString(), a["argument"].toString(), a["value"].toString(),
                           a["text"].toString(), a["comparison"].toString()});
    }
    *out = d;
    return true;
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
    if(!identifier(d.label) || (!m_originalLabel.isEmpty()&&d.label!=m_originalLabel)) return QCoreApplication::translate("Porymap", "Use a valid script label. Existing labels cannot be renamed here.");
    if(d.commands.isEmpty()||d.commands.size()>1000||d.commands.last().id!="end") return QCoreApplication::translate("Porymap", "The last command must be End Script (maximum 1000 commands).");
    QString scanError;const auto used=otherLabels(&scanError);if(!scanError.isEmpty()) return scanError;
    // Reserve a namespace for local labels emitted by either backend/Poryscript.
    for(const auto &label:used) if(label==d.label||label.startsWith(d.label+"_")) return QCoreApplication::translate("Porymap", "This label or its generated labels conflict with another script.");
    QVector<bool> conditions;
    for(int i=0;i<d.commands.size();++i) {
        const auto &c=d.commands[i];const auto spec=commandSpec(c.id);
        if(!spec) return QCoreApplication::translate("Porymap", "Unsupported event command.");
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
    if(d.lockPlayer) required.unite({"lockall","releaseall"});
    for(const auto &c:d.commands) {
        if(c.id=="message"||c.id=="question") required.insert("msgbox");
        else if(c.id=="ifflag") {
            required.unite({"checkflag","goto_if_eq","goto"});
            if(m_pory) required.unite({"goto_if_set","goto_if_unset"});
        }
        else if(c.id=="ifvar") required.unite({"compare","goto_if_eq","goto_if_ne","goto_if_lt","goto_if_le","goto_if_gt","goto_if_ge","goto"});
        else if(c.id!="else"&&c.id!="endif") required.insert(c.id);
    }
    for(const auto &name:required) if(!QRegularExpression("\\.macro\\s+"+name+"(?:\\s|$)").match(QString::fromUtf8(macros)).hasMatch())
        return QCoreApplication::translate("Porymap", "A required script command is missing from this project: %1").arg(name);
    return {};
}
static QString quotedText(QString s) {return s.replace('"',"\\\"").replace('\n',"\\n");}
QString ScriptFile::generate(const Document &d) const {
    QString out;QStringList texts;
    struct Branch {QString alternate,done;bool hasElse=false;};QVector<Branch> branches;
    int sequence=0,textIndex=0,depth=1;
    auto line=[&](const QString &s){out+=QString(depth,'\t')+s+"\n";};
    out=m_pory?"script "+d.label+" {\n":d.label+"::\n";
    if(d.lockPlayer) line("lockall");
    for(const auto &c:d.commands) {
        if(c.id=="message"||c.id=="question") {
            QString type=c.id=="question"?"MSGBOX_YESNO":"MSGBOX_DEFAULT";
            if(m_pory) line("msgbox(\""+quotedText(c.text)+"\", "+type+")");
            else {
                QString textLabel=d.label+"_Text_"+QString::number(++textIndex);
                line("msgbox "+textLabel+", "+type);
                texts.append(textLabel+":\n\t.string \""+quotedText(c.text)+"$\"\n");
            }
        } else if(c.id=="ifflag"||c.id=="ifvar") {
            if(m_pory) {
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
            if(m_pory) {--depth;line("} else {");++depth;}
            else {line("goto "+branch.done);out+=branch.alternate+":\n";}
        } else if(c.id=="endif") {
            if(branches.isEmpty()) continue;
            auto branch=branches.takeLast();
            if(m_pory) {--depth;line("}");}
            else out+=(branch.hasElse?branch.done:branch.alternate)+":\n";
        } else {
            if(c.id=="end"&&d.lockPlayer) line("releaseall");
            QStringList args;if(!c.argument.isEmpty()) args.append(c.argument);if(!c.value.isEmpty()) args.append(c.value);
            if(c.id=="playbgm") args.append("0");
            line(c.id+(args.isEmpty()?"":m_pory?"("+args.join(", ")+")":" "+args.join(", ")));
        }
    }
    if(m_pory) out+="}\n";
    if(!texts.isEmpty()) out+="\n"+texts.join("\n");
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
bool ScriptFile::save(const Document &d,QString *error,QString *backupPath) {
    auto fail=[&](const QString &s){if(error) *error=s;return false;};
    const QString issue=validate(d);if(!issue.isEmpty()) return fail(issue);
    if(!inside(m_root,m_path)) return fail(QCoreApplication::translate("Porymap", "The script path is outside the project."));
    QLockFile lock(m_path+".porymap-visual.lock");
    if(!lock.tryLock(0)) return fail(QCoreApplication::translate("Porymap", "Another visual editor is saving this script."));
    bool ok;const auto current=QFileInfo::exists(m_path)?read(m_path,&ok):QByteArray();
    if(QFileInfo::exists(m_path)!=m_exists || (m_exists&&(!ok||current!=m_original)))
        return fail(QCoreApplication::translate("Porymap", "The source file changed after opening. Close and reopen the editor before saving."));
    QByteArray replacement=block(d);
    if(m_original.contains("\r\n")) replacement.replace("\n","\r\n");
    QByteArray output=m_original;
    if(m_start>=0) output.replace(m_start,m_length,replacement);
    else {
        const QByteArray newline=m_original.contains("\r\n")?"\r\n":"\n";
        if(!output.isEmpty()&&!output.endsWith('\n')) output+=newline;
        output+=newline;output+=replacement;
    }
    if(output==m_original) return true;
    QString backup;
    if(m_exists) {
        backup=m_path+".porymap-visual-"+QDateTime::currentDateTimeUtc().toString("yyyyMMdd-HHmmsszzz")+"-"+QUuid::createUuid().toString(QUuid::Id128)+".bak";
        if(!QFile::copy(m_path,backup)) return fail(QCoreApplication::translate("Porymap", "Could not create the backup. Nothing was saved."));
    }
    QSaveFile file(m_path);
    if(!file.open(QIODevice::WriteOnly)||file.write(output)!=output.size())
        return fail(QCoreApplication::translate("Porymap", "Could not save the script atomically. The original file is preserved."));
    // Check again after preparing the temporary file; do not replace a newer edit.
    if(!inside(m_root,m_path)||QFileInfo::exists(m_path)!=m_exists||(m_exists&&read(m_path)!=m_original))
        return fail(QCoreApplication::translate("Porymap", "The source file changed after opening. Close and reopen the editor before saving."));
    if(!file.commit()) return fail(QCoreApplication::translate("Porymap", "Could not save the script atomically. The original file is preserved."));
    if(backupPath) *backupPath=backup;
    document=d;
    return true;
}
}
