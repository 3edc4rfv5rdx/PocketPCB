#include "contacts.h"

#include "netlist.h"
#include "sheetedit.h"
#include "trackedit.h"
#include "wiring.h"

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

// A wire pad's footprint and a connector part: a contact for another
// part's wire (isWirePad()).
bool isWirePadOf(const QString &footprint, const QString &partName, const Library &library)
{
    const Part *part = library.findPart(partName);
    return part != nullptr && part->connector
           && footprint.startsWith(QLatin1String(WIRE_PAD_PART));
}

// A wire pad on the schematic.
bool isWirePadComponent(const Component &component, const Library &library)
{
    return isWirePadOf(component.footprint, component.part, library);
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

    const QStringList links = linkDesignators(changed.board);
    for (int s = 0; s < design.schematic.sheets.size(); ++s) {
        // The sheet as it was: the pins of its off-board components asked
        // for, and the points where the pins of its wire pads are.
        const Sheet &sheet = design.schematic.sheets.at(s);
        QSet<ObjectId> far;
        QSet<ObjectId> wirePads;
        for (const Component &component : sheet.components) {
            if (component.offBoard && (only.isEmpty() || only.contains(component.id)))
                far.insert(component.id);
            if (isWirePadComponent(component, changed.library))
                wirePads.insert(component.id);
        }
        const QList<SheetPin> pins = sheetPins(sheet, changed.library);
        // The points that take no wire: a wire pad's there already, or a
        // no-connect mark.
        QSet<QPair<Coord, Coord>> padded;
        for (const SheetPin &pin : pins) {
            if (wirePads.contains(pin.owner))
                padded.insert({pin.at.x, pin.at.y});
        }
        for (const NoConnect &mark : sheet.noConnects)
            padded.insert({mark.at.x, mark.at.y});
        for (const SheetPin &pin : pins) {
            if (!far.contains(pin.owner) || padded.contains({pin.at.x, pin.at.y}))
                continue;
            Component contact =
                newComponent(changed.library, QLatin1String(WIRE_PAD_PART), footprint);
            contact.rotation = rotationAlong(pin.at - pin.bodyEnd);
            contact.at = pin.at - placedPoint(Point(), contact.rotation, false, padPin);
            // Its texts turned with it, off the wire.
            contact.refAt = placedPoint(Point(), contact.rotation, false, contact.refAt);
            contact.valueAt = placedPoint(Point(), contact.rotation, false, contact.valueAt);
            Sheet piece;
            piece.components.append(contact);
            piece = numberedOn(piece, changed.schematic, changed.library,
                               changed.project.designatorStyle, links);
            changed.schematic.sheets[s].components.append(piece.components.first());
            result.placed.insert(contact.id);
        }
    }
    if (result.placed.isEmpty())
        return {design, {}};
    return result;
}

bool isWirePad(const Placement &placement, const Design &design)
{
    for (const Sheet &sheet : design.schematic.sheets) {
        for (const Component &component : sheet.components) {
            if (component.id == placement.component)
                return isWirePadOf(placement.footprint, component.part, design.library);
        }
    }
    return false;
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
    QList<bool> labelled;
    for (const Placement &placement : board.placements)
        labelled.append(isWirePad(placement, design));
    const bool any = labelled.contains(true);
    // The nets are made only for a board that has wire pads.
    const QList<Net> all = any ? nets(design.schematic, design.library, design.project)
                               : QList<Net>();
    for (int i = 0; i < board.placements.size(); ++i) {
        Placement &placement = board.placements[i];
        placement.padLabel =
            labelled.at(i) ? wirePadLabel(placement, all, design.schematic) : QString();
    }
    return board;
}

} // namespace ppcb
