#include "itemchooser.h"

#include "librarypanel.h"
#include "sheeteditor.h"

#include <QAbstractButton>
#include <QButtonGroup>
#include <QComboBox>
#include <QDialog>
#include <QHash>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QPushButton>
#include <QRadioButton>
#include <QVBoxLayout>

#include <algorithm>

namespace ppcb {

namespace {

// The least width of the drawing, in pixels.
const int PREVIEW_SIZE = 200;

QStringList namesOf(const Library &library, LibraryKind kind)
{
    switch (kind) {
    case LibraryKind::Symbol:
        return library.symbols.keys();
    case LibraryKind::Footprint:
        return library.footprints.keys();
    case LibraryKind::Part:
        return library.parts.keys();
    }
    return {};
}

const char *kindObjectName(LibraryKind kind)
{
    switch (kind) {
    case LibraryKind::Symbol:
        return "chooseSymbols";
    case LibraryKind::Footprint:
        return "chooseFootprints";
    case LibraryKind::Part:
        return "chooseParts";
    }
    return "";
}

} // namespace

QString ItemChooser::kindTitle(LibraryKind kind)
{
    switch (kind) {
    case LibraryKind::Symbol:
        return tr("Symbols");
    case LibraryKind::Footprint:
        return tr("Footprints");
    case LibraryKind::Part:
        return tr("Parts");
    }
    return QString();
}

QString ItemChooser::sourceTitle(Source source)
{
    switch (source) {
    case Source::All:
        return tr("All");
    case Source::BuiltIn:
        return tr("Built-in");
    case Source::User:
        return tr("User");
    }
    return QString();
}

QString ItemChooser::choose(QWidget *parent, const QString &title, const Library &library,
                            const QList<LibraryKind> &kinds, LibraryKind &kind,
                            QDialogButtonBox::StandardButton accept, const QString &current,
                            Options *options)
{
    if (kinds.isEmpty())
        return QString();
    if (!kinds.contains(kind))
        kind = kinds.first();
    const bool switchable =
        options != nullptr && options->builtIn != nullptr && options->user != nullptr;
    Source source = switchable ? options->source : Source::All;
    // The library the list shows now.
    const auto shownLibrary = [&library, &source, options]() -> const Library & {
        switch (source) {
        case Source::All:
            break;
        case Source::BuiltIn:
            return *options->builtIn;
        case Source::User:
            return *options->user;
        }
        return library;
    };

    QDialog dialog(parent);
    dialog.setWindowTitle(title);
    QHBoxLayout *kindRow = new QHBoxLayout;
    QButtonGroup switches(&dialog);
    QHash<QAbstractButton *, LibraryKind> kindOf;
    if (kinds.size() > 1) {
        for (const LibraryKind each : kinds) {
            QRadioButton *button = new QRadioButton(kindTitle(each), &dialog);
            button->setObjectName(QLatin1String(kindObjectName(each)));
            button->setChecked(each == kind);
            switches.addButton(button);
            kindOf.insert(button, each);
            kindRow->addWidget(button);
        }
    }
    kindRow->addStretch();
    QComboBox *sources = nullptr;
    if (switchable) {
        sources = new QComboBox(&dialog);
        sources->setObjectName(QStringLiteral("chooseSource"));
        for (const Source each : {Source::All, Source::BuiltIn, Source::User})
            sources->addItem(sourceTitle(each), int(each));
        sources->setCurrentIndex(sources->findData(int(source)));
        kindRow->addWidget(new QLabel(tr("Library") + QLatin1Char(':'), &dialog));
        kindRow->addWidget(sources);
    }
    QLineEdit *search = new QLineEdit(&dialog);
    search->setObjectName(QStringLiteral("chooseItemSearch"));
    search->setClearButtonEnabled(true);
    QListWidget *list = new QListWidget(&dialog);
    list->setObjectName(QStringLiteral("chooseItemList"));
    QPushButton *remove = nullptr;
    if (options != nullptr && options->remove) {
        remove = new QPushButton(
            SheetEditor::actionName(SheetEditor::Action::Delete) + QStringLiteral("…"), &dialog);
        remove->setObjectName(QStringLiteral("chooseItemDelete"));
        remove->setToolTip(tr("Deletes the one chosen from your library"));
    }
    PartPreview *preview = new PartPreview(&dialog);
    preview->setObjectName(QStringLiteral("chooseItemPreview"));
    preview->setMinimumWidth(PREVIEW_SIZE);
    const auto shown = [list] {
        const QListWidgetItem *item = list->currentItem();
        return item != nullptr && !item->isHidden() ? item->text() : QString();
    };
    // 'kind' and 'source' are captured by reference: the switches keep them
    // up to date.
    const auto draw = [preview, &kind, &source, remove, shown, shownLibrary] {
        const QString name = shown();
        const Library &from = shownLibrary();
        if (remove != nullptr)
            remove->setEnabled(source == Source::User && !name.isEmpty());
        switch (kind) {
        case LibraryKind::Symbol:
            preview->setAlone(true);
            preview->setContent(from.findSymbol(name), nullptr);
            break;
        case LibraryKind::Footprint:
            preview->setAlone(true);
            preview->setContent(nullptr, from.findFootprint(name));
            break;
        case LibraryKind::Part: {
            const Part *part = from.findPart(name);
            const Footprint *footprint = part != nullptr && !part->footprints.isEmpty()
                                             ? from.findFootprint(part->footprints.first())
                                             : nullptr;
            preview->setAlone(false);
            preview->setContent(from.findSymbolOfPart(name), footprint);
            break;
        }
        }
    };
    // The list holds the names with the text searched for.
    const auto filter = [list, search, draw](const QString &wanted) {
        QListWidgetItem *first = nullptr;
        QListWidgetItem *preferred = nullptr;
        for (int i = 0; i < list->count(); ++i) {
            QListWidgetItem *item = list->item(i);
            const bool match = item->text().contains(search->text().trimmed(), Qt::CaseInsensitive);
            item->setHidden(!match);
            if (match && first == nullptr)
                first = item;
            if (match && item->text() == wanted)
                preferred = item;
        }
        list->setCurrentItem(preferred != nullptr ? preferred : first);
        draw();
    };
    const auto fill = [list, &kind, filter, shownLibrary](const QString &wanted) {
        list->clear();
        list->addItems(namesOf(shownLibrary(), kind));
        filter(wanted);
    };
    QObject::connect(list, &QListWidget::currentItemChanged, &dialog, draw);
    QObject::connect(search, &QLineEdit::textChanged, &dialog, [filter] { filter(QString()); });
    QObject::connect(&switches, &QButtonGroup::buttonToggled, &dialog,
                     [&kind, &kindOf, fill](QAbstractButton *button, bool checked) {
                         if (!checked)
                             return;
                         kind = kindOf.value(button);
                         fill(QString());
                     });
    if (sources != nullptr) {
        QObject::connect(sources, &QComboBox::activated, &dialog,
                         [sources, &source, shown, fill](int index) {
                             const QString was = shown();
                             source = Source(sources->itemData(index).toInt());
                             fill(was);
                         });
    }
    if (remove != nullptr) {
        QObject::connect(remove, &QPushButton::clicked, &dialog,
                         [preview, &kind, options, shown, fill, list] {
                             const QString name = shown();
                             const int row = list->currentRow();
                             // The drawing points into the libraries about to
                             // be read again.
                             preview->setContent(nullptr, nullptr);
                             options->remove(kind, name);
                             fill(QString());
                             // The one after it chosen, as a list goes on.
                             if (list->count() > 0)
                                 list->setCurrentRow(std::min(row, list->count() - 1));
                         });
    }
    fill(current);
    QDialogButtonBox *buttons = new QDialogButtonBox(accept | QDialogButtonBox::Cancel, &dialog);
    QObject::connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    QObject::connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    QObject::connect(list, &QListWidget::itemActivated, &dialog, &QDialog::accept);
    QHBoxLayout *choice = new QHBoxLayout;
    choice->addWidget(list, 1);
    choice->addWidget(preview, 1);
    // Delete at the left of the row of buttons, away from Open.
    QHBoxLayout *buttonRow = new QHBoxLayout;
    if (remove != nullptr)
        buttonRow->addWidget(remove);
    buttonRow->addStretch();
    buttonRow->addWidget(buttons);
    QVBoxLayout *layout = new QVBoxLayout(&dialog);
    if (kinds.size() > 1 || sources != nullptr)
        layout->addLayout(kindRow);
    else
        delete kindRow;
    layout->addWidget(search);
    layout->addLayout(choice, 1);
    layout->addLayout(buttonRow);
    search->setFocus();
    const bool accepted = dialog.exec() == QDialog::Accepted;
    const QString name = shown();
    // The preview points into the library: nothing is drawn any more.
    preview->setContent(nullptr, nullptr);
    if (options != nullptr)
        options->source = source;
    return accepted ? name : QString();
}

} // namespace ppcb
