#include "footprinteditor.h"

#include "format.h"
#include "isometric.h"
#include "paint.h"
#include "propertiespanel.h"
#include "swatches.h"

#include <QCheckBox>
#include <QComboBox>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QPainter>
#include <QToolButton>
#include <QVBoxLayout>

#include <algorithm>
#include <climits>

namespace ppcb {

namespace {

// A heading over the fields of a panel.
QLabel *headingLabel(QWidget *parent)
{
    QLabel *heading = new QLabel(parent);
    QFont bold = heading->font();
    bold.setBold(true);
    heading->setFont(bold);
    return heading;
}

// True when 'field' keeps what the user is typing.
bool isTyping(const QWidget *field, bool force)
{
    return !force && field->hasFocus();
}

// 'edit' showing 'value' of 'items' as 'text' makes it when they all have
// the same, else empty saying they differ; left alone while typed in.
template <typename Item, typename Value, typename Text>
void showSame(QLineEdit *edit, const QList<Item> &items, Value value, Text text, bool force)
{
    const bool same = std::all_of(items.cbegin(), items.cend(), [&](const Item &item) {
        return value(item) == value(items.first());
    });
    edit->setPlaceholderText(same ? QString() : PropertiesPanel::differentMark());
    if (!isTyping(edit, force))
        edit->setText(same ? text(value(items.first())) : QString());
}

// 'box' at 'value' of 'items' when they all have the same, else at none.
template <typename Item, typename Value>
void showSameChoice(QComboBox *box, const QList<Item> &items, Value value)
{
    const bool same = std::all_of(items.cbegin(), items.cend(), [&](const Item &item) {
        return value(item) == value(items.first());
    });
    box->setCurrentIndex(same ? int(value(items.first())) : -1);
}

QLineEdit *panelLineEdit(QWidget *panel, const char *name)
{
    QLineEdit *edit = new QLineEdit(panel);
    edit->setObjectName(QLatin1String(name));
    edit->installEventFilter(panel);
    return edit;
}

// The indices of 'chosen' in order.
QList<int> inOrder(const QSet<int> &chosen)
{
    QList<int> indices(chosen.cbegin(), chosen.cend());
    std::sort(indices.begin(), indices.end());
    return indices;
}

QString mmText(Coord nm)
{
    return formatMmShort(nm);
}

// The body a new one starts as: 3 mm high, black.
const Coord NEW_BODY_HEIGHT = 3000000;
// The colour button's swatch, and those of its table, in pixels.
const QSize BODY_SWATCH(14, 14);
const QSize BODY_BUTTON_SWATCH(48, 16);
// 16 columns of 8 rows: 128 colours.
const int BODY_COLOR_COLUMNS = 16;
// The first of the greys: a black plastic, not quite black.
const int BODY_DARKEST_GREY = 24;

// The colour of the table of bodies nearest to 'code', so that a
// ready-made body's colour is found in it.
QString nearestBodyColor(const QString &code)
{
    const QColor wanted(code);
    QColor best = wanted;
    int bestDistance = INT_MAX;
    for (const QColor &color : BodyPanel::bodyColors()) {
        const int r = color.red() - wanted.red();
        const int g = color.green() - wanted.green();
        const int b = color.blue() - wanted.blue();
        const int distance = r * r + g * g + b * b;
        if (distance < bestDistance) {
            bestDistance = distance;
            best = color;
        }
    }
    return best.name();
}

// The footprint as it would stand on a board, drawn in three dimensions.
class BodyPreview : public QWidget
{
public:
    BodyPreview(const FootprintEditor *editor, QWidget *parent)
        : QWidget(parent)
        , m_editor(editor)
    {
        setMinimumHeight(PREVIEW_HEIGHT);
    }

protected:
    void paintEvent(QPaintEvent *event) override
    {
        Q_UNUSED(event);
        QPainter painter(this);
        painter.fillRect(rect(), paintcolor::background());
        paintIsometric(painter, footprintFaces(m_editor->footprint()), QRectF(rect()));
    }

private:
    static constexpr int PREVIEW_HEIGHT = 160;
    const FootprintEditor *m_editor;
};

Body bodyOf(BodyShape shape, Coord sizeX, Coord sizeY, Coord height, Coord raised,
            const char *color)
{
    Body body;
    body.shape = shape;
    body.sizeX = sizeX;
    body.sizeY = sizeY;
    body.height = height;
    body.raised = raised;
    body.color = nearestBodyColor(QLatin1String(color));
    return body;
}

} // namespace

bool panelFieldEscape(QObject *watched, QEvent *event, const std::function<void()> &refresh)
{
    const bool escape = (event->type() == QEvent::ShortcutOverride
                         || event->type() == QEvent::KeyPress)
                        && static_cast<QKeyEvent *>(event)->key() == Qt::Key_Escape;
    if (!escape || qobject_cast<QLineEdit *>(watched) == nullptr)
        return false;
    // Esc is the field's, not the window's, which would drop the choice.
    event->accept();
    if (event->type() == QEvent::KeyPress) {
        refresh();
        static_cast<QWidget *>(watched)->clearFocus();
    }
    return true;
}

// PadPanel

PadPanel::PadPanel(FootprintEditor *editor, QWidget *parent)
    : QWidget(parent)
    , m_editor(editor)
{
    m_heading = headingLabel(this);
    m_number = panelLineEdit(this, "padNumber");
    m_shape = new QComboBox(this);
    m_shape->setObjectName(QStringLiteral("padShape"));
    // In the order of PadShape.
    m_shape->addItems({tr("Round"), tr("Rectangle"), tr("Oval")});
    m_width = panelLineEdit(this, "padWidth");
    m_height = panelLineEdit(this, "padHeight");
    m_hole = panelLineEdit(this, "padHole");
    m_layers = new QComboBox(this);
    m_layers->setObjectName(QStringLiteral("padLayers"));
    // In the order of PadLayers.
    m_layers->addItems({tr("Both sides"), tr("Side of the part"), tr("Other side")});
    m_x = panelLineEdit(this, "padX");
    m_y = panelLineEdit(this, "padY");

    QFormLayout *form = new QFormLayout;
    const QString unit = QStringLiteral(", ") + mmUnit() + QLatin1Char(':');
    form->addRow(tr("Number") + QLatin1Char(':'), m_number);
    form->addRow(tr("Shape") + QLatin1Char(':'), m_shape);
    form->addRow(tr("Width") + unit, m_width);
    form->addRow(tr("Height") + unit, m_height);
    form->addRow(tr("Hole") + unit, m_hole);
    form->addRow(tr("Copper") + QLatin1Char(':'), m_layers);
    form->addRow(QStringLiteral("X") + unit, m_x);
    form->addRow(QStringLiteral("Y") + unit, m_y);
    QVBoxLayout *layout = new QVBoxLayout(this);
    layout->addWidget(m_heading);
    layout->addLayout(form);
    layout->addStretch();

    connect(m_number, &QLineEdit::editingFinished, this, &PadPanel::commitNumber);
    for (QLineEdit *edit : {m_width, m_height, m_hole, m_x, m_y})
        connect(edit, &QLineEdit::editingFinished, this, [this, edit] { commitMm(edit); });
    connect(m_shape, &QComboBox::activated, this, &PadPanel::commitShape);
    connect(m_layers, &QComboBox::activated, this, &PadPanel::commitLayers);
    refresh(true);
}

QString PadPanel::padTitle()
{
    return tr("Pad");
}

bool PadPanel::eventFilter(QObject *watched, QEvent *event)
{
    return panelFieldEscape(watched, event, [this] { refresh(true); })
           || QWidget::eventFilter(watched, event);
}

void PadPanel::refresh(bool force)
{
    const QSet<int> &chosen = m_editor->chosen().pads;
    if (chosen != m_shown)
        force = true;
    m_shown = chosen;
    QList<Pad> pads;
    for (const int i : inOrder(chosen))
        pads.append(m_editor->footprint().pads.at(i));
    const bool none = pads.isEmpty();
    if (none)
        pads.append(m_editor->nextPad());

    if (none)
        m_heading->setText(tr("New pads"));
    else if (pads.size() == 1)
        m_heading->setText(padTitle() + QLatin1Char(' ') + pads.first().number);
    else
        m_heading->setText(tr("Pads chosen") + QStringLiteral(": ") + QString::number(pads.size()));

    m_number->setEnabled(pads.size() == 1 && !none);
    showSame(m_number, pads, [](const Pad &pad) { return pad.number; },
             [](const QString &number) { return number; }, force);
    if (none && !isTyping(m_number, force))
        m_number->clear();
    showSameChoice(m_shape, pads, [](const Pad &pad) { return pad.shape; });
    showSame(m_width, pads, [](const Pad &pad) { return pad.sizeX; }, mmText, force);
    showSame(m_height, pads, [](const Pad &pad) { return pad.sizeY; }, mmText, force);
    // A round pad has one size.
    m_height->setEnabled(std::none_of(pads.cbegin(), pads.cend(), [](const Pad &pad) {
        return pad.shape == PadShape::Circle;
    }));
    showSame(m_hole, pads, [](const Pad &pad) { return pad.hole; }, mmText, force);
    showSameChoice(m_layers, pads, [](const Pad &pad) { return pad.layers; });
    m_x->setEnabled(!none);
    m_y->setEnabled(!none);
    showSame(m_x, pads, [](const Pad &pad) { return pad.at.x; }, mmText, force);
    showSame(m_y, pads, [](const Pad &pad) { return pad.at.y; }, mmText, force);
    if (none && !isTyping(m_x, force))
        m_x->clear();
    if (none && !isTyping(m_y, force))
        m_y->clear();
}

template <typename Apply>
void PadPanel::commit(Apply apply)
{
    const QList<int> indices = inOrder(m_shown);
    const Footprint &footprint = m_editor->footprint();
    QList<Pad> changed;
    if (indices.isEmpty())
        changed.append(m_editor->nextPad());
    for (const int i : indices)
        changed.append(footprint.pads.at(i));
    for (Pad &pad : changed) {
        if (!apply(pad)) {
            refresh(true);
            return;
        }
        // A round pad has one size, and one with no hole sits on one side.
        if (pad.shape == PadShape::Circle)
            pad.sizeY = pad.sizeX;
        if (pad.hole == 0 && pad.layers == PadLayers::Both)
            pad.layers = PadLayers::Front;
        const QString problem = padProblem(pad);
        if (!problem.isEmpty()) {
            m_editor->showStatus(capitalized(problem));
            refresh(true);
            return;
        }
    }
    // The next pads placed are like those set.
    m_editor->setNextPad(changed.first());
    if (!indices.isEmpty()) {
        FootprintChoice chosen = m_editor->chosen();
        m_editor->change(FootprintEditor::padPropertiesStep(),
                         withPadsReplaced(footprint, indices, changed), chosen);
    }
    refresh(true);
}

void PadPanel::commitNumber()
{
    if (m_shown.size() != 1)
        return;
    const QString number = m_number->text().trimmed();
    commit([&number](Pad &pad) {
        pad.number = number;
        return true;
    });
}

void PadPanel::commitShape(int index)
{
    if (index < 0)
        return;
    commit([index](Pad &pad) {
        pad.shape = PadShape(index);
        return true;
    });
}

void PadPanel::commitLayers(int index)
{
    if (index < 0)
        return;
    commit([index](Pad &pad) {
        pad.layers = PadLayers(index);
        return true;
    });
}

void PadPanel::commitMm(QLineEdit *edit)
{
    // An empty field of pads that differ sets nothing.
    Coord nm = 0;
    if (edit->text().trimmed().isEmpty() || !parseMm(edit->text(), nm)) {
        refresh(true);
        return;
    }
    commit([this, edit, nm](Pad &pad) {
        if (edit == m_width)
            pad.sizeX = nm;
        else if (edit == m_height)
            pad.sizeY = nm;
        else if (edit == m_hole)
            pad.hole = nm;
        else if (edit == m_x)
            pad.at.x = nm;
        else if (edit == m_y)
            pad.at.y = nm;
        return true;
    });
}

// GraphicPanel

GraphicPanel::GraphicPanel(FootprintEditor *editor, QWidget *parent)
    : QWidget(parent)
    , m_editor(editor)
{
    m_heading = headingLabel(this);
    m_width = panelLineEdit(this, "drawingWidth");
    m_dashed = new QCheckBox(tr("Dashed"), this);
    m_dashed->setObjectName(QStringLiteral("drawingDashed"));
    m_diameter = panelLineEdit(this, "drawingDiameter");
    m_filled = new QCheckBox(tr("Filled"), this);
    m_filled->setObjectName(QStringLiteral("drawingFilled"));
    m_text = panelLineEdit(this, "drawingText");
    m_height = panelLineEdit(this, "drawingHeight");
    m_align = new QComboBox(this);
    m_align->setObjectName(QStringLiteral("drawingAlign"));
    // In the order of TextAlign.
    m_align->addItems({tr("Left"), tr("Centre"), tr("Right")});

    QFormLayout *form = new QFormLayout;
    const QString unit = QStringLiteral(", ") + mmUnit() + QLatin1Char(':');
    form->addRow(tr("Line") + unit, m_width);
    form->addRow(QString(), m_dashed);
    form->addRow(tr("Diameter") + unit, m_diameter);
    form->addRow(QString(), m_filled);
    form->addRow(tr("Text") + QLatin1Char(':'), m_text);
    form->addRow(tr("Letters") + unit, m_height);
    form->addRow(tr("Alignment") + QLatin1Char(':'), m_align);
    QVBoxLayout *layout = new QVBoxLayout(this);
    layout->addWidget(m_heading);
    layout->addLayout(form);
    layout->addStretch();

    connect(m_width, &QLineEdit::editingFinished, this, [this] {
        Coord nm = 0;
        if (m_width->text().trimmed().isEmpty() || !parseMm(m_width->text(), nm) || nm <= 0) {
            refresh(true);
            return;
        }
        commit([nm](Graphic &graphic) {
            if (graphic.kind != GraphicKind::Text)
                graphic.width = nm;
            return true;
        });
    });
    connect(m_dashed, &QCheckBox::clicked, this, [this](bool on) {
        commit([on](Graphic &graphic) {
            if (graphic.kind != GraphicKind::Text)
                graphic.dashed = on;
            return true;
        });
    });
    connect(m_diameter, &QLineEdit::editingFinished, this, [this] {
        Coord nm = 0;
        if (m_diameter->text().trimmed().isEmpty() || !parseMm(m_diameter->text(), nm)
            || nm < 2) {
            refresh(true);
            return;
        }
        commit([nm](Graphic &graphic) {
            if (graphic.kind == GraphicKind::Circle || graphic.kind == GraphicKind::Arc)
                graphic.radius = nm / 2;
            return true;
        });
    });
    connect(m_filled, &QCheckBox::clicked, this, [this](bool on) {
        commit([on](Graphic &graphic) {
            if (graphic.kind == GraphicKind::Polygon)
                graphic.filled = on;
            return true;
        });
    });
    connect(m_text, &QLineEdit::editingFinished, this, [this] {
        const QString text = m_text->text().trimmed();
        if (text.isEmpty() || m_shown.size() != 1) {
            refresh(true);
            return;
        }
        commit([&text](Graphic &graphic) {
            graphic.text = text;
            return true;
        });
    });
    connect(m_height, &QLineEdit::editingFinished, this, [this] {
        Coord nm = 0;
        if (m_height->text().trimmed().isEmpty() || !parseMm(m_height->text(), nm) || nm <= 0) {
            refresh(true);
            return;
        }
        commit([nm](Graphic &graphic) {
            if (graphic.kind == GraphicKind::Text)
                graphic.height = nm;
            return true;
        });
    });
    connect(m_align, &QComboBox::activated, this, [this](int index) {
        if (index < 0)
            return;
        commit([index](Graphic &graphic) {
            if (graphic.kind == GraphicKind::Text)
                graphic.align = TextAlign(index);
            return true;
        });
    });
    refresh(true);
}

QString GraphicPanel::drawingTitle()
{
    return tr("Drawing");
}

bool GraphicPanel::eventFilter(QObject *watched, QEvent *event)
{
    return panelFieldEscape(watched, event, [this] { refresh(true); })
           || QWidget::eventFilter(watched, event);
}

void GraphicPanel::refresh(bool force)
{
    const QSet<int> &chosen = m_editor->chosen().graphics;
    if (chosen != m_shown)
        force = true;
    m_shown = chosen;
    QList<Graphic> graphics;
    for (const int i : inOrder(chosen))
        graphics.append(m_editor->footprint().graphics.at(i));
    const bool none = graphics.isEmpty();
    m_heading->setText(none ? tr("New drawings")
                            : drawingTitle() + QStringLiteral(": ")
                                  + QString::number(graphics.size()));
    // The fields of what there is among them.
    QList<Graphic> lines;
    QList<Graphic> texts;
    QList<Graphic> rounds;
    bool polygons = false;
    for (const Graphic &graphic : std::as_const(graphics)) {
        if (graphic.kind == GraphicKind::Text)
            texts.append(graphic);
        else
            lines.append(graphic);
        if (graphic.kind == GraphicKind::Circle || graphic.kind == GraphicKind::Arc)
            rounds.append(graphic);
        polygons = polygons || graphic.kind == GraphicKind::Polygon;
    }
    m_diameter->setEnabled(!rounds.isEmpty());
    if (!rounds.isEmpty())
        showSame(m_diameter, rounds, [](const Graphic &graphic) { return 2 * graphic.radius; },
                 mmText, force);
    else if (!isTyping(m_diameter, force))
        m_diameter->clear();
    if (none)
        lines.append(m_editor->nextGraphic());

    m_width->setEnabled(!lines.isEmpty());
    m_dashed->setEnabled(!lines.isEmpty());
    if (!lines.isEmpty()) {
        showSame(m_width, lines, [](const Graphic &graphic) { return graphic.width; }, mmText, force);
        const int dashed = int(std::count_if(lines.cbegin(), lines.cend(),
                                             [](const Graphic &graphic) { return graphic.dashed; }));
        m_dashed->setChecked(dashed > 0);
    } else if (!isTyping(m_width, force)) {
        m_width->clear();
        m_dashed->setChecked(false);
    }
    m_filled->setEnabled(polygons);
    m_filled->setChecked(polygons && std::any_of(graphics.cbegin(), graphics.cend(),
                                                 [](const Graphic &graphic) {
                                                     return graphic.kind == GraphicKind::Polygon
                                                            && graphic.filled;
                                                 }));
    m_text->setEnabled(texts.size() == 1 && graphics.size() == 1);
    m_height->setEnabled(!texts.isEmpty());
    m_align->setEnabled(!texts.isEmpty());
    if (!texts.isEmpty()) {
        showSame(m_text, texts, [](const Graphic &graphic) { return graphic.text; },
                 [](const QString &text) { return text; }, force);
        showSame(m_height, texts, [](const Graphic &graphic) { return graphic.height; }, mmText,
                 force);
        showSameChoice(m_align, texts, [](const Graphic &graphic) { return graphic.align; });
    } else {
        if (!isTyping(m_text, force))
            m_text->clear();
        if (!isTyping(m_height, force))
            m_height->clear();
        m_align->setCurrentIndex(-1);
    }
}

template <typename Apply>
void GraphicPanel::commit(Apply apply)
{
    const QList<int> indices = inOrder(m_shown);
    if (indices.isEmpty()) {
        // The drawings drawn next.
        Graphic next = m_editor->nextGraphic();
        apply(next);
        m_editor->setNextGraphic(next);
        refresh(true);
        return;
    }
    Footprint changed = m_editor->footprint();
    for (const int i : indices) {
        if (!apply(changed.graphics[i])) {
            refresh(true);
            return;
        }
    }
    // The next drawings drawn are like those set.
    for (const int i : indices) {
        if (changed.graphics.at(i).kind != GraphicKind::Text) {
            m_editor->setNextGraphic(changed.graphics.at(i));
            break;
        }
    }
    m_editor->change(FootprintEditor::drawingPropertiesStep(), changed, m_editor->chosen());
    refresh(true);
}

// BodyPanel

BodyPanel::BodyPanel(FootprintEditor *editor, QWidget *parent)
    : QWidget(parent)
    , m_editor(editor)
{
    QLabel *heading = headingLabel(this);
    heading->setText(bodyTitle());
    m_shape = new QComboBox(this);
    m_shape->setObjectName(QStringLiteral("bodyShape"));
    // None, then in the order of BodyShape.
    m_shape->addItems({tr("None"), tr("Box"), tr("Upright cylinder"), tr("Lying cylinder")});
    m_presets = new QToolButton(this);
    m_presets->setObjectName(QStringLiteral("bodyPresets"));
    m_presets->setText(tr("Ready-made"));
    m_presets->setPopupMode(QToolButton::InstantPopup);
    QMenu *presets = new QMenu(m_presets);
    struct Preset
    {
        QString name;
        Body body;
    };
    const QList<Preset> ready = {
        {QStringLiteral("TO-92"), bodyOf(BodyShape::Upright, 4800000, 4800000, 4800000, 2500000, "#202020")},
        {QStringLiteral("TO-220"), bodyOf(BodyShape::Box, 10200000, 4600000, 9200000, 0, "#202020")},
        {QStringLiteral("DIP"), bodyOf(BodyShape::Box, 0, 0, 3500000, 500000, "#202020")},
        {tr("Resistor 0.25 W"), bodyOf(BodyShape::Lying, 6500000, 2500000, 2500000, 500000, "#d9c3a0")},
        {tr("Electrolytic 5 × 11"), bodyOf(BodyShape::Upright, 5000000, 5000000, 11000000, 0, "#1f4fa0")},
        {tr("LED 5 mm"), bodyOf(BodyShape::Upright, 5000000, 5000000, 8600000, 1000000, "#c03030")},
    };
    for (const Preset &preset : ready) {
        connect(presets->addAction(preset.name), &QAction::triggered, this,
                [this, body = preset.body] { commit(body); });
    }
    m_presets->setMenu(presets);
    m_sizeX = panelLineEdit(this, "bodySizeX");
    m_sizeY = panelLineEdit(this, "bodySizeY");
    m_sizeX->setPlaceholderText(tr("by the silkscreen"));
    m_sizeY->setPlaceholderText(tr("by the silkscreen"));
    m_height = panelLineEdit(this, "bodyHeight");
    m_raised = panelLineEdit(this, "bodyRaised");
    m_color = new QToolButton(this);
    m_color->setObjectName(QStringLiteral("bodyColor"));
    m_color->setPopupMode(QToolButton::InstantPopup);
    m_color->setIconSize(BODY_BUTTON_SWATCH);
    QMenu *colors = new QMenu(m_color);
    // Filled as it opens, the colour there is pressed.
    connect(colors, &QMenu::aboutToShow, this, [this, colors] {
        colors->clear();
        const std::optional<Body> &body = m_editor->footprint().body;
        addSwatchTable(colors, bodyColors(), body ? QColor(body->color) : QColor(), BODY_SWATCH,
                       BODY_COLOR_COLUMNS,
                       [](const QColor &color) { return colorSwatch(color, BODY_SWATCH); },
                       [this](const QColor &color) {
                           std::optional<Body> changed = m_editor->footprint().body;
                           if (!changed)
                               return;
                           changed->color = color.name();
                           commit(changed);
                       },
                       nullptr, nullptr, true);
    });
    m_color->setMenu(colors);

    QHBoxLayout *shapeRow = new QHBoxLayout;
    shapeRow->addWidget(m_shape, 1);
    shapeRow->addWidget(m_presets);
    QHBoxLayout *sizeRow = new QHBoxLayout;
    sizeRow->addWidget(m_sizeX);
    sizeRow->addWidget(new QLabel(QStringLiteral("×"), this));
    sizeRow->addWidget(m_sizeY);
    QFormLayout *form = new QFormLayout;
    const QString unit = QStringLiteral(", ") + mmUnit() + QLatin1Char(':');
    form->addRow(tr("Shape") + QLatin1Char(':'), shapeRow);
    form->addRow(tr("Size") + unit, sizeRow);
    form->addRow(tr("Height") + unit, m_height);
    form->addRow(tr("Above the board") + unit, m_raised);
    form->addRow(tr("Colour") + QLatin1Char(':'), m_color);
    m_preview = new BodyPreview(editor, this);
    m_preview->setObjectName(QStringLiteral("bodyPreview"));
    QVBoxLayout *layout = new QVBoxLayout(this);
    layout->addWidget(heading);
    layout->addLayout(form);
    layout->addWidget(m_preview);

    connect(m_shape, &QComboBox::activated, this, [this](int index) {
        if (index <= 0) {
            commit(std::nullopt);
            return;
        }
        Body body = m_editor->footprint().body.value_or(
            bodyOf(BodyShape::Box, 0, 0, NEW_BODY_HEIGHT, 0, DEFAULT_BODY_COLOR));
        body.shape = BodyShape(index - 1);
        commit(body);
    });
    for (QLineEdit *edit : {m_sizeX, m_sizeY, m_height, m_raised})
        connect(edit, &QLineEdit::editingFinished, this, &BodyPanel::commitMm);
    refresh(true);
}

QString BodyPanel::bodyTitle()
{
    return tr("Body");
}

QList<QColor> BodyPanel::bodyColors()
{
    // A row of greys from black to white, then a row for each shade of
    // the colours round the wheel, from dark to pale.
    struct Shade
    {
        double saturation;
        double value;
    };
    static const Shade shades[] = {
        {0.90, 0.30}, {0.90, 0.50}, {0.85, 0.70}, {0.80, 0.90},
        {0.55, 0.95}, {0.35, 0.97}, {0.18, 0.99},
    };
    QList<QColor> colors;
    for (int i = 0; i < BODY_COLOR_COLUMNS; ++i) {
        const int grey = BODY_DARKEST_GREY
                         + (255 - BODY_DARKEST_GREY) * i / (BODY_COLOR_COLUMNS - 1);
        colors.append(QColor(grey, grey, grey));
    }
    for (const Shade &shade : shades) {
        for (int i = 0; i < BODY_COLOR_COLUMNS; ++i)
            // Through its code, as a body keeps it, so that the one a body
            // has is found among them.
            colors.append(QColor(QColor::fromHsvF(double(i) / BODY_COLOR_COLUMNS,
                                                  shade.saturation, shade.value)
                                     .name()));
    }
    return colors;
}

bool BodyPanel::eventFilter(QObject *watched, QEvent *event)
{
    return panelFieldEscape(watched, event, [this] { refresh(true); })
           || QWidget::eventFilter(watched, event);
}

void BodyPanel::refresh(bool force)
{
    const std::optional<Body> &body = m_editor->footprint().body;
    m_shape->setCurrentIndex(body ? int(body->shape) + 1 : 0);
    for (QWidget *field : {static_cast<QWidget *>(m_sizeX), static_cast<QWidget *>(m_sizeY),
                           static_cast<QWidget *>(m_height), static_cast<QWidget *>(m_raised),
                           static_cast<QWidget *>(m_color)})
        field->setEnabled(body.has_value());
    // A lying cylinder is as high as it is thick.
    m_height->setEnabled(body && body->shape != BodyShape::Lying);
    const auto show = [force](QLineEdit *edit, const QString &text) {
        if (!isTyping(edit, force))
            edit->setText(text);
    };
    const bool sized = body && body->sizeX > 0 && body->sizeY > 0;
    show(m_sizeX, sized ? mmText(body->sizeX) : QString());
    show(m_sizeY, sized ? mmText(body->sizeY) : QString());
    show(m_height, body ? mmText(body->height) : QString());
    show(m_raised, body ? mmText(body->raised) : QString());
    m_color->setIcon(body ? QIcon(colorSwatch(QColor(body->color), BODY_BUTTON_SWATCH)) : QIcon());
    m_color->setToolTip(body ? body->color.toUpper() : QString());
    m_preview->update();
}

void BodyPanel::commit(const std::optional<Body> &body)
{
    if (body == m_editor->footprint().body) {
        refresh(true);
        return;
    }
    Footprint changed = m_editor->footprint();
    changed.body = body;
    m_editor->change(bodyTitle(), changed, m_editor->chosen());
    refresh(true);
}

void BodyPanel::commitMm()
{
    std::optional<Body> body = m_editor->footprint().body;
    if (!body)
        return;
    const auto read = [](const QLineEdit *edit, Coord &nm) {
        return parseMm(edit->text(), nm);
    };
    Coord sizeX = 0;
    Coord sizeY = 0;
    // Both sizes, or neither for the drawing of the silkscreen.
    const bool noSize = m_sizeX->text().trimmed().isEmpty() && m_sizeY->text().trimmed().isEmpty();
    if (!noSize && !(read(m_sizeX, sizeX) && read(m_sizeY, sizeY) && sizeX > 0 && sizeY > 0)) {
        // One typed of two: waits for the other.
        if (m_sizeX->text().trimmed().isEmpty() || m_sizeY->text().trimmed().isEmpty())
            return;
        refresh(true);
        return;
    }
    Coord height = body->height;
    Coord raised = 0;
    if (!read(m_height, height) || height <= 0 || !read(m_raised, raised) || raised < 0) {
        refresh(true);
        return;
    }
    body->sizeX = sizeX;
    body->sizeY = sizeY;
    body->height = height;
    body->raised = raised;
    commit(body);
}

} // namespace ppcb
