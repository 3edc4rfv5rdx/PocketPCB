#include "boardupdate.h"

#include "bom.h"
#include "grid.h"
#include "netlist.h"
#include "paint.h"

#include <QHash>

#include <algorithm>

namespace ppcb {

namespace {

// How tall a column of new placements may grow on a board without an outline.
const Coord DEFAULT_COLUMN_HEIGHT = 50 * NM_PER_MM;

// The key of a pin of a component among the nets.
QString pinKey(const ObjectId &component, const QString &number)
{
    return idToString(component) + QLatin1Char('.') + number;
}

// The net of every pad of 'footprint' joined to a pin of 'component' that
// is on a net.
QMap<QString, QString> padNetsOf(const Footprint &footprint, const ObjectId &component,
                                 const QHash<QString, QString> &pinNets)
{
    QMap<QString, QString> nets;
    for (const Pad &pad : footprint.pads) {
        const auto it = pinNets.constFind(pinKey(component, pad.number));
        if (!pad.number.isEmpty() && it != pinNets.constEnd())
            nets.insert(pad.number, it.value());
    }
    return nets;
}

} // namespace

bool operator==(const BoardChange &a, const BoardChange &b)
{
    return a.kind == b.kind && a.designator == b.designator && a.before == b.before
           && a.after == b.after;
}

BoardUpdate boardUpdatedFromSchematic(const Design &design)
{
    const Library &library = design.library;
    QHash<QString, QString> pinNets;
    for (const Net &net : nets(design.schematic, library, design.project)) {
        for (const NetPin &pin : net.pins)
            pinNets.insert(pinKey(pin.component, pin.number), net.name);
    }

    // The components that belong on the board, in the order of the sheets.
    // Those whose footprint the library lacks belong there too, but cannot
    // be placed: their placements are left alone.
    QList<const Component *> wanted;
    QSet<ObjectId> wantedIds;
    QSet<ObjectId> leftAlone;
    QList<BoardChange> changes;
    for (const Sheet &sheet : design.schematic.sheets) {
        for (const Component &component : sheet.components) {
            if (component.offBoard || component.footprint.isEmpty())
                continue;
            if (library.findFootprint(component.footprint) == nullptr) {
                leftAlone.insert(component.id);
                const bool placed =
                    std::any_of(design.board.placements.cbegin(), design.board.placements.cend(),
                                [&component](const Placement &placement) {
                                    return placement.component == component.id;
                                });
                changes.append(BoardChange{placed ? BoardChangeKind::MissingFootprint
                                                  : BoardChangeKind::MissingFootprintNotAdded,
                                           component.designator, QString(),
                                           component.footprint});
                continue;
            }
            wanted.append(&component);
            wantedIds.insert(component.id);
        }
    }

    BoardUpdate update;
    update.board = design.board;
    QList<Placement> &placements = update.board.placements;

    for (int i = int(placements.size()) - 1; i >= 0; --i) {
        const ObjectId &component = placements.at(i).component;
        if (!wantedIds.contains(component) && !leftAlone.contains(component)) {
            changes.append(BoardChange{BoardChangeKind::Remove, placements.at(i).designator,
                                       placements.at(i).footprint, QString()});
            placements.removeAt(i);
        }
    }

    QList<const Component *> added;
    for (const Component *component : std::as_const(wanted)) {
        const auto found = std::find_if(placements.begin(), placements.end(),
                                        [component](const Placement &placement) {
                                            return placement.component == component->id;
                                        });
        if (found == placements.end()) {
            added.append(component);
            continue;
        }
        Placement &placement = *found;
        const Footprint &footprint = *library.findFootprint(component->footprint);
        const QString &name = component->designator;
        if (placement.designator != name) {
            changes.append(
                BoardChange{BoardChangeKind::Designator, name, placement.designator, name});
            placement.designator = name;
        }
        if (placement.value != component->value) {
            changes.append(BoardChange{BoardChangeKind::Value, name, placement.value,
                                       component->value});
            placement.value = component->value;
        }
        if (placement.footprint != component->footprint) {
            changes.append(BoardChange{BoardChangeKind::Footprint, name, placement.footprint,
                                       component->footprint});
            placement.footprint = component->footprint;
        }
        const QMap<QString, QString> padNets = padNetsOf(footprint, component->id, pinNets);
        if (placement.padNets != padNets) {
            changes.append(BoardChange{BoardChangeKind::Nets, name, QString(), QString()});
            placement.padNets = padNets;
        }
    }

    // The new ones go to the right of what is on the board, from the top of
    // the outline down, in the order of their designators.
    std::stable_sort(added.begin(), added.end(), [](const Component *a, const Component *b) {
        return designatorLess(a->designator, b->designator);
    });
    QPolygonF outlineCorners;
    for (const OutlineCorner &corner : update.board.outline)
        outlineCorners.append(toMm(corner.at));
    const QRectF outline = outlineCorners.boundingRect();
    const bool empty = update.board.outline.isEmpty() && placements.isEmpty();
    const QRectF taken = empty ? QRectF() : boardBounds(update.board, library);
    const Coord grid = design.project.boardGridStep;
    const Coord gap = NEW_PLACEMENT_GAP;
    const Coord top = update.board.outline.isEmpty() ? (empty ? 0 : mmToNm(taken.top()))
                                                     : mmToNm(outline.top());
    const Coord columnHeight = update.board.outline.isEmpty() ? DEFAULT_COLUMN_HEIGHT
                                                              : mmToNm(outline.height());
    Coord x = empty ? 0 : mmToNm(taken.right()) + gap;
    Coord y = top;
    Coord columnWidth = 0;
    for (const Component *component : std::as_const(added)) {
        const Footprint &footprint = *library.findFootprint(component->footprint);
        const QRectF bounds = footprintBounds(footprint);
        const Coord width = mmToNm(bounds.width());
        const Coord height = mmToNm(bounds.height());
        if (y > top && y + height > top + columnHeight) {
            x += columnWidth + gap;
            y = top;
            columnWidth = 0;
        }
        Placement placement;
        placement.id = newObjectId();
        placement.component = component->id;
        placement.footprint = component->footprint;
        placement.designator = component->designator;
        placement.value = component->value;
        placement.at = Point{snapToGrid(x - mmToNm(bounds.left()), grid),
                             snapToGrid(y - mmToNm(bounds.top()), grid)};
        placement.refAt = footprint.refAt;
        placement.valueAt = footprint.valueAt;
        placement.padNets = padNetsOf(footprint, component->id, pinNets);
        placements.append(placement);
        changes.append(BoardChange{BoardChangeKind::Add, component->designator, QString(),
                                   placement.footprint});
        y += height + gap;
        columnWidth = std::max(columnWidth, width);
    }

    std::stable_sort(changes.begin(), changes.end(),
                     [](const BoardChange &a, const BoardChange &b) {
                         if (a.designator != b.designator)
                             return designatorLess(a.designator, b.designator);
                         return int(a.kind) < int(b.kind);
                     });
    update.changes = changes;
    return update;
}

} // namespace ppcb
