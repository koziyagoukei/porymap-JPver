#ifndef VISUALEVENTTOOLS_H
#define VISUALEVENTTOOLS_H
#include "visualeventscript.h"
class Project;
class Map;
class QWidget;
namespace VisualEventTools {
QString chooseConstant(Project *, VisualEvent::Field, const QString &, QWidget *);
bool editMovement(Project *, Map *, VisualEvent::Command *, QWidget *);
bool editWarp(Project *, VisualEvent::Command *, QWidget *);
bool insertTemplate(Project *, Map *, VisualEvent::Document *, bool isNpc, QWidget *);
}
#endif
