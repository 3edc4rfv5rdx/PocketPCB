#pragma once

#include "itemeditor.h"
#include "propertiespanel.h"

#include <QComboBox>
#include <QLineEdit>

#include <algorithm>
#include <functional>

class QCheckBox;
class QLabel;

namespace ppcb {

// The properties of the drawings chosen in an item editor: the line width
// and dashes, the diameter of a circle or an arc, the fill of a polygon,
// and of a text its words, height and alignment; fields that none of them
// has are off. Several chosen are set as one step; with none chosen, the
// line of the drawings drawn next. Enter or leaving a field sets what is
// typed, Esc takes it back.
class GraphicPanel : public ItemPanel
{
    Q_OBJECT

public:
    explicit GraphicPanel(ItemEditor *editor, QWidget *parent = nullptr);

    static QString drawingTitle();
    void refresh(bool force = false) override;

protected:
    bool eventFilter(QObject *watched, QEvent *event) override;

private:
    template <typename Apply>
    void commit(Apply apply);

    ItemEditor *m_editor;
    QSet<int> m_shown;
    QLabel *m_heading = nullptr;
    QLineEdit *m_width = nullptr;
    QCheckBox *m_dashed = nullptr;
    QLineEdit *m_diameter = nullptr;
    QCheckBox *m_filled = nullptr;
    QLineEdit *m_text = nullptr;
    QLineEdit *m_height = nullptr;
    QComboBox *m_align = nullptr;
};

// What the panels of the item editors share.

// Esc in one of the line fields of a panel: the panel shows what is chosen
// again and the field lets go of the keyboard, the window keeping its Esc.
// True when 'event' was that.
bool panelFieldEscape(QObject *watched, QEvent *event, const std::function<void()> &refresh);
// A heading over the fields of a panel.
QLabel *panelHeading(QWidget *parent);
// A line field of 'panel', which watches its keys (panelFieldEscape()).
QLineEdit *panelLineEdit(QWidget *panel, const char *name);
// The indices of 'chosen' in order.
QList<int> inOrder(const QSet<int> &chosen);
// A length as a field shows it, in millimetres without the unit.
QString mmText(Coord nm);

// True when 'field' keeps what the user is typing.
inline bool isTyping(const QWidget *field, bool force)
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

} // namespace ppcb
