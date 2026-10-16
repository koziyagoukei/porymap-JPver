#include "pokemonevent.h"
#include <QCoreApplication>
#include <QDirIterator>
#include <QFile>
#include <QJsonArray>
#include <QRegularExpression>

namespace VisualEvent {
static QString read(const QString &p) { QFile f(p); return f.open(QIODevice::ReadOnly) ? QString::fromUtf8(f.readAll()) : QString(); }
static bool ident(const QString &s) { return QRegularExpression("^[A-Za-z_][A-Za-z_0-9]*$").match(s).hasMatch(); }
static bool teraType(const PokemonEngine &p, const QString &name) {
    if (!ident(name) || !name.startsWith("TYPE_") || name == "TYPE_NONE" || name == "TYPE_MYSTERY") return false;
    // Verify literal enum/define values in the target type table; do not invent a value
    // for an unresolved alias. SetTeraType has no success return to check afterwards.
    const auto match=QRegularExpression("(?:\\b"+name+"\\s*=\\s*|#define[ \\t]+"+name+"[ \\t]+)([0-9]+)\\b").match(p.constants);
    return match.hasMatch() && match.captured(1).toInt()>0 && match.captured(1).toInt()<256;
}
static QString function(const QString &s, const QString &name) {
    const auto m = QRegularExpression("(?:^|\\n)[^;{}\\n]*\\b" + name + "\\([^;{}]*\\)\\s*\\{").match(s);
    if (!m.hasMatch()) return {};
    const int start = m.capturedEnd() - 1;
    int depth = 1, i = start + 1;
    bool quote = false, escape = false, line = false, comment = false;
    for (; i < s.size() && depth; ++i) {
        const auto c = s[i], n = i + 1 < s.size() ? s[i+1] : QChar();
        if (line) { if (c == '\n') line = false; continue; }
        if (comment) { if (c == '*' && n == '/') { comment = false; ++i; } continue; }
        if (quote) { if (!escape && c == '"') quote = false; escape = !escape && c == '\\'; continue; }
        if (c == '"') { quote = true; continue; }
        if (c == '/' && n == '/') { line = true; ++i; continue; }
        if (c == '/' && n == '*') { comment = true; ++i; continue; }
        if (c == '{') ++depth;
        if (c == '}') --depth;
    }
    return depth ? QString() : s.mid(start, i-start);
}
PokemonEngine PokemonEngine::inspect(const QString &root) {
    PokemonEngine p; p.root = root;
    const QString macroText = read(root + "/asm/macros/event.inc");
    auto matches = QRegularExpression("^\\s*\\.macro[ \\t]+(\\w+)([^\\n]*)\\n([\\s\\S]*?)\\.endm", QRegularExpression::MultilineOption).globalMatch(macroText);
    while (matches.hasNext()) { const auto m = matches.next(); p.macros[m.captured(1)] = m.captured(2).trimmed();p.macroBodies[m.captured(1)]=m.captured(3); }
    const auto specialText = read(root + "/data/specials.inc");
    matches = QRegularExpression("def_special[ \\t]+(\\w+)([^\\n]*)").globalMatch(specialText);
    while (matches.hasNext()) { const auto m = matches.next(); p.specials.insert(m.captured(1)); if (m.captured(2).contains("waitstate=1")) p.automaticWait.insert(m.captured(1)); }
    if (!macroText.contains("SPECIAL_WAITSTATE_")) p.automaticWait.clear();
    p.scriptC = read(root + "/src/scrcmd.c"); p.utilityC = read(root + "/src/script_pokemon_util.c");
    p.pokemonC = read(root + "/src/pokemon.c"); p.fieldC = read(root + "/src/field_specials.c");
    p.partyC = read(root + "/src/party_menu.c"); p.battleC = read(root + "/src/battle_setup.c"); p.tvC = read(root + "/src/tv.c");
    p.storageC = read(root + "/src/pokemon_storage_system.c");
    p.constants = read(root + "/include/constants/pokemon.h");
    const auto utility = p.utilityC + p.pokemonC;
    p.wild = p.macros.value("setwildbattle").startsWith("species:req, level:req, item") && p.macros.contains("dowildbattle")
        && function(p.scriptC, "ScrCmd_dowildbattle").contains("BattleSetup_StartScriptedWildBattle")
        && p.specials.contains("GetBattleOutcome") && function(p.fieldC, "GetBattleOutcome").contains("gBattleOutcome")
        && p.specials.contains("CountPartyAliveNonEggMons_IgnoreVar0x8004Slot")
        && function(p.storageC,"CountPartyAliveNonEggMons_IgnoreVar0x8004Slot").contains("CountPartyAliveNonEggMonsExcept(gSpecialVar_0x8004)")
        && function(p.storageC,"CountPartyAliveNonEggMonsExcept").contains("MON_DATA_HP") && function(p.storageC,"CountPartyAliveNonEggMonsExcept").contains("MON_DATA_IS_EGG");
    p.trainer = p.macros.value("trainerbattle_single").startsWith("trainer:req, intro_text:req, lose_text:req, event_script=")
        && p.macros.value("trainerbattle_double").startsWith("trainer:req, intro_text:req, lose_text:req, not_enough_pkmn_text:req")
        && !function(p.battleC, "GetTrainerFlagFromScriptPointer").isEmpty()
        && p.battleC.contains("HasEnoughMonsForDoubleBattle") && p.macros.contains("checktrainerflag");
    const auto healer = function(utility, "HealPlayerParty");
    p.heal = p.specials.contains("HealPlayerParty") && (healer.contains("HealPokemon") || (healer.contains("MON_DATA_HP") && healer.contains("MON_DATA_STATUS") && healer.contains("MON_DATA_PP")));
    const auto giftArgs = p.macros.value("givemon");
    if (giftArgs.startsWith("species:req, level:req, item, ball, nature, abilityNum, gender, hpEv") && giftArgs.contains("teraType, dmaxLevel, isEgg")) p.giftDialect = "expanded";
    else if (giftArgs == "species:req, level:req, item:req, unknown1:req, unknown2:req, unknown3:req") p.giftDialect = "standard";
    const auto storage = function(p.pokemonC, "CopyMonToPC");
    const bool storageSafe = storage.contains("return MON_CANT_GIVE") && storage.contains("return MON_GIVEN_TO_PC");
    const auto giver = function(p.pokemonC, "GiveScriptedMonToPlayer");
    const auto legacyGiver = function(utility, "ScriptGiveMon");
    p.gift = !p.giftDialect.isEmpty() && storageSafe &&
        ((p.giftDialect == "expanded" && p.macros.contains("callnative") && p.macroBodies.value("givemon").contains("callnative ScrCmd_createmon") && p.macroBodies.value("givemon").contains("PARTY_SIZE") && function(p.utilityC, "ScrCmd_createmon").contains("ScriptGiveMonParameterized") && giver.contains("CopyMonToPC") && giver.contains("MON_CANT_GIVE"))
        || (p.giftDialect == "standard" && function(p.scriptC, "ScrCmd_givemon").contains("ScriptGiveMon") && legacyGiver.contains("GiveMonToPlayer")));
    p.egg = p.macros.value("giveegg") == "species:req" && function(p.scriptC, "ScrCmd_giveegg").contains("gSpecialVar_Result = ScriptGiveEgg")
        && function(utility, "ScriptGiveEgg").contains("CreateEgg") && function(utility, "ScriptGiveEgg").contains("GiveCapturedMonToPlayer") && storageSafe;
    p.party = p.macros.contains("getpartysize") && p.specials.contains("ScriptGetPartyMonSpecies")
        && function(p.fieldC, "ScriptGetPartyMonSpecies").contains("MON_DATA_SPECIES_OR_EGG")
        && p.specials.contains("ChoosePartyMon") && function(p.partyC, "BufferMonSelection").contains("gSpecialVar_0x8004");
    p.partyStatus = p.party && p.macros.contains("callnative") && p.macros.value("setstatus1") == "status1:req, slot:req" && p.macroBodies.value("setstatus1").contains("callnative Script_SetStatus1") && function(p.utilityC,"Script_SetStatus1").contains("MON_DATA_STATUS") && function(p.utilityC,"Script_SetStatus1").contains("ScriptReadHalfword");
    p.partyTera = p.party && p.macros.contains("callnative") && p.macros.value("setteratype") == "type:req, slot:req" && p.macroBodies.value("setteratype").contains("callnative SetTeraType") && function(p.utilityC,"SetTeraType").contains("MON_DATA_TERA_TYPE") && function(p.utilityC,"SetTeraType").contains("ScriptReadByte");
    p.nickname = p.gift && p.giftDialect == "expanded" && p.specials.contains("ChangePokemonNickname")
        && function(p.tvC, "ChangePokemonNickname").contains("ChangePokemonNicknameWithCallback")
        && function(p.pokemonC, "GetSelectedBoxMonFromPcOrParty").contains("PC_MON_CHOSEN")
        && function(p.pokemonC, "GetSelectedBoxMonFromPcOrParty").contains("gSpecialVar_MonBoxPos")
        && storage.contains("gSpecialVar_MonBoxId = boxNo") && storage.contains("gSpecialVar_MonBoxPos = boxPos");
    return p;
}
bool isPokemonCommand(const QString &id) { return QStringList{"wildpokemon", "trainerpokemon", "healpokemon", "giftpokemon", "eggPokemon", "partypokemon", "trainerstatus"}.contains(id); }
QString PokemonEngine::unavailable(const QString &id) const {
    for (const auto &macro : {"compare", "copyvar", "setvar", "goto", "goto_if_eq", "goto_if_ne", "getpartysize", "msgbox", "special", "specialvar"})
        if (isPokemonCommand(id) && !macros.contains(macro)) return QCoreApplication::translate("PokemonEvent", "A required engine macro is unavailable: %1").arg(macro);
    const bool ok = id == "wildpokemon" ? wild : id == "trainerpokemon" || id == "trainerstatus" ? trainer : id == "healpokemon" ? heal : id == "giftpokemon" ? gift : id == "eggPokemon" ? egg : id == "partypokemon" ? party : true;
    return ok ? QString() : QCoreApplication::translate("PokemonEvent", "The required macro signature, special or C implementation could not be verified for: %1").arg(commandName(id));
}
QString pokemonCategory(const QString &id) {
    if (id == "wildpokemon" || id == "trainerpokemon" || id == "trainerstatus") return QCoreApplication::translate("PokemonEvent", "Battles");
    if (id == "healpokemon") return QCoreApplication::translate("PokemonEvent", "Pokemon Healing");
    if (id == "giftpokemon" || id == "eggPokemon") return QCoreApplication::translate("PokemonEvent", "Pokemon Gifts");
    return QCoreApplication::translate("PokemonEvent", "Party Pokemon");
}
Command pokemonCommand(const QString &id, const PokemonEngine *engine) {
    Command c; c.id = id;
    const QString dialect = engine ? engine->giftDialect : "standard";
    if (id == "wildpokemon") c.options = {{"species", "SPECIES_POOCHYENA"}, {"level", 5}, {"item", "ITEM_NONE"}, {"result", "VAR_0x8008"}, {"before", ""}, {"win", ""}, {"caught", ""}, {"escaped", ""}};
    if (id == "trainerpokemon") c.options = {{"trainer", "TRAINER_CALVIN_1"}, {"double", false}, {"rematch", false}, {"intro", ""}, {"defeat", ""}, {"insufficient", ""}, {"victory", ""}, {"beaten", ""}, {"result", "VAR_0x8008"}};
    if (id == "trainerstatus") c.options = {{"trainer", "TRAINER_CALVIN_1"}, {"result", "VAR_0x8008"}};
    if (id == "healpokemon") c.options = {{"center", false}, {"message", ""}};
    if (id == "giftpokemon") c.options = {{"engine", dialect}, {"species", "SPECIES_BULBASAUR"}, {"level", 5}, {"item", "ITEM_NONE"}, {"result", "VAR_0x8008"}, {"flag", ""}, {"nickname", ""}, {"message", ""}, {"failure", ""}, {"details", QJsonObject{}}, {"waitNickname", engine ? !engine->automaticWait.contains("ChangePokemonNickname") : true}};
    if (id == "eggPokemon") c.options = {{"species", "SPECIES_TOGEPI"}, {"result", "VAR_0x8008"}, {"flag", ""}, {"message", ""}, {"failure", ""}};
    if (id == "partypokemon") c.options = {{"target", "select"}, {"slot", 0}, {"species", "SPECIES_BULBASAUR"}, {"operation", "species"}, {"move", "MOVE_TACKLE"}, {"moveSlot", 0}, {"teraType", "TYPE_NORMAL"}, {"result", "VAR_0x8008"}, {"slotResult", "VAR_0x8009"}, {"waitSelect", engine ? !engine->automaticWait.contains("ChoosePartyMon") : true}};
    return c;
}
SpeciesDetails speciesDetails(const QString &root, const QString &species) {
    SpeciesDetails result;
    QDirIterator files(root + "/src/data/pokemon", {"*.h"}, QDir::Files, QDirIterator::Subdirectories);
    const QRegularExpression entry("\\[" + QRegularExpression::escape(species) + "\\]\\s*=\\s*\\{");
    while (files.hasNext()) {
        const auto text = read(files.next()); const auto found = entry.match(text); if (!found.hasMatch()) continue;
        const int next = text.indexOf(QRegularExpression("\\n\\s*\\[SPECIES_"), found.capturedEnd());
        const auto block = text.mid(found.capturedEnd(), next < 0 ? -1 : next-found.capturedEnd());
        auto match = QRegularExpression("\\.speciesName\\s*=\\s*_\\(\"([^\"]+)\"\\)").match(block);
        if (match.hasMatch()) { auto name = match.captured(1); name.remove("{JPN}"); if (!name.contains('\\') && !name.contains(QChar::ReplacementCharacter)) result.name = name; }
        match = QRegularExpression("\\.genderRatio\\s*=\\s*([^,\\n]+)").match(block); result.gender = match.captured(1).trimmed();result.gender.remove(QRegularExpression("\\s+"));
        match = QRegularExpression("\\.abilities\\s*=\\s*\\{([^}]+)\\}").match(block);
        if (match.hasMatch()) { result.abilities = match.captured(1).split(','); for (auto &ability : result.abilities) ability = ability.trimmed(); }
        result.gigantamax = block.contains("gigantamaxSpecies") || read(root + "/include/constants/species.h").contains(species + "_GMAX");
        return result;
    }
    return result;
}
QStringList pokemonTexts(const Command &c) {
    QStringList texts;
    for (const auto &key : {"before", "win", "caught", "escaped", "intro", "defeat", "insufficient", "victory", "beaten", "message", "failure", "nickname"})
        if (c.options[key].isString() && !c.options[key].toString().isEmpty()) texts.append(c.options[key].toString());
    return texts;
}
QMap<QString, QString> pokemonDisplayNames(const QString &root, Field field) {
    QMap<QString, QString> names;
    if (field != Field::Species && field != Field::Move && field != Field::Ability) return names;
    const QString prefix = field == Field::Species ? "SPECIES_" : field == Field::Move ? "MOVE_" : "ABILITY_";
    const QString member = field == Field::Species ? "speciesName" : "name";
    QStringList paths;
    if (field == Field::Species) { QDirIterator it(root+"/src/data/pokemon",{"*.h"},QDir::Files,QDirIterator::Subdirectories); while(it.hasNext())paths.append(it.next()); }
    else paths = field == Field::Move ? QStringList{root+"/src/data/moves_info.h"} : QStringList{root+"/src/data/abilities.h",root+"/src/data/text/abilities.h"};
    const QRegularExpression entry("\\[("+prefix+"[A-Za-z_0-9]+)\\]\\s*=\\s*\\{");
    const QRegularExpression display("\\."+member+"\\s*=\\s*(?:_|COMPOUND_STRING)\\(\"([^\"]+)\"\\)");
    for(const auto &path:paths) {
        const auto text=read(path); auto it=entry.globalMatch(text); QVector<QRegularExpressionMatch> entries;
        while(it.hasNext())entries.append(it.next());
        for(int i=0;i<entries.size();++i) {
            const int start=entries[i].capturedEnd(),end=i+1<entries.size()?entries[i+1].capturedStart():text.size();
            const auto match=display.match(text.mid(start,end-start));if(!match.hasMatch())continue;
            auto name=match.captured(1);name.remove("{JPN}");
            if(!name.contains('\\')&&!name.contains(QChar::ReplacementCharacter)&&!name.contains('{'))names[entries[i].captured(1)]=name;
        }
    }
    return names;
}
QString validatePokemon(const Command &c, const PokemonEngine &p) {
    auto error = p.unavailable(c.id); if (!error.isEmpty()) return error;
    const auto defaults = pokemonCommand(c.id, &p).options;
    if (!c.argument.isEmpty() || !c.value.isEmpty() || !c.text.isEmpty() || c.options.keys() != defaults.keys()) return QCoreApplication::translate("PokemonEvent", "Invalid Pokemon command data.");
    for (auto it = defaults.begin(); it != defaults.end(); ++it) if (it.value().type() != c.options[it.key()].type()) return QCoreApplication::translate("PokemonEvent", "Invalid Pokemon command data.");
    const auto o = c.options;
    if (o["species"].toString() == "SPECIES_NONE" || o["species"].toString() == "SPECIES_EGG") return QCoreApplication::translate("PokemonEvent", "Choose an actual Pokemon species, not NONE or EGG.");
    if(o["trainer"].toString()=="TRAINER_NONE")return QCoreApplication::translate("PokemonEvent", "Choose an actual trainer ID, not TRAINER_NONE.");
    for (auto it = o.begin(); it != o.end(); ++it) if (QStringList{"species", "item", "trainer", "move"}.contains(it.key()) && !ident(it.value().toString())) return QCoreApplication::translate("PokemonEvent", "Enter a constant identifier for species, item, trainer or move.");
    if (o.contains("level") && (o["level"].toDouble() != o["level"].toInt() || o["level"].toInt() < 1 || o["level"].toInt() > 100)) return QCoreApplication::translate("PokemonEvent", "The level must be an integer from 1 to 100.");
    if (o.contains("result") && !QStringList{"VAR_0x8008", "VAR_0x8009", "VAR_0x800A", "VAR_0x800B"}.contains(o["result"].toString())) return QCoreApplication::translate("PokemonEvent", "Choose a result variable from VAR_0x8008 to VAR_0x800B.");
    if (o.contains("flag") && !o["flag"].toString().isEmpty() && (!ident(o["flag"].toString()) || !o["flag"].toString().startsWith("FLAG_") || o["flag"].toString().startsWith("FLAG_TEMP_"))) return QCoreApplication::translate("PokemonEvent", "Choose an unused persistent FLAG_ constant.");
    if (c.id == "healpokemon" && o["center"].toBool()) for (const auto &macro : {"fadescreen", "playfanfare", "waitfanfare"}) if (!p.macros.contains(macro)) return QCoreApplication::translate("PokemonEvent", "The healing animation macros are unavailable.");
    if (c.id == "trainerpokemon") {
        if (o["intro"].toString().isEmpty() || o["defeat"].toString().isEmpty() || (o["double"].toBool() && o["insufficient"].toString().isEmpty())) return QCoreApplication::translate("PokemonEvent", "Enter the trainer's intro, defeat dialogue and, for doubles, the insufficient-party dialogue.");
        if (o["rematch"].toBool() && !p.macros.contains(o["double"].toBool() ? "trainerbattle_rematch_double" : "trainerbattle_rematch")) return QCoreApplication::translate("PokemonEvent", "This project's rematch macro is unavailable.");
        if (o["rematch"].toBool() && !o["victory"].toString().isEmpty()) return QCoreApplication::translate("PokemonEvent", "Official rematches use the engine's rematch table. Distinct first-victory dialogue is only available for normal trainer battles.");
    }
    if (c.id == "giftpokemon") {
        if (o["engine"].toString() != p.giftDialect) return QCoreApplication::translate("PokemonEvent", "The project's givemon signature changed. Reconfigure this Pokemon command.");
        if (!o["nickname"].toString().isEmpty() && (!p.nickname || o["waitNickname"].toBool() == p.automaticWait.contains("ChangePokemonNickname"))) return QCoreApplication::translate("PokemonEvent", "The safe nickname workflow is unavailable or its wait behavior changed.");
        const auto details = o["details"].toObject();
        if (!details.isEmpty() && p.giftDialect != "expanded") return QCoreApplication::translate("PokemonEvent", "Detailed Pokemon creation requires a verified extended givemon implementation.");
        const QSet<QString> parameters{"ball", "nature", "abilityNum", "gender", "shinyMode", "gmaxFactor", "teraType", "dmaxLevel", "hpIv", "atkIv", "defIv", "speedIv", "spAtkIv", "spDefIv", "hpEv", "atkEv", "defEv", "speedEv", "spAtkEv", "spDefEv", "move1", "move2", "move3", "move4"};
        int total = 0;
        for (auto it = details.begin(); it != details.end(); ++it) {
            if (!parameters.contains(it.key()) || !it.value().isString() || !QRegularExpression("^(?:[A-Za-z_][A-Za-z_0-9]*|[0-9]+)$").match(it.value().toString()).hasMatch()) return QCoreApplication::translate("PokemonEvent", "Invalid detailed Pokemon parameter.");
            if (it.key() == "shinyMode" && it.value().toString() != "0" && it.value().toString() != "1") return QCoreApplication::translate("PokemonEvent", "This engine's shiny override accepts only 0 or 1; leave it blank for normal generation.");
            const auto value=it.value().toString();
            const QString requiredPrefix=it.key()=="ball"?"BALL_":it.key()=="nature"?"NATURE_":it.key()=="teraType"?"TYPE_":it.key().startsWith("move")?"MOVE_":"";
            if(!requiredPrefix.isEmpty()&&!value.startsWith(requiredPrefix))return QCoreApplication::translate("PokemonEvent", "Use the matching BALL_, NATURE_, TYPE_ or MOVE_ constant for this field.");
            if(it.key()=="teraType"&&!teraType(p,value))return QCoreApplication::translate("PokemonEvent", "Choose a usable Tera type.");
            if (it.key().endsWith("Iv") || it.key().endsWith("Ev") || QStringList{"abilityNum", "dmaxLevel", "gmaxFactor"}.contains(it.key())) {
                bool ok; const int n = it.value().toString().toInt(&ok); const int max = it.key().endsWith("Iv") ? 31 : it.key().endsWith("Ev") ? 252 : it.key() == "abilityNum" ? 2 : it.key() == "gmaxFactor" ? 1 : 10;
                if (!ok || n < 0 || n > max) return QCoreApplication::translate("PokemonEvent", "IV, EV, ability slot or Dynamax values are out of range.");
                if (it.key().endsWith("Ev")) total += n;
            }
        }
        if (total > 510) return QCoreApplication::translate("PokemonEvent", "Total EVs must not exceed 510.");
        const auto info = speciesDetails(p.root, o["species"].toString());
        if (details.contains("abilityNum")) {
            const int slot = details["abilityNum"].toString().toInt();
            if (slot >= info.abilities.size() || info.abilities[slot] == "ABILITY_NONE" || !info.abilities[slot].startsWith("ABILITY_") || !ident(info.abilities[slot])) return QCoreApplication::translate("PokemonEvent", "This ability slot is unavailable or could not be verified for the species.");
        }
        if (details.contains("gender")) {
            const auto gender = details["gender"].toString();
            const auto ratio=QRegularExpression("^PERCENT_FEMALE\\(([0-9]+(?:\\.[0-9]+)?)\\)$").match(info.gender);
            const bool knownGender=QStringList{"MON_MALE","MON_FEMALE","MON_GENDERLESS"}.contains(info.gender)||(ratio.hasMatch()&&ratio.captured(1).toDouble()<=100);
            const bool maleOnly=info.gender=="MON_MALE"||(ratio.hasMatch()&&ratio.captured(1).toDouble()==0);
            const bool femaleOnly=info.gender=="MON_FEMALE"||(ratio.hasMatch()&&ratio.captured(1).toDouble()==100);
            if (!QStringList{"MON_MALE", "MON_FEMALE", "MON_GENDERLESS"}.contains(gender) || !knownGender
                || (info.gender == "MON_GENDERLESS" && gender != "MON_GENDERLESS") || (maleOnly && gender != "MON_MALE") || (femaleOnly && gender != "MON_FEMALE") || (ratio.hasMatch() && gender == "MON_GENDERLESS")) return QCoreApplication::translate("PokemonEvent", "The selected gender is incompatible with the species or could not be verified.");
        }
        if (details.value("gmaxFactor").toString() == "1" && !info.gigantamax) return QCoreApplication::translate("PokemonEvent", "Gigantamax eligibility could not be verified for this species.");
    }
    if (c.id == "partypokemon") {
        if (!QStringList{"first", "slot", "select", "find"}.contains(o["target"].toString()) || o["slot"].toDouble() != o["slot"].toInt() || o["slot"].toInt() < 0 || o["slot"].toInt() > 5
            || !QStringList{"species", "move", "status", "tera"}.contains(o["operation"].toString()) || o["moveSlot"].toDouble() != o["moveSlot"].toInt() || o["moveSlot"].toInt() < 0 || o["moveSlot"].toInt() > 3
            || o["slotResult"].toString() != "VAR_0x8009" || o["result"].toString() == "VAR_0x8009") return QCoreApplication::translate("PokemonEvent", "Choose a valid party target, operation, move slot and distinct result variables.");
        if (o["target"].toString() == "select" && o["waitSelect"].toBool() == p.automaticWait.contains("ChoosePartyMon")) return QCoreApplication::translate("PokemonEvent", "The party selector's wait behavior changed. Reconfigure this command.");
        if (o["operation"].toString() == "move" && (p.macros.value("setmonmove") != "partyIndex:req, slot:req, move:req" || !function(p.utilityC + p.pokemonC, "ScriptSetMonMoveSlot").contains("SetMonMoveSlot"))) return QCoreApplication::translate("PokemonEvent", "The safe move replacement implementation is unavailable.");
        if ((o["operation"].toString()=="status"&&!p.partyStatus)||(o["operation"].toString()=="tera"&&!p.partyTera)) return QCoreApplication::translate("PokemonEvent", "The selected party operation has no verified engine implementation.");
        if(o["operation"].toString()=="tera"&&!teraType(p,o["teraType"].toString()))return QCoreApplication::translate("PokemonEvent", "Choose a usable Tera type.");
    }
    return {};
}
QString generatePokemon(const Command &c, const QString &prefix, QStringList *texts) {
    const auto o = c.options; QString out;
    auto line = [&](const QString &s) { out += "\t" + s + "\n"; };
    int sequence = 0;
    auto textLabel = [&](const QString &text) { const QString label = prefix + "_Text_" + QString::number(++sequence); auto escaped = text; escaped.replace('"', "\\\"").replace('\n', "\\n"); texts->append(label + ":\n\t.string \"" + escaped + "$\"\n"); return label; };
    auto message = [&](const QString &key) { if (!o[key].toString().isEmpty()) line("msgbox " + textLabel(o[key].toString()) + ", MSGBOX_DEFAULT"); };
    const QString result = o["result"].toString();
    if (c.id == "wildpokemon") {
        line("setvar VAR_0x8004, PARTY_SIZE");line("specialvar VAR_RESULT, CountPartyAliveNonEggMons_IgnoreVar0x8004Slot");line("compare VAR_RESULT, 0");line("goto_if_eq "+prefix+"_NoParty");
        message("before"); line("setwildbattle " + o["species"].toString() + ", " + QString::number(o["level"].toInt()) + ", " + o["item"].toString()); line("dowildbattle"); line("specialvar " + result + ", GetBattleOutcome");
        for (const auto &pair : QList<QPair<QString,QString>>{{"win","B_OUTCOME_WON"},{"caught","B_OUTCOME_CAUGHT"},{"escaped","B_OUTCOME_RAN"}}) if (!o[pair.first].toString().isEmpty()) {
            const QString skip = prefix + "_Skip_" + pair.first; line("compare " + result + ", " + pair.second); line("goto_if_ne " + skip); message(pair.first); out += skip + ":\n";
        }
        line("goto "+prefix+"_Done");out+=prefix+"_NoParty:\n";line("setvar "+result+", 0");out+=prefix+"_Done:\n";
    } else if (c.id == "trainerpokemon") {
        QString macro = o["rematch"].toBool() ? "trainerbattle_rematch" : "trainerbattle";
        macro += o["double"].toBool() ? "_double" : o["rematch"].toBool() ? "" : "_single";
        QString args = o["trainer"].toString() + ", " + textLabel(o["intro"].toString()) + ", " + textLabel(o["defeat"].toString());
        if (o["double"].toBool()) args += ", " + textLabel(o["insufficient"].toString());
        // A separate native entry distinguishes first victory from later conversations.
        if (!o["rematch"].toBool()) args += ", " + prefix + "_Won";
        line(macro + " " + args);
        line("setvar " + result + ", " + (o["rematch"].toBool() ? "2" : "0"));message("beaten"); line("goto " + prefix + "_Post");
        out += prefix + "_Won:\n";line("setvar " + result + ", 1"); message("victory"); out += prefix + "_Post:\n";
    } else if (c.id == "trainerstatus") {
        line("checktrainerflag " + o["trainer"].toString()); line("copyvar " + result + ", VAR_RESULT");
    } else if (c.id == "healpokemon") {
        if (o["center"].toBool()) { line("fadescreen FADE_TO_BLACK"); line("playfanfare MUS_HEAL"); }
        line("special HealPlayerParty");
        if (o["center"].toBool()) { line("waitfanfare"); line("fadescreen FADE_FROM_BLACK"); }
        message("message");
    } else if (c.id == "giftpokemon" || c.id == "eggPokemon") {
        const QString done = prefix + "_Done", fail = prefix + "_Fail", receive = prefix + "_Receive";
        if (!o["flag"].toString().isEmpty()) { line("setvar " + result + ", 3"); line("checkflag " + o["flag"].toString()); line("goto_if_eq TRUE, " + done); }
        line("getpartysize"); line("copyvar VAR_0x8007, VAR_RESULT");
        if (c.id == "eggPokemon") line("giveegg " + o["species"].toString());
        else {
            QString args = o["species"].toString() + ", " + QString::number(o["level"].toInt()) + ", " + o["item"].toString();
            if (o["engine"].toString() == "standard") args += ", 0, 0, 0";
            else {
                auto details = o["details"].toObject();
                if (!details.contains("nature")) details["nature"] = "NATURE_RANDOM";
                if (!details.contains("gender")) details["gender"] = "MON_GENDER_RANDOM";
                for (auto it = details.begin(); it != details.end(); ++it) args += ", " + it.key() + "=" + it.value().toString();
            }
            line("givemon " + args);
        }
        line("copyvar " + result + ", VAR_RESULT"); line("compare " + result + ", MON_CANT_GIVE"); line("goto_if_eq " + fail);
        if (!o["flag"].toString().isEmpty()) line("setflag " + o["flag"].toString());
        if (c.id == "giftpokemon" && !o["nickname"].toString().isEmpty()) {
            line("compare " + result + ", MON_GIVEN_TO_PC"); line("goto_if_eq " + prefix + "_Pc"); line("copyvar VAR_0x8004, VAR_0x8007"); line("goto " + receive);
            out += prefix + "_Pc:\n"; line("setvar VAR_0x8004, PC_MON_CHOSEN"); out += receive + ":\n";
            line("msgbox " + textLabel(o["nickname"].toString()) + ", MSGBOX_YESNO"); line("compare VAR_RESULT, 1"); line("goto_if_ne " + prefix + "_Named"); line("special ChangePokemonNickname"); if (o["waitNickname"].toBool()) line("waitstate"); out += prefix + "_Named:\n";
        }
        message("message"); line("goto " + done); out += fail + ":\n"; message("failure"); out += done + ":\n";
    } else if (c.id == "partypokemon") {
        const QString fail = prefix + "_Invalid", done = prefix + "_Done", selected = prefix + "_Selected";
        line("getpartysize"); line("compare VAR_RESULT, 0"); line("goto_if_eq " + fail);
        const QString target = o["target"].toString(), slotResult = o["slotResult"].toString();
        if (target == "select") { line("special ChoosePartyMon"); if (o["waitSelect"].toBool()) line("waitstate"); line("copyvar " + slotResult + ", VAR_0x8004"); }
        else if (target == "find") {
            for (int slot = 0; slot < 6; ++slot) {
                const QString next = prefix + "_Find_" + QString::number(slot);
                line("getpartysize"); line("compare VAR_RESULT, " + QString::number(slot)); line("goto_if_le " + fail); line("setvar VAR_0x8004, " + QString::number(slot)); line("specialvar VAR_RESULT, ScriptGetPartyMonSpecies"); line("compare VAR_RESULT, " + o["species"].toString()); line("goto_if_ne " + next); line("setvar " + slotResult + ", " + QString::number(slot)); line("goto " + selected); out += next + ":\n";
            }
            line("goto " + fail);
        } else line("setvar " + slotResult + ", " + QString::number(target == "first" ? 0 : o["slot"].toInt()));
        out += selected + ":\n"; line("getpartysize"); line("compare " + slotResult + ", VAR_RESULT"); line("goto_if_ge " + fail);
        line("copyvar VAR_0x8004, " + slotResult); line("specialvar " + result + ", ScriptGetPartyMonSpecies"); line("compare " + result + ", SPECIES_EGG"); line("goto_if_eq " + fail); line("compare " + result + ", SPECIES_NONE"); line("goto_if_eq " + fail);
        if (o["operation"].toString() == "move") {
            // setmonmove reads a byte, not VarGet. Dispatch only to checked literal slots.
            for (int slot = 0; slot < 6; ++slot) { const auto next = prefix + "_Move_" + QString::number(slot); line("compare " + slotResult + ", " + QString::number(slot)); line("goto_if_ne " + next); line("setmonmove " + QString::number(slot) + ", " + QString::number(o["moveSlot"].toInt()) + ", " + o["move"].toString()); line("setvar " + result + ", 1"); line("goto " + done); out += next + ":\n"; }
        } else {
            if(o["operation"].toString()=="status"){line("setstatus1 STATUS1_NONE, "+slotResult);line("setvar "+result+", 1");}
            if(o["operation"].toString()=="tera"){line("setteratype "+o["teraType"].toString()+", "+slotResult);line("setvar "+result+", 1");}
            line("goto " + done);
        }
        out += fail + ":\n"; line("setvar " + slotResult + ", 6"); line("setvar " + result + ", 0"); out += done + ":\n";
    }
    return out;
}
}
