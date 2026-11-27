#include "itemchooser.h"

#include "librarypanel.h"

#include <QAbstractButton>
#include <QButtonGroup>
#include <QDialog>
#include <QHash>
#include <QHBoxLayout>
#include <QLineEdit>
#include <QListWidget>
#include <QRadioButton>
#include <QVBoxLayout>

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

QString ItemChooser::choose(QWidget *parent, const QString &title, const Library &library,
                            const QList<LibraryKind> &kinds, LibraryKind &kind,
                            QDialogButtonBox::StandardButton accept, const QString &current)
{
    if (kinds.isEmpty())
        return QString();
    if (!kinds.contains(kind))
        kind = kinds.first();
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
        kindRow->addStretch();
    }
    QLineEdit *search = new QLineEdit(&dialog);
    search->setObjectName(QStringLiteral("chooseItemSearch"));
    search->setClearButtonEnabled(true);
    QListWidget *list = new QListWidget(&dialog);
    list->setObjectName(QStringLiteral("chooseItemList"));
    PartPreview *preview = new PartPreview(&dialog);
    preview->setObjectName(QStringLiteral("chooseItemPreview"));
    preview->setMinimumWidth(PREVIEW_SIZE);
    const auto shown = [list] {
        const QListWidgetItem *item = list->currentItem();
        return item != nullptr && !item->isHidden() ? item->text() : QString();
    };
    // 'kind' is captured by reference: the switch keeps it up to date.
    const auto draw = [preview, &library, &kind, shown] {
        const QString name = shown();
        switch (kind) {
        case LibraryKind::Symbol:
            preview->setAlone(true);
            preview->setContent(library.findSymbol(name), nullptr);
            break;
        case LibraryKind::Footprint:
            preview->setAlone(true);
            preview->setContent(nullptr, library.findFootprint(name));
            break;
        case LibraryKind::Part: {
            const Part *part = library.findPart(name);
            const Footprint *footprint = part != nullptr && !part->footprints.isEmpty()
                                             ? library.findFootprint(part->footprints.first())
                                             : nullptr;
            preview->setAlone(false);
            preview->setContent(library.findSymbolOfPart(name), footprint);
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
    const auto fill = [list, &library, &kind, filter](const QString &wanted) {
        list->clear();
        list->addItems(namesOf(library, kind));
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
    fill(current);
    QDialogButtonBox *buttons = new QDialogButtonBox(accept | QDialogButtonBox::Cancel, &dialog);
    QObject::connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    QObject::connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    QObject::connect(list, &QListWidget::itemActivated, &dialog, &QDialog::accept);
    QHBoxLayout *choice = new QHBoxLayout;
    choice->addWidget(list, 1);
    choice->addWidget(preview, 1);
    QVBoxLayout *layout = new QVBoxLayout(&dialog);
    if (kinds.size() > 1)
        layout->addLayout(kindRow);
    else
        delete kindRow;
    layout->addWidget(search);
    layout->addLayout(choice, 1);
    layout->addWidget(buttons);
    search->setFocus();
    const bool accepted = dialog.exec() == QDialog::Accepted;
    const QString name = shown();
    // The preview points into the library: nothing is drawn any more.
    preview->setContent(nullptr, nullptr);
    return accepted ? name : QString();
}

} // namespace ppcb
