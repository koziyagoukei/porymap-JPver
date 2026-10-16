#ifndef POKEMONEVENT_H
#define POKEMONEVENT_H
#include "visualeventscript.h"

namespace VisualEvent {
// Capabilities are derived from macro signatures, specials and their C implementations.
struct PokemonEngine {
    QString root, giftDialect;
    QMap<QString, QString> macros;
    QMap<QString, QString> macroBodies;
    QSet<QString> specials, automaticWait;
    QString scriptC, utilityC, pokemonC, fieldC, partyC, battleC, tvC, storageC, constants;
    bool wild = false, trainer = false, heal = false, gift = false, egg = false, party = false, nickname = false;
    bool partyStatus = false, partyTera = false;
    QString unavailable(const QString &id) const;
    static PokemonEngine inspect(const QString &root);
};
bool isPokemonCommand(const QString &id);
Command pokemonCommand(const QString &id, const PokemonEngine *engine = nullptr);
QString validatePokemon(const Command &, const PokemonEngine &);
QStringList pokemonTexts(const Command &);
QString generatePokemon(const Command &, const QString &label, QStringList *texts);
struct SpeciesDetails { QString name, gender; QStringList abilities; bool gigantamax = false; };
SpeciesDetails speciesDetails(const QString &root, const QString &species);
QMap<QString, QString> pokemonDisplayNames(const QString &root, Field);
QString pokemonCategory(const QString &id);
}
#endif
