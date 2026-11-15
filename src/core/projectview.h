#pragma once

#include "jsonio.h"

#include <QMap>
#include <QPointF>
#include <QString>

namespace ppcb {

// How a page of a project was last looked at: the scene point in the middle
// of the view, in millimetres, and the scale in screen pixels per millimetre.
struct PageView
{
    QPointF center;
    double pixelsPerMm = 0;
};

bool operator==(const PageView &a, const PageView &b);
inline bool operator!=(const PageView &a, const PageView &b) { return !(a == b); }

// The views of the pages of a project: a sheet's by its id (idToString()),
// the board's by BOARD_VIEW_KEY. They are not part of the design: kept in
// a hidden file beside the project file, outside the history and the saved
// mark, so that looking around changes nothing in the project.
using ProjectView = QMap<QString, PageView>;
extern const char BOARD_VIEW_KEY[];

// "<project folder>/.ppview".
QString projectViewPath(const QString &projectPath);

QJsonObject projectViewToJson(const ProjectView &view);
// Pages whose view is not a centre and a positive scale are left out.
ProjectView projectViewFromJson(const QJsonObject &root, JsonReader &reader);

// No views when the file is missing or cannot be read: a view is not worth
// an error.
ProjectView readProjectView(const QString &projectPath);
bool writeProjectView(const QString &projectPath, const ProjectView &view, QString &error);

} // namespace ppcb
