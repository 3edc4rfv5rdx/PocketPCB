#include "contacts.h"

#include "netlist.h"
#include "sheetedit.h"
#include "trackedit.h"
#include "wiring.h"

#include <QHash>

#include <algorithm>

namespace ppcb {

const char WIRE_PAD_PART[] = "wire-pad";

namespace {

// The turn that points a symbol's +X along 'direction', one of the four
// across and up and down; none for no direction.
int rotationAlong(const Point &direction)
{
    if (direction.x < 0)
        return 180;
    if (direction.y > 0)
        return 90;
    if (direction.y < 0)
        return 270;
    return 0;
}

bool isOnBoard(const Component &component)
{
    return !component.offBoard && !component.footprint.isEmpty();
}

} // namespace

PlacedContacts withContactsPlaced(const Design &design, const Library &source,
                                  const QSet<ObjectId> &only)
{
    PlacedContacts result{design, {}};
    Design &changed = result.design;
    changed.library = withPartFrom(changed.library, source, QLatin1String(WIRE_PAD_PART));
    const Part *part = changed.library.findPart(QLatin1String(WIRE_PAD_PART));
    const Symbol *symbol = changed.library.findSymbolOfPart(QLatin1String(WIRE_PAD_PART));
    if (part == nullptr || symbol == nullptr || symbol->pins.size() != 1
        || part->footprints.isEmpty())
        return {design, {}};
    const Point padPin = symbol->pins.first().at;
    const QString footprint = part->footprints.first();

    // Where each component is: its sheet and its index there.
    QHash<ObjectId, QPair<int, int>> where;
    const QList<Sheet> &sheets = changed.schematic.sheets;
    for (int s = 0; s < sheets.size(); ++s) {
        for (int c = 0; c < sheets.at(s).components.size(); ++c)
            where.insert(sheets.at(s).components.at(c).id, {s, c});
    }
    const QStringList links = linkDesignators(changed.board);
    const QList<Net> all = nets(design.schematic, changed.library, design.project);
    for (const Net &net : all) {
        bool reaches = false;
        bool hasContact = false;
        const NetPin *chosen = nullptr;
        for (const NetPin &pin : net.pins) {
            const auto found = where.constFind(pin.component);
            if (found == where.cend())
                continue;
            const Component &component = sheets.at(found->first).components.at(found->second);
            if (isOnBoard(component)) {
                reaches = true;
                const Part *own = changed.library.findPart(component.part);
                if (own != nullptr && own->connector)
                    hasContact = true;
            } else if (component.offBoard && chosen == nullptr
                       && (only.isEmpty() || only.contains(component.id))) {
                chosen = &pin;
            }
        }
        if (!reaches || hasContact || chosen == nullptr)
            continue;
        const int sheet = where.value(chosen->component).first;
        const QList<SheetPin> pins = sheetPins(changed.schematic.sheets.at(sheet), changed.library);
        const auto pin = std::find_if(pins.cbegin(), pins.cend(), [chosen](const SheetPin &each) {
            return each.owner == chosen->component && each.number == chosen->number;
        });
        if (pin == pins.cend())
            continue;
        Component contact = newComponent(changed.library, QLatin1String(WIRE_PAD_PART), footprint);
        contact.rotation = rotationAlong(pin->at - pin->bodyEnd);
        contact.at = pin->at - placedPoint(Point(), contact.rotation, false, padPin);
        // Its texts turned with it, off the wire.
        contact.refAt = placedPoint(Point(), contact.rotation, false, contact.refAt);
        contact.valueAt = placedPoint(Point(), contact.rotation, false, contact.valueAt);
        Sheet piece;
        piece.components.append(contact);
        piece = numberedOn(piece, changed.schematic, changed.library,
                           changed.project.designatorStyle, links);
        changed.schematic.sheets[sheet].components.append(piece.components.first());
        result.placed.insert(contact.id);
    }
    if (result.placed.isEmpty())
        return {design, {}};
    return result;
}

bool isWirePad(const Placement &placement)
{
    return placement.footprint.startsWith(QLatin1String(WIRE_PAD_PART));
}

QString wirePadLabel(const Placement &placement, const QList<Net> &nets,
                     const Schematic &schematic)
{
    QSet<ObjectId> offBoard;
    for (const Sheet &sheet : schematic.sheets) {
        for (const Component &component : sheet.components) {
            if (component.offBoard)
                offBoard.insert(component.id);
        }
    }
    QStringList parts;
    // padNets is kept in the order of the pad numbers.
    for (const QString &netName : placement.padNets) {
        const auto net = std::find_if(nets.cbegin(), nets.cend(),
                                      [&netName](const Net &each) { return each.name == netName; });
        QList<NetPin> far;
        if (net != nets.cend()) {
            for (const NetPin &pin : net->pins) {
                if (offBoard.contains(pin.component))
                    far.append(pin);
            }
        }
        const QString text = far.isEmpty() ? netName : pinsText(far);
        if (!text.isEmpty() && !parts.contains(text))
            parts.append(text);
    }
    return parts.join(QStringLiteral(", "));
}

Board withWirePadLabels(const Design &design)
{
    Board board = design.board;
    const bool any = std::any_of(board.placements.cbegin(), board.placements.cend(),
                                 [](const Placement &p) { return isWirePad(p); });
    // The nets are made only for a board that has wire pads.
    const QList<Net> all = any ? nets(design.schematic, design.library, design.project)
                               : QList<Net>();
    for (Placement &placement : board.placements)
        placement.padLabel =
            isWirePad(placement) ? wirePadLabel(placement, all, design.schematic) : QString();
    return board;
}

} // namespace ppcb
