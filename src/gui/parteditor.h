#pragma once

#include "librarytab.h"

class QCheckBox;
class QComboBox;
class QLabel;
class QLineEdit;
class QListWidget;
class QPushButton;

namespace ppcb {

class NameField;
class PartPreview;

// A tab of the library editor window editing one part of the library (see
// LibraryTab) in a form: its symbol, the symbols of its other units, its
// footprints with the default first,
// its designator prefixes, the value it starts with, its group, its rated
// power, whether it is a connector and needs no value, and its name in
// every language. Beside the form its symbol and the footprint chosen in
// the list are drawn; under it, whether every pin has a pad in every
// footprint. Each change is one step of its Undo.
class PartEditor : public LibraryTab
{
    Q_OBJECT

public:
    // 'library' is the one the symbol and the footprints are taken from;
    // see setLibrary().
    PartEditor(const QString &name, const Part &part, bool inUserLibrary, const Library *library,
               QWidget *parent = nullptr);

    // "New part".
    static QString newPartTitle();
    // True when the user's library has a part 'name' of its own.
    static bool isInUserLibrary(const QString &name);
    QString editorTitle() const override;
    QString newItemTitle() const override { return newPartTitle(); }
    void takeFocus() override;

    const Part &part() const { return m_part; }
    // One step of Undo: the part becomes 'changed'; nothing is done when it
    // is the same.
    void change(const Part &changed);
    // Undo and Redo put 'part' back.
    void applyStep(const Part &part);
    // The library it reads, or the same one changed: the drawings and the
    // check are made again. It must outlive the tab, or be set again
    // before it goes.
    void setLibrary(const Library *library);
    // What the line under the form says.
    QString checkText() const;
    // The index of the footprint chosen in the list; -1 for none.
    int chosenFootprint() const;
    void chooseFootprint(int index);

    // The buttons of the form: the symbol and a footprint asked for from
    // the library, the footprint chosen taken out or moved up or down.
    void askSymbol();
    // The symbol of one more unit asked for and added after the others;
    // the unit chosen in their list taken out.
    void askUnit();
    void removeChosenUnit();
    void askFootprint();
    void removeChosenFootprint();
    void moveChosenFootprint(int by);

protected:
    void takeTyping() override { takeFields(); }
    const char *libraryFolder() const override;
    LibraryItem libraryItem() const override;
    QString saveProblem() const override;
    QString saveWarning() const override;
    QStringList partsUsing(const Library &library, const QString &name) const override;
    Part partRenamed(const Part &part, const QString &from, const QString &to) const override;
    QString nameLabel() const override;
    QString takenQuestion(const QString &name) const override;
    QString strandedQuestion(const QString &name, const QStringList &parts) const override;
    QString unsavedQuestion() const override;
    QString renamedMessage(const QString &from, const QString &to) const override;

private:
    // The form, the drawings and the check show the part again.
    void refresh();
    void showDrawings();
    // The text fields put into the part, one step when they changed it.
    void takeFields();

    Part m_part;
    const Library *m_library = nullptr;
    // What the drawings show: copies, the library may change under them.
    Symbol m_shownSymbol;
    Footprint m_shownFootprint;
    bool m_symbolShown = false;
    bool m_footprintShown = false;
    QPushButton *m_symbol = nullptr;
    // The units after the first (Part::units); the one chosen is drawn.
    QListWidget *m_units = nullptr;
    QPushButton *m_removeUnit = nullptr;
    QListWidget *m_footprints = nullptr;
    QPushButton *m_remove = nullptr;
    QPushButton *m_up = nullptr;
    QPushButton *m_down = nullptr;
    QLineEdit *m_prefix = nullptr;
    QLineEdit *m_prefixGost = nullptr;
    QLineEdit *m_value = nullptr;
    QComboBox *m_group = nullptr;
    QComboBox *m_power = nullptr;
    QCheckBox *m_connector = nullptr;
    QCheckBox *m_noValue = nullptr;
    NameField *m_name = nullptr;
    QLabel *m_check = nullptr;
    PartPreview *m_preview = nullptr;
};

} // namespace ppcb
