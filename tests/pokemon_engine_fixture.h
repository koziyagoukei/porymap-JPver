#ifndef POKEMON_ENGINE_FIXTURE_H
#define POKEMON_ENGINE_FIXTURE_H
#include <functional>
#include <QStringList>
static void seedPokemonEngineFixture(const std::function<void(const QString &,const QByteArray &)> &put,const QByteArray &originalMacros) {
auto macros = originalMacros;
for (const auto &signature : QStringList{"setwildbattle species:req, level:req, item=ITEM_NONE", "dowildbattle", "getpartysize", "callnative", "setvar", "copyvar", "special", "specialvar", "fadescreen", "playfanfare", "waitfanfare", "checktrainerflag trainer:req", "trainerbattle_single trainer:req, intro_text:req, lose_text:req, event_script=FALSE, music=TRUE", "trainerbattle_double trainer:req, intro_text:req, lose_text:req, not_enough_pkmn_text:req, event_script=FALSE, music=TRUE", "trainerbattle_rematch trainer:req, intro_text:req, lose_text:req", "trainerbattle_rematch_double trainer:req, intro_text:req, lose_text:req, not_enough_pkmn_text:req", "givemon species:req, level:req, item, ball, nature, abilityNum, gender, hpEv, atkEv, defEv, speedEv, spAtkEv, spDefEv, hpIv, atkIv, defIv, speedIv, spAtkIv, spDefIv, move1, move2, move3, move4, shinyMode, gmaxFactor, teraType, dmaxLevel, isEgg, ignoreTotalEvCheck = FALSE", "giveegg species:req", "setmonmove partyIndex:req, slot:req, move:req", "setstatus1 status1:req, slot:req", "setteratype type:req, slot:req"}) macros += ".macro " + signature.toUtf8() + "\n.endm\n";
macros.replace("ignoreTotalEvCheck = FALSE\n.endm", "ignoreTotalEvCheck = FALSE\ncallnative ScrCmd_createmon\n.byte PARTY_SIZE\n.endm");
macros.replace(".macro setstatus1 status1:req, slot:req\n.endm", ".macro setstatus1 status1:req, slot:req\ncallnative Script_SetStatus1\n.endm");
macros.replace(".macro setteratype type:req, slot:req\n.endm", ".macro setteratype type:req, slot:req\ncallnative SetTeraType\n.endm");
put("asm/macros/event.inc", macros);
put("data/specials.inc", "def_special HealPlayerParty\ndef_special GetBattleOutcome\ndef_special CountPartyAliveNonEggMons_IgnoreVar0x8004Slot\ndef_special ChoosePartyMon\ndef_special ScriptGetPartyMonSpecies\ndef_special ChangePokemonNickname\n");
put("src/scrcmd.c", "void ScrCmd_dowildbattle(void) { BattleSetup_StartScriptedWildBattle(); }\nvoid ScrCmd_giveegg(void) { gSpecialVar_Result = ScriptGiveEgg(species); }\n");
put("src/script_pokemon_util.c", "void HealPlayerParty(void) { HealPokemon(mon); }\nvoid ScrCmd_createmon(void) { ScriptGiveMonParameterized(side,slot); }\nint ScriptGiveEgg(void) { CreateEgg(); return GiveCapturedMonToPlayer(mon); }\nvoid ScriptSetMonMoveSlot(void) { SetMonMoveSlot(mon, move, slot); }\nvoid Script_SetStatus1(void) { ScriptReadHalfword(ctx); SetMonData(mon, MON_DATA_STATUS, status); }\nvoid SetTeraType(void) { ScriptReadByte(ctx); SetMonData(mon, MON_DATA_TERA_TYPE, type); }\n");
put("src/pokemon.c", "int CopyMonToPC(void) { gSpecialVar_MonBoxId = boxNo; gSpecialVar_MonBoxPos = boxPos; if (space) return MON_GIVEN_TO_PC; return MON_CANT_GIVE; }\nint GiveScriptedMonToPlayer(void) { if (full) return CopyMonToPC(mon); return MON_CANT_GIVE; }\nvoid *GetSelectedBoxMonFromPcOrParty(void) { if (gSpecialVar_0x8004 == PC_MON_CHOSEN) return GetBoxedMonPtr(gSpecialVar_MonBoxId, gSpecialVar_MonBoxPos); return party; }\n");
put("src/field_specials.c", "int GetBattleOutcome(void) { return gBattleOutcome; }\nint ScriptGetPartyMonSpecies(void) { return GetMonData(mon, MON_DATA_SPECIES_OR_EGG); }\n");
put("src/party_menu.c", "static void BufferMonSelection(void) { gSpecialVar_0x8004 = GetCursorSelectionMonId(); }\n");
put("src/battle_setup.c", "int GetTrainerFlagFromScriptPointer(void) { return trainerFlag; }\nvoid HasEnoughMonsForDoubleBattle(void) {}\n");
put("src/pokemon_storage_system.c", "int CountPartyAliveNonEggMonsExcept(int slot) { return GetMonData(mon, MON_DATA_HP) && !GetMonData(mon, MON_DATA_IS_EGG); }\nint CountPartyAliveNonEggMons_IgnoreVar0x8004Slot(void) { return CountPartyAliveNonEggMonsExcept(gSpecialVar_0x8004); }\n");
put("src/tv.c", "void ChangePokemonNickname(void) { ChangePokemonNicknameWithCallback(callback); }\n");
put("src/data/pokemon/species_info/gen_1_families.h", "[SPECIES_BULBASAUR] = { .genderRatio = PERCENT_FEMALE(12.5), .abilities = {ABILITY_OVERGROW, ABILITY_NONE, ABILITY_CHLOROPHYLL}, .speciesName = _(\"Bulbasaur\"), },\n[SPECIES_MAGNEMITE] = { .genderRatio = MON_GENDERLESS, .abilities = {ABILITY_MAGNET_PULL, ABILITY_STURDY, ABILITY_ANALYTIC}, },\n");
put("include/constants/pokemon.h", "#define MON_CANT_GIVE 2\nenum Type { TYPE_NORMAL = 1, TYPE_GRASS = 13, TYPE_FIRE = 11 };\n#define NATURE_MODEST 15\n");
}
#endif
