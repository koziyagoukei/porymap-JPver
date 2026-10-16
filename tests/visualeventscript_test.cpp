#include "visualeventscript.h"
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonArray>
#include <QTemporaryDir>
#include <QTextStream>
#include <cstdlib>

using namespace VisualEvent;
static int checks=0;
static void check(bool value,const char *message) {
    ++checks;if(value) return;
    QTextStream(stderr)<<"FAIL: "<<message<<Qt::endl;std::exit(1);
}
static QByteArray bytes(const QString &p) {QFile f(p);check(f.open(QIODevice::ReadOnly),"read fixture");return f.readAll();}
static void write(const QString &p,const QByteArray &b) {QDir().mkpath(QFileInfo(p).absolutePath());QFile f(p);check(f.open(QIODevice::WriteOnly),"write fixture");check(f.write(b)==b.size(),"complete fixture write");}
static Command cmd(const QString &id,QString arg={},QString val={},QString text={}) {auto c=defaultCommand(id);c.argument=arg;c.value=val;c.text=text;return c;}
#include "pokemonevent_checks.h"

int main(int argc,char **argv) {
    QCoreApplication app(argc,argv);
    if(argc>2&&QString::fromLocal8Bit(argv[1])=="--inspect"){
        QJsonArray projects;
        for(int i=2;i<argc;++i){const QString root=QString::fromLocal8Bit(argv[i]);const auto engine=PokemonEngine::inspect(root);QJsonObject commands;for(const auto &spec:commandSpecs())if(isPokemonCommand(spec.id))commands[spec.id]=engine.unavailable(spec.id);projects.append(QJsonObject{{"root",root},{"giftDialect",engine.giftDialect},{"commands",commands},{"partyStatus",engine.partyStatus},{"partyTera",engine.partyTera},{"nickname",engine.nickname}});}
        QTextStream(stdout)<<QJsonDocument(projects).toJson(QJsonDocument::Indented);return 0;
    }
    QTemporaryDir fixture(QDir::currentPath()+"/visual-fixture-XXXXXX");check(fixture.isValid(),"temporary fixture");
    const QString root=fixture.path(),inc=root+"/data/maps/Test/scripts.inc",pory=root+"/data/maps/Pory/scripts.pory";
    QByteArray macroBytes;
    for(const auto &s:QStringList{"end","lockall","releaseall","msgbox","checkflag","compare","goto","goto_if_eq","goto_if_ne","goto_if_lt","goto_if_le","goto_if_gt","goto_if_ge","goto_if_set","goto_if_unset","setflag","clearflag","setvar","addvar","subvar","faceplayer","giveitem","delay","playse","playbgm","applymovement","waitmovement","warp","waitstate","removeobject"}) macroBytes+="\t.macro "+s.toUtf8()+"\n\t.endm\n";
    QByteArray charmap="'H' = 01\n'e' = 02\n'l' = 03\n'o' = 04\n' ' = 00\n'!' = 05\n'\\n' = FE\n'\\l' = FA\n'\\p' = FB\nPLAYER = FD 01\n";
    charmap+=QString::fromUtf8("'こ' = 06\n'ん' = 07\n'に' = 08\n'ち' = 09\n'は' = 0A\n").toUtf8();
    if(argc>1) {macroBytes=bytes(QString::fromLocal8Bit(argv[1])+"/asm/macros/event.inc");charmap=bytes(QString::fromLocal8Bit(argv[1])+"/charmap.txt");}
    write(root+"/asm/macros/event.inc",macroBytes);write(root+"/charmap.txt",charmap);
    QByteArray movementBytes;
    for (const auto &direction : QStringList{"down", "up", "left", "right"})
        for (const auto &action : QStringList{"face_", "walk_", "walk_slow_", "walk_fast_"}) movementBytes += "create_movement_action " + action.toUtf8() + direction.toUtf8() + ", 1\n";
    for (const auto &action : QStringList{"delay_1", "delay_2", "delay_4", "delay_8", "delay_16", "step_end"}) movementBytes += "create_movement_action " + action.toUtf8() + ", 1\n";
    if (argc > 1) movementBytes = bytes(QString::fromLocal8Bit(argv[1]) + "/asm/macros/movement.inc");
    write(root + "/asm/macros/movement.inc", movementBytes);
    const QByteArray untouched="\xef\xbb\xbf@ preserve this header\r\nTest_MapScripts::\r\n\t.byte 0\r\n\r\nCustom_Existing::\r\n\tcustom_macro 123\r\n\tend\r\n";
    write(inc,untouched);
    ScriptFile file;check(file.open(root,inc,"",{inc},false),"new inc editor");
    Document d=file.document;d.label="Test_Visual";
    d.commands={cmd("faceplayer"),cmd("message",{},{},QString::fromUtf8("こんにちは\n{PLAYER}!")),
        cmd("question",{},{},"Hello!"),cmd("ifvar","VAR_RESULT","1"),cmd("setflag","FLAG_TEMP_1"),
        cmd("ifflag","FLAG_TEMP_2","0"),cmd("giveitem","ITEM_POTION","1"),cmd("else"),
        cmd("clearflag","FLAG_TEMP_1"),cmd("endif"),cmd("else"),cmd("setvar","VAR_TEMP_1","0"),
        cmd("endif"),cmd("addvar","VAR_TEMP_1","1"),cmd("subvar","VAR_TEMP_1","1"),
        cmd("delay","30"),cmd("playse","SE_SELECT"),cmd("playbgm","MUS_LITTLEROOT"),cmd("end")};
    QString error,backup;const auto preview=file.preview(d,&error);
    check(error.isEmpty()&&!preview.isEmpty(),"valid inc preview");check(bytes(inc)==untouched,"preview is nonmutating");
    check(file.save(d,&error,&backup),qPrintable(error));check(bytes(backup)==untouched,"exact backup bytes");
    const auto firstSaved=bytes(inc);check(firstSaved.startsWith(untouched),"unrelated CRLF/BOM bytes preserved");
    check(!firstSaved.contains("\n")||!QByteArray(firstSaved).replace("\r\n","").contains('\n'),"new block uses original CRLF");
    ScriptFile reload;check(reload.open(root,inc,d.label,{inc},false),qPrintable(reload.reason()));
    check(reload.document.toJson()==d.toJson(),"inc round trip including nested conditions and Japanese");
    d.commands[1].text=QString::fromUtf8("こんにちは!");
    check(reload.save(d,&error,&backup),qPrintable(error));check(bytes(inc).startsWith(untouched),"editing replaces only owned block");
    auto afterEdit=bytes(inc);check(afterEdit.count("PORYMAP_VISUAL_BEGIN Test_Visual v1")==1,"one block after re-edit");
    ScriptFile untouchedReadOnly;check(!untouchedReadOnly.open(root,inc,"Custom_Existing",{inc},false),"unknown existing script read-only");
    check(!untouchedReadOnly.save(d,&error),"unknown script overwrite blocked");check(bytes(inc)==afterEdit,"readonly preserves file");
    ScriptFile stale;check(stale.open(root,inc,d.label,{inc},false),"stale session opened");write(inc,afterEdit+"@ external edit\r\n");
    check(!stale.save(d,&error),"concurrent external modification rejected");check(bytes(inc).endsWith("@ external edit\r\n"),"external edit retained");
    write(inc,afterEdit);
    auto handEdit=afterEdit;handEdit.replace("\tsetflag FLAG_TEMP_1","\tcustom_macro 999");write(inc,handEdit);
    ScriptFile hand;check(!hand.open(root,inc,d.label,{inc},false),"hand-edited owned script read-only");
    check(bytes(inc)==handEdit,"hand-edited code retained");write(inc,afterEdit);
    ScriptFile invalid;check(invalid.open(root,inc,"New_Label",{inc},false),"new label in existing file");
    auto bad=d;bad.label="New_Label";bad.commands[1].text=QString::fromUtf8("漢字😀");
    invalid.preview(bad,&error);check(!error.isEmpty(),"unmapped glyph rejected");
    bad=d;bad.label="New_Label";bad.commands[1].text="Hello$";invalid.preview(bad,&error);check(!error.isEmpty(),"terminator injection rejected");
    bad=d;bad.label="New_Label";bad.commands[1].text=QString(1001,QChar(0x3053));invalid.preview(bad,&error);check(!error.isEmpty(),"encoded text length limited for preproc buffer");
    bad=d;bad.label="New_Label";bad.commands[4].argument="FLAG_TEMP_1\nend";invalid.preview(bad,&error);check(!error.isEmpty(),"operand source injection rejected");
    bad=d;bad.label="New_Label";bad.commands.removeAt(9);invalid.preview(bad,&error);check(!error.isEmpty(),"unclosed nested condition rejected");
    bad=d;bad.label="Custom_Existing";invalid.preview(bad,&error);check(!error.isEmpty(),"existing label collision rejected");
    const QString other=root+"/data/scripts/other.inc";write(other,"New_Label_Text_1:\n\t.string \"Hello$\"\n");
    ScriptFile collision;check(collision.open(root,inc,"New_Label",{inc,other},false),"collision test opened");
    bad=d;bad.label="New_Label";collision.preview(bad,&error);check(!error.isEmpty(),"generated child label collision rejected");
    write(pory,"mapscripts Pory_MapScripts {}\n");
    const QString generated=pory.left(pory.size()-5)+".inc";write(generated,"@ generated; do not edit\n");
    ScriptFile high;check(high.open(root,generated,"Pory_Visual",{generated,pory},true),"pory source preferred over inc");
    check(high.path()==pory,"source path points to pory");
    d.label="Pory_Visual";check(high.save(d,&error,&backup),qPrintable(error));
    check(bytes(generated)=="@ generated; do not edit\n","generated inc untouched");
    ScriptFile highReload;check(highReload.open(root,pory,d.label,{pory,generated},true),qPrintable(highReload.reason()));
    check(highReload.document.toJson()==d.toJson(),"pory round trip");
    const QString missing=root+"/data/maps/Missing/scripts.inc";write(missing,"GeneratedLabel::\n\tend\n");
    ScriptFile noSource;check(!noSource.open(root,missing,"",{missing},true),"missing pory cannot fall back to inc");
    write(missing,"# 1 \"missing.pory\"\nGeneratedLabel::\n\tend\n");
    check(!noSource.open(root,missing,"",{missing},false)&&noSource.isGeneratedSource(),"generated line marker protected without config flag");
    const QString incomplete=root+"/data/maps/Incomplete/scripts.pory";
    write(incomplete,"script Outer {\n");
    ScriptFile partial;check(!partial.open(root,incomplete,"",{incomplete},true),"cannot append inside incomplete pory script");
    write(incomplete,"raw `\n"+highReload.preview(d,&error).toUtf8()+"`\n");
    check(!partial.open(root,incomplete,d.label,{incomplete},true),"owned markers inside raw text are protected");
    write(missing,".if 1\n");check(!noSource.open(root,missing,"",{missing},false),"cannot append inside open assembly conditional");
    const QString newFile=root+"/data/maps/Test/new.inc";ScriptFile create;
    check(create.open(root,newFile,"New_Created",{newFile},false),"new source can be opened without existing bytes");
    auto newDocument=create.document;check(create.save(newDocument,&error),"new source can be saved");
    const QString legacy=root+"/data/maps/Legacy/scripts.inc";write(legacy,QByteArray::fromHex("82b182f182c982bf82cd"));
    ScriptFile legacyFile;check(!legacyFile.open(root,legacy,"",{legacy},false),"Shift JIS source preserved read-only");
    ScriptFile outside;check(!outside.open(root,QDir(root).absoluteFilePath("../outside.inc"),"",{},false),"outside project write rejected");
    // All new commands and templates must survive both native backends independently.
    Command movement = defaultCommand("movement"); movement.argument = "LOCALID_PLAYER";
    QJsonArray route;
    for (const auto &direction : QStringList{"down", "up", "left", "right"})
        for (const auto &speed : QStringList{"slow", "normal", "fast"}) route.append(QJsonObject{{"action", "walk"}, {"direction", direction}, {"speed", speed}, {"count", 2}});
    route.append(QJsonObject{{"action", "face"}, {"direction", "up"}, {"speed", "normal"}, {"count", 1}});
    route.append(QJsonObject{{"action", "wait"}, {"direction", "down"}, {"speed", "normal"}, {"count", 16}});
    movement.options = {{"route", route}};
    Command warp = defaultCommand("warp"); warp.argument = "MAP_LITTLEROOT_TOWN"; warp.options = {{"x", 4}, {"y", 5}};
    const QMap<QString, QSize> mapSizes{{"MAP_LITTLEROOT_TOWN", QSize(30, 30)}};
    QVector<Command> additions{movement, warp, cmd("removeobject", "VAR_LAST_TALKED")};
    for (const auto &id : QStringList{"item", "once", "disappear"}) additions += templateCommands(id, "FLAG_RECEIVED_POTION_OLDALE", "ITEM_POTION", "Hello!", "VAR_LAST_TALKED");
    for (bool isPory : {false, true}) {
        const auto path = root + "/data/maps/Test/extended." + (isPory ? "pory" : "inc");
        ScriptFile extended; check(extended.open(root, path, "Extended_Visual", {path}, isPory), "new extensions fixture opens"); extended.setMapSizes(mapSizes);
        auto extendedDocument = extended.document; extendedDocument.commands = additions; extendedDocument.commands.append(defaultCommand("end")); extendedDocument.objectFlag = "FLAG_RECEIVED_POTION_OLDALE";
        const auto code = extended.preview(extendedDocument, &error); check(error.isEmpty(), qPrintable(error));
        check(code.contains("applymovement") && code.contains("walk_fast_right") && code.contains("waitmovement") && code.contains("delay_16") && code.contains("warp"), "route and warp generated");
        check(code.contains(isPory ? "var(VAR_RESULT) == 1" : "compare VAR_RESULT, 1"), "item template checks bag success");
        check(extended.save(extendedDocument, &error), qPrintable(error));
        ScriptFile reread; check(reread.open(root, path, extendedDocument.label, {path}, isPory), qPrintable(reread.reason())); reread.setMapSizes(mapSizes);
        check(reread.document.toJson() == extendedDocument.toJson(), "all extensions and templates round trip");
        auto invalidRoute = extendedDocument; invalidRoute.commands[0].options["route"] = QJsonArray{QJsonObject{{"action", "wait"}, {"direction", "down"}, {"speed", "normal"}, {"count", 3}}};
        reread.preview(invalidRoute, &error); check(!error.isEmpty(), "unsupported movement duration blocked");
        auto invalidWarp = extendedDocument; invalidWarp.commands[1].options["x"] = 30; reread.preview(invalidWarp, &error); check(!error.isEmpty(), "out of bounds warp blocked");
        invalidWarp.commands[1].argument = "MAP_MISSING"; reread.preview(invalidWarp, &error); check(!error.isEmpty(), "unknown map blocked");
    }
    // A failure committing the final reference file must roll back earlier scripts.
    const QString txScript = root + "/transaction.inc", txMap = root + "/map.json";
    write(txScript, "old script"); write(txMap, "old map");
    QVector<FileChange> tx{{txScript, "old script", "new script", true}, {txMap, "old map", "new map", true}};
    QStringList recovery;
    check(!commitChanges(root, tx, &error, &recovery, [](int index) { return index != 1; }), "map commit failure injected");
    check(bytes(txScript) == "old script" && bytes(txMap) == "old map", "failed save restores both original files");
    check(recovery.size() == 2 && bytes(recovery[0]) == "old script", "transaction recovery backups");
    check(commitChanges(root, tx, &error), "transaction retry succeeds");
    write(txMap, "external map"); check(!commitChanges(root, tx, &error), "external map change blocks transaction"); check(bytes(txMap) == "external map", "external map is preserved");
    QFile::remove(txScript); tx[0].existed = false; tx[0].before.clear(); tx[1].before = "external map";
    check(!commitChanges(root, tx, &error, nullptr, [](int index) { return index != 1; }), "new script commit rolls back"); check(!QFileInfo::exists(txScript), "failed map save removes unreferenced new source");
    const auto mergePath = root + "/data/maps/Test/merged.inc"; write(mergePath, "@ original\n");
    ScriptFile first, second; check(first.open(root, mergePath, "First_Visual", {mergePath}, false) && second.open(root, mergePath, "Second_Visual", {mergePath}, false), "multiple pending labels open");
    ScriptPatch firstPatch, secondPatch;
    check(first.prepare(first.document, &firstPatch, &error) && second.prepare(second.document, &secondPatch, &error), "multiple patches prepared");
    QVector<FileChange> merged; check(combinePatches({firstPatch, secondPatch}, &merged, &error) && commitChanges(root, merged, &error), "multiple labels saved in one source");
    check(bytes(mergePath).contains("First_Visual::") && bytes(mergePath).contains("Second_Visual::"), "both scripts retained");
    // Export real generated sources for the independent Poryscript/preproc/assembler checks.
    QDir().mkpath("results");
    ScriptFile exportInc;check(exportInc.open(root,inc,"Test_Visual",{inc},false),"export fixture opens");
    d.commands[1].text=QString::fromUtf8("こんにちは\n{PLAYER}!\\lHello\\pこんにちは");
    auto exportDocument=d;exportDocument.label="Test_Visual";
    int templateRow = exportDocument.commands.size() - 1;
    for (const auto &id : QStringList{"item", "once", "disappear"})
        for (const auto &command : templateCommands(id, "FLAG_RECEIVED_POTION_OLDALE", "ITEM_POTION", "Hello!", "VAR_LAST_TALKED")) exportDocument.commands.insert(templateRow++, command);
    exportDocument.commands.insert(exportDocument.commands.size() - 1, movement);
    exportDocument.commands.insert(exportDocument.commands.size() - 1, warp);
    exportDocument.commands.insert(exportDocument.commands.size() - 1, cmd("removeobject", "VAR_LAST_TALKED"));
    exportInc.setMapSizes(mapSizes); highReload.setMapSizes(mapSizes);
    write("results/visual.inc",exportInc.preview(exportDocument,&error).toUtf8());check(error.isEmpty(),"export inc validates");
    exportDocument.label = d.label;
    write("results/visual.pory",highReload.preview(exportDocument,&error).toUtf8());check(error.isEmpty(),"export pory validates");
    pokemonEventChecks(root, argc > 1 ? QString::fromLocal8Bit(argv[1]) : QString());
    QTextStream(stdout)<<"PASS: "<<checks<<" visual event protection / generation checks"<<Qt::endl;
    return 0;
}
