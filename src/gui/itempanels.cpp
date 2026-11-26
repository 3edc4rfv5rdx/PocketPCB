#include "itempanels.h"

#include "format.h"

#include <QCheckBox>
#include <QFormLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QVBoxLayout>

namespace ppcb {

QLabel *panelHeading(QWidget *parent)
{
    QLabel *heading = new QLabel(parent);
    QFont bold = heading->font();
    bold.setBold(true);
    heading->setFont(bold);
    return heading;
}

QLineEdit *panelLineEdit(QWidget *panel, const char *name)
{
    QLineEdit *edit = new QLineEdit(panel);
    edit->setObjectName(QLatin1String(name));
    edit->installEventFilter(panel);
    return edit;
}

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

// GraphicPanel

GraphicPanel::GraphicPanel(ItemEditor *editor, QWidget *parent)
    : ItemPanel(parent)
    , m_editor(editor)
{
    m_heading = panelHeading(this);
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
        graphics.append(m_editor->graphics().at(i));
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
    QList<Graphic> changed = m_editor->graphics();
    for (const int i : indices) {
        if (!apply(changed[i])) {
            refresh(true);
            return;
        }
    }
    // The next drawings drawn are like those set.
    for (const int i : indices) {
        if (changed.at(i).kind != GraphicKind::Text) {
            m_editor->setNextGraphic(changed.at(i));
            break;
        }
    }
    m_editor->changeGraphics(ItemEditor::drawingPropertiesStep(), changed);
    refresh(true);
}

} // namespace ppcb
