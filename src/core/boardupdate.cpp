#include "boardupdate.h"

#include "bom.h"
#include "grid.h"
#include "netlist.h"
#include "paint.h"
#include "trackedit.h"

#include <QHash>

#include <algorithm>
#include <cmath>

namespace ppcb {

namespace {

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

QHash<QString, QString> holeNetsFollowed(const Board &before, const Board &after)
{
    QHash<ObjectId, const Placement *> afterById;
    QSet<QString> afterNets;
    for (const Placement &placement : after.placements) {
        afterById.insert(placement.id, &placement);
        for (const QString &net : placement.padNets)
            afterNets.insert(net);
    }
    QSet<QString> holeNets;
    for (const MountingHole &hole : before.holes) {
        if (!hole.net.isEmpty())
            holeNets.insert(hole.net);
    }
    QHash<QString, QString> followed;
    for (const QString &net : std::as_const(holeNets)) {
        // How many pads that were on 'net' are on each net now.
        QMap<QString, int> votes;
        for (const Placement &old : before.placements) {
            const Placement *now = afterById.value(old.id);
            if (now == nullptr)
                continue;
            for (auto it = old.padNets.cbegin(); it != old.padNets.cend(); ++it) {
                const QString next = now->padNets.value(it.key());
                if (it.value() == net && !next.isEmpty())
                    ++votes[next];
            }
        }
        QString chosen;
        int most = 0;
        // The map goes by name: the first of equals wins.
        for (auto it = votes.cbegin(); it != votes.cend(); ++it) {
            if (it.value() > most) {
                most = it.value();
                chosen = it.key();
            }
        }
        if (chosen.isEmpty() && afterNets.contains(net))
            chosen = net;
        followed.insert(net, chosen);
    }
    return followed;
}

BoardUpdate boardUpdatedFromSchematic(const Design &design)
{
    const Library &library = design.library;
    // Every net with pins gets a name that stays when the schematic is
    // edited, then the nets are found again by those names.
    const Project project =
        withNetsNamedByPins(design.project, nets(design.schematic, library, design.project));
    // Two nets with pins under one name would be one net on the board: the
    // update waits until a label or a power symbol is named otherwise.
    QHash<QString, QString> pinNets;
    QHash<QString, int> netsOfName;
    for (const Net &net : nets(design.schematic, library, project)) {
        if (!net.pins.isEmpty())
            ++netsOfName[net.name];
        for (const NetPin &pin : net.pins)
            pinNets.insert(pinKey(pin.component, pin.number), net.name);
    }
    QStringList sharedNames;
    for (auto it = netsOfName.cbegin(); it != netsOfName.cend(); ++it) {
        if (it.value() > 1)
            sharedNames.append(it.key());
    }
    if (!sharedNames.isEmpty()) {
        std::sort(sharedNames.begin(), sharedNames.end(), naturalLess);
        BoardUpdate refused;
        refused.board = design.board;
        refused.project = design.project;
        refused.sharedNames = sharedNames;
        return refused;
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
    update.project = project;
    QList<Placement> &placements = update.board.placements;

    // A placement removed takes its tracks with it (tracksOfPads()).
    QSet<ObjectId> removed;
    for (int i = int(placements.size()) - 1; i >= 0; --i) {
        const ObjectId &component = placements.at(i).component;
        if (!wantedIds.contains(component) && !leftAlone.contains(component)) {
            changes.append(BoardChange{BoardChangeKind::Remove, placements.at(i).designator,
                                       placements.at(i).footprint, QString()});
            removed.insert(placements.at(i).id);
            placements.removeAt(i);
        }
    }
    update.board = withoutTracks(update.board, tracksOfPads(design.board, library, removed));

    QList<const Component *> added;
    // The placements given other footprints: their tracks go along.
    QSet<ObjectId> refooted;
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
            refooted.insert(placement.id);
        }
        const QMap<QString, QString> padNets = padNetsOf(footprint, component->id, pinNets);
        if (placement.padNets != padNets) {
            changes.append(BoardChange{BoardChangeKind::Nets, name, QString(), QString()});
            placement.padNets = padNets;
        }
    }

    update.board = withTracksOnNewFootprints(design.board, update.board, library, refooted);
    // A track stretched onto copper of another net goes back to the ratsnest.
    Design updated = design;
    updated.project = project;
    update.board = withoutShortingStretches(design.board, update.board, updated);

    // The pads of the holes follow their nets, said once for each net.
    const QHash<QString, QString> holeNets = holeNetsFollowed(design.board, update.board);
    for (auto it = holeNets.cbegin(); it != holeNets.cend(); ++it) {
        if (it.key() != it.value())
            changes.append(BoardChange{BoardChangeKind::HoleNet, QString(), it.key(), it.value()});
    }
    for (MountingHole &hole : update.board.holes) {
        if (!hole.net.isEmpty())
            hole.net = holeNets.value(hole.net);
    }

    // The new ones join the list in the order of their designators, and go
    // to the right of what is on the board (withPlacementsPacked()).
    std::stable_sort(added.begin(), added.end(), [](const Component *a, const Component *b) {
        return designatorLess(a->designator, b->designator);
    });
    QSet<ObjectId> addedIds;
    for (const Component *component : std::as_const(added)) {
        const Footprint &footprint = *library.findFootprint(component->footprint);
        Placement placement;
        placement.id = newObjectId();
        placement.component = component->id;
        placement.footprint = component->footprint;
        placement.designator = component->designator;
        placement.value = component->value;
        placement.refAt = footprint.refAt;
        placement.valueAt = footprint.valueAt;
        placement.padNets = padNetsOf(footprint, component->id, pinNets);
        placements.append(placement);
        addedIds.insert(placement.id);
        changes.append(BoardChange{BoardChangeKind::Add, component->designator, QString(),
                                   placement.footprint});
    }
    update.board = withPlacementsPacked(update.board, library, addedIds,
                                        design.project.boardGridStep);

    std::stable_sort(changes.begin(), changes.end(),
                     [](const BoardChange &a, const BoardChange &b) {
                         if (a.designator != b.designator)
                             return designatorLess(a.designator, b.designator);
                         if (a.kind != b.kind)
                             return int(a.kind) < int(b.kind);
                         return a.before < b.before;
                     });
    update.changes = changes;
    return update;
}

} // namespace ppcb
