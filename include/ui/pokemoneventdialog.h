#ifndef POKEMONEVENTDIALOG_H
#define POKEMONEVENTDIALOG_H
#include "pokemonevent.h"
class Project;
class Map;
class QWidget;
namespace PokemonEventDialog {
bool edit(Project *, Map *, VisualEvent::Command *, QJsonObject *npcSettings, bool isNpc, QWidget *, bool requireFlag = false);
bool insertTemplate(Project *, Map *, VisualEvent::Document *, bool isNpc, QWidget *);
}
#endif
