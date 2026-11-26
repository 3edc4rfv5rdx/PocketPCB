#include "swatches.h"

#include "paint.h"

#include <QEvent>
#include <QGridLayout>
#include <QLabel>
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
// The one chosen in a dense table: a thick black frame.
// Every frame as thick, unseen but there, so that none moves the others.
const char DENSE_STYLE[] = "QToolButton { border: 3px solid transparent; padding: 0px; }"
                           "QToolButton:hover { border: 3px solid #909090; }"
                           "QToolButton:checked { border: 3px solid #000000; }";
// The large swatch beside a dense table, of the colour under the mouse.
const int LOUPE_SIZE = 96;

// Shows in 'loupe' the colour of a swatch the mouse enters, and 'rest'
// once it leaves.
class SwatchLoupe : public QObject
{
public:
    SwatchLoupe(QLabel *loupe, const QColor &rest, QObject *parent)
        : QObject(parent)
        , m_loupe(loupe)
        , m_rest(rest)
    {
        show(rest);
    }

protected:
    bool eventFilter(QObject *watched, QEvent *event) override
    {
        if (event->type() == QEvent::Enter)
            show(watched->property(SWATCH_COLOR_PROPERTY).value<QColor>());
        else if (event->type() == QEvent::Leave)
            show(m_rest);
        return QObject::eventFilter(watched, event);
    }

private:
    void show(const QColor &color)
    {
        if (!color.isValid()) {
            m_loupe->clear();
            m_loupe->setToolTip(QString());
            return;
        }
        m_loupe->setPixmap(colorSwatch(color, QSize(LOUPE_SIZE, LOUPE_SIZE)));
        m_loupe->setToolTip(color.name().toUpper());
    }

    QLabel *m_loupe;
    QColor m_rest;
};

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
    // Left of a dense table, the colour under the mouse at large.
    SwatchLoupe *loupe = nullptr;
    if (dense) {
        QLabel *label = new QLabel(table);
        label->setObjectName(QStringLiteral("swatchLoupe"));
        label->setFixedSize(LOUPE_SIZE, LOUPE_SIZE);
        loupe = new SwatchLoupe(label, current, table);
        const int rows = (int(colors.size()) + columns - 1) / columns;
        grid->addWidget(label, 0, 0, rows, 1, Qt::AlignTop);
        grid->setColumnMinimumWidth(0, LOUPE_SIZE + SWATCH_SPACING);
    }
    // The swatches right of the large one, in a dense table.
    const int firstColumn = dense ? 1 : 0;
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
        if (loupe != nullptr)
            button->installEventFilter(loupe);
        grid->addWidget(button, i / columns, firstColumn + i % columns);
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
