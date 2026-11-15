#pragma once

#include "objectid.h"

#include <QSet>
#include <QWidget>

class QCheckBox;
class QComboBox;
class QLabel;
class QLineEdit;

namespace ppcb {

class Document;
class SheetEditor;

// The properties of the components chosen on the sheet: designator, value,
// footprint and whether the part is off the board. With several chosen, a
// field shows what they all have, or stays empty saying they differ, and
// what is set there goes to them all as one step. The designator is set one
// component at a time; one another component has is shown in red.
class PropertiesPanel : public QWidget
{
    Q_OBJECT

public:
    PropertiesPanel(Document *document, SheetEditor *editor, QWidget *parent = nullptr);

    // The panel's title, and the name of a footprint as a field and in messages.
    static QString propertiesTitle();
    static QString footprintTitle();

    // Shows the chosen components again. A field being typed in is left
    // alone while the same components stay chosen, unless 'force'.
    void refresh(bool force = false);

private:
    void commitDesignator();
    void commitValue();
    void commitFootprint(int index);
    void commitOffBoard();
    // Red when the designator typed is another component's.
    void markDesignator();
    // True when 'field' keeps what the user is typing.
    bool isTyping(const QWidget *field, bool force) const;

    Document *m_document;
    SheetEditor *m_editor;
    // The components shown.
    QSet<ObjectId> m_ids;
    QLabel *m_heading = nullptr;
    QLineEdit *m_designator = nullptr;
    QLineEdit *m_value = nullptr;
    QComboBox *m_footprint = nullptr;
    QCheckBox *m_offBoard = nullptr;
    // The value differs between the components shown.
    bool m_valuesDiffer = false;
};

} // namespace ppcb
