#pragma once

#include <QColor>
#include <QList>
#include <QPixmap>
#include <QSize>

#include <functional>

class QMenu;
class QObject;

namespace ppcb {

// The property of a swatch button that holds its colour, for a preview
// while the mouse is over it.
extern const char SWATCH_COLOR_PROPERTY[];

// Adds to 'menu' a table of 'colors', 'columns' in a row, each a button
// showing 'picture' of it, its code as its tip; the one 'current' is pressed.
// A click gives the colour to 'chosen' and closes 'closing', or 'menu' when
// it is null. 'previewer', when not null, watches each button's Enter and
// Leave (see SWATCH_COLOR_PROPERTY). 'dense' sets them close, with thin
// frames: a table of many small swatches.
void addSwatchTable(QMenu *menu, const QList<QColor> &colors, const QColor &current,
                    const QSize &size, int columns,
                    const std::function<QPixmap(const QColor &)> &picture,
                    const std::function<void(const QColor &)> &chosen,
                    QObject *previewer = nullptr, QMenu *closing = nullptr, bool dense = false);

// A plain square of 'color' of 'size', edged in grey: a swatch.
QPixmap colorSwatch(const QColor &color, const QSize &size);

} // namespace ppcb
