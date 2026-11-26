#include "swatches.h"

#include "paint.h"

#include <QGridLayout>
#include <QMenu>
#include <QPainter>
#include <QToolButton>
#include <QWidgetAction>

namespace ppcb {

namespace {

const int SWATCH_SPACING = 6;
const int DENSE_SPACING = 1;
// The one chosen has a frame in the selection colour.
const char SWATCH_STYLE[] = "QToolButton { border: 1px solid #a0a0a0; padding: 2px; }"
                            "QToolButton:checked { border: 3px solid %1; padding: 0px; }";
const char DENSE_STYLE[] = "QToolButton { border: 1px solid transparent; padding: 0px; }"
                           "QToolButton:hover { border: 1px solid #404040; }"
                           "QToolButton:checked { border: 1px solid %1; }";

} // namespace

const char SWATCH_COLOR_PROPERTY[] = "swatchColor";

void addSwatchTable(QMenu *menu, const QList<QColor> &colors, const QColor &current,
                    const QSize &size, int columns,
                    const std::function<QPixmap(const QColor &)> &picture,
                    const std::function<void(const QColor &)> &chosen, QObject *previewer,
                    QMenu *closing, bool dense)
{
    QWidget *table = new QWidget(menu);
    table->setStyleSheet(QString::fromLatin1(dense ? DENSE_STYLE : SWATCH_STYLE)
                             .arg(paintcolor::selection().name()));
    QGridLayout *grid = new QGridLayout(table);
    const int spacing = dense ? DENSE_SPACING : SWATCH_SPACING;
    grid->setSpacing(spacing);
    grid->setContentsMargins(SWATCH_SPACING, SWATCH_SPACING, SWATCH_SPACING, SWATCH_SPACING);
    QMenu *toClose = closing != nullptr ? closing : menu;
    for (int i = 0; i < colors.size(); ++i) {
        const QColor color = colors.at(i);
        QToolButton *button = new QToolButton(table);
        button->setCheckable(true);
        button->setAutoExclusive(true);
        button->setChecked(color == current);
        button->setIcon(QIcon(picture(color)));
        button->setIconSize(size);
        button->setToolTip(color.name().toUpper());
        button->setProperty(SWATCH_COLOR_PROPERTY, color);
        if (previewer != nullptr)
            button->installEventFilter(previewer);
        grid->addWidget(button, i / columns, i % columns);
        QObject::connect(button, &QToolButton::clicked, menu, [chosen, color, toClose] {
            chosen(color);
            toClose->close();
        });
    }
    QWidgetAction *action = new QWidgetAction(menu);
    action->setDefaultWidget(table);
    menu->addAction(action);
}

QPixmap colorSwatch(const QColor &color, const QSize &size)
{
    QPixmap pixmap(size);
    pixmap.fill(color);
    QPainter painter(&pixmap);
    painter.setPen(QColor(120, 120, 120));
    painter.drawRect(0, 0, size.width() - 1, size.height() - 1);
    return pixmap;
}

} // namespace ppcb
