#include "pokemon_engine_fixture.h"
#include "pokemonevent.h"

static void pokemonEventChecks(const QString &root, const QString &realProject) {
    using namespace VisualEvent;
    if (!realProject.isEmpty()) {
        for (const auto &path : QStringList{"src/scrcmd.c","src/script_pokemon_util.c","src/pokemon.c","src/field_specials.c","src/party_menu.c","src/battle_setup.c","src/tv.c","src/pokemon_storage_system.c","data/specials.inc","include/constants/pokemon.h","include/constants/species.h","src/data/pokemon/species_info/gen_1_families.h"})
            write(root + "/" + path, bytes(realProject + "/" + path));
    } else {
        seedPokemonEngineFixture([&](const QString &path,const QByteArray &data){write(root+"/"+path,data);},bytes(root+"/asm/macros/event.inc"));
    }
    const auto engine = PokemonEngine::inspect(root);
    for (const auto &id : QStringList{"wildpokemon","trainerpokemon","healpokemon","giftpokemon","eggPokemon","partypokemon","trainerstatus"}) check(engine.unavailable(id).isEmpty(), qPrintable(engine.unavailable(id)));
    check(engine.giftDialect == "expanded" && engine.nickname, "extended gift and PC nickname implementation detected");
    auto trainer = pokemonCommand("trainerpokemon", &engine); trainer.options["intro"] = "Hello!"; trainer.options["defeat"] = "Hello!"; trainer.options["double"] = true; trainer.options["insufficient"] = "Hello!"; trainer.options["victory"] = "Hello!"; trainer.options["beaten"] = "Hello!";
    auto wild = pokemonCommand("wildpokemon", &engine); wild.options["before"] = "Hello!"; wild.options["win"] = "Hello!"; wild.options["caught"] = "Hello!"; wild.options["escaped"] = "Hello!";
    auto heal = pokemonCommand("healpokemon", &engine); heal.options["center"] = true; heal.options["message"] = "Hello!";
    auto gift = pokemonCommand("giftpokemon", &engine); gift.options["flag"] = "FLAG_RECEIVED_POTION_OLDALE"; gift.options["nickname"] = "Hello!"; gift.options["message"] = "Hello!"; gift.options["failure"] = "Hello!";
    gift.options["details"] = QJsonObject{{"nature","NATURE_MODEST"},{"abilityNum","2"},{"gender","MON_FEMALE"},{"shinyMode","1"},{"hpIv","31"},{"atkIv","0"},{"defIv","31"},{"speedIv","31"},{"spAtkIv","31"},{"spDefIv","31"},{"hpEv","6"},{"spAtkEv","252"},{"speedEv","252"},{"move1","MOVE_TACKLE"},{"move2","MOVE_GROWL"},{"move3","MOVE_NONE"},{"move4","MOVE_NONE"},{"teraType","TYPE_GRASS"},{"dmaxLevel","10"},{"gmaxFactor","0"},{"ball","BALL_POKE"}};
    auto egg = pokemonCommand("eggPokemon", &engine); egg.options["message"] = "Hello!"; egg.options["failure"] = "Hello!";
    auto party = pokemonCommand("partypokemon", &engine); party.options["operation"] = "move";
    auto cure=party;cure.options["operation"]="status";cure.options["target"]="first";
    auto tera=party;tera.options["operation"]="tera";tera.options["target"]="slot";tera.options["slot"]=5;tera.options["teraType"]="TYPE_GRASS";
    const auto status = pokemonCommand("trainerstatus", &engine);
    for (bool pory : {false,true}) {
        const QString path = root + "/data/maps/Test/pokemon." + (pory ? "pory" : "inc");
        ScriptFile source; check(source.open(root, path, pory ? "Pory_Pokemon" : "Native_Pokemon", {path}, pory), "Pokemon source opens");
        auto doc = source.document; doc.lockPlayer = false; doc.commands = {trainer,wild,heal,gift,egg,party,cure,tera,status,defaultCommand("end")}; doc.npcTrainer = {{"type","TRAINER_TYPE_NORMAL"},{"sight",3}};
        QString error; const auto code = source.preview(doc, &error); check(error.isEmpty(), qPrintable(error));
        check(code.contains("trainerbattle_double") && code.contains("_Won:") && code.contains("special HealPlayerParty"), "native trainer flow and healing generated");
        check(code.contains("givemon SPECIES_BULBASAUR, 5, ITEM_NONE") && code.contains("abilityNum=2") && code.contains("compare VAR_0x8008, MON_CANT_GIVE"), "extended creation checks actual receipt status");
        check(code.contains("setvar VAR_0x8004, PC_MON_CHOSEN") && code.contains("special ChangePokemonNickname"), "party and PC nickname paths generated");
        check(code.contains("giveegg SPECIES_TOGEPI") && code.contains("compare VAR_0x8008, SPECIES_EGG") && code.contains("setmonmove 5, 0, MOVE_TACKLE"), "egg receipt and guarded literal move dispatch");
        check(code.contains("B_OUTCOME_CAUGHT") && code.contains("B_OUTCOME_RAN"), "battle result branches generated from engine constants");
        check(code.contains("CountPartyAliveNonEggMons_IgnoreVar0x8004Slot")&&code.contains("_NoParty:"),"wild battle guarded against empty, egg-only and fainted parties using verified counter");
        check(code.contains("setstatus1 STATUS1_NONE, VAR_0x8009")&&code.contains("setteratype TYPE_GRASS, VAR_0x8009"),"verified status cure and Tera operations generated");
        check(source.save(doc,&error), qPrintable(error)); ScriptFile reopened;check(reopened.open(root,path,doc.label,{path},pory),qPrintable(reopened.reason()));check(reopened.document.toJson()==doc.toJson(),"all Pokemon commands and NPC metadata reload");
        write(pory ? "results/pokemon.pory" : "results/pokemon.inc",code.toUtf8());
        auto changed=doc;changed.commands[0].options["intro"]="Hello!\nHello!";check(reopened.save(changed,&error),qPrintable(error));
        auto bytesBefore=bytes(path);auto hand=bytesBefore;hand.replace("special HealPlayerParty","special UnsupportedCustomHealing");write(path,hand);ScriptFile readOnly;check(!readOnly.open(root,path,doc.label,{path},pory),"edited Pokemon native blocks protected");write(path,bytesBefore);
        auto invalid=doc;invalid.lockPlayer=true;source.preview(invalid,&error);check(!error.isEmpty(),"trainer preamble locking rejected");
        invalid=doc;invalid.commands.move(0,1);source.preview(invalid,&error);check(!error.isEmpty(),"trainer opcode must be first");
    }
    auto invalid=gift;auto details=gift.options["details"].toObject();details["atkEv"]="252";invalid.options["details"]=details;check(!validatePokemon(invalid,engine).isEmpty(),"over-total EVs rejected");
    details=gift.options["details"].toObject();details["hpIv"]="32";invalid.options["details"]=details;check(!validatePokemon(invalid,engine).isEmpty(),"out of range IV rejected");
    details=gift.options["details"].toObject();details["abilityNum"]="1";invalid.options["details"]=details;check(!validatePokemon(invalid,engine).isEmpty(),"unavailable ability slot rejected");
    invalid=gift;invalid.options["species"]="SPECIES_MAGNEMITE";check(!validatePokemon(invalid,engine).isEmpty(),"fixed-gender species protected");
    invalid=gift;details=gift.options["details"].toObject();details["shinyMode"]="2";invalid.options["details"]=details;check(!validatePokemon(invalid,engine).isEmpty(),"non-boolean shiny override rejected");
    invalid=gift;invalid.options["level"]=101;check(!validatePokemon(invalid,engine).isEmpty(),"level limit enforced");
    invalid=gift;invalid.options["species"]="SPECIES_BULBASAUR\nend";check(!validatePokemon(invalid,engine).isEmpty(),"Pokemon parameter injection rejected");
    for(const auto &target:QStringList{"first","slot","select","find"}){auto selected=party;selected.options["target"]=target;check(validatePokemon(selected,engine).isEmpty(),"all party selection modes validate");QStringList texts;const auto code=generatePokemon(selected,"Party_Test",&texts);check(code.contains("getpartysize")&&code.contains("SPECIES_EGG")&&code.contains("_Invalid:"),"empty party, missing slot and eggs guarded for every selector");}
    invalid=party;invalid.options["slot"]=6;check(!validatePokemon(invalid,engine).isEmpty(),"party slot 6 never dereferenced");
    invalid=tera;invalid.options["teraType"]="TYPE_NONE";check(!validatePokemon(invalid,engine).isEmpty(),"invalid target Tera type rejected");
    auto missing=engine;missing.partyTera=false;check(!validatePokemon(tera,missing).isEmpty(),"missing native Tera implementation disables operation");
    auto unknown=engine;unknown.giftDialect="custom";check(!validatePokemon(gift,unknown).isEmpty(),"changed givemon dialect rejected");
    unknown=engine;unknown.macros.remove("special");check(!unknown.unavailable("healpokemon").isEmpty(),"missing macros disable UI capabilities");
    unknown=engine;unknown.giftDialect="standard";unknown.nickname=false;auto basic=pokemonCommand("giftpokemon",&unknown);basic.options["details"]=gift.options["details"];check(!validatePokemon(basic,unknown).isEmpty(),"standard dialect rejects Expansion creation fields");
    // Full-party/PC-full outcomes are delegated to the verified engine. Test the script's gates,
    // not a fabricated replacement for the engine's storage implementation.
    QStringList text;const auto code=generatePokemon(gift,"Gift_Result_Gates",&text);
    check(code.indexOf("compare VAR_0x8008, MON_CANT_GIVE")<code.indexOf("setflag FLAG_RECEIVED_POTION_OLDALE"),"failure guard precedes success flag");
    check(code.contains("setvar VAR_0x8008, 3")&&code.contains("getpartysize"),"already received has explicit result and party slots are not overwritten");
}
