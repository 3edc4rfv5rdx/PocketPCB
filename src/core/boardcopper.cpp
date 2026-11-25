#include "boardcopper.h"

#include "project.h"
#include "ratsnest.h"

namespace ppcb {

namespace {

const int QUARTER_TURN = 90;

// True when a placement turned by 'rotation' lays its pads' X along the
// board's Y.
bool turnedAcross(int rotation)
{
    return ((rotation / QUARTER_TURN) % 2) != 0;
}

} // namespace

QList<CopperPad> copperPads(const Board &board, const Library &library, BoardSide side)
{
    QList<CopperPad> pads;
    for (const Placement &placement : board.placements) {
        const Footprint *footprint = library.findFootprint(placement.footprint);
        if (footprint == nullptr)
            continue;
        const bool across = turnedAcross(placement.rotation);
        for (const Pad &pad : footprint->pads) {
            if (!copperOn(padCopperSides(pad.layers, placement.side), side))
                continue;
            CopperPad copper;
            copper.kind = CopperPad::Kind::Part;
            copper.at = placedPadPoint(placement, pad.at);
            copper.shape = pad.shape;
            copper.sizeX = across ? pad.sizeY : pad.sizeX;
            copper.sizeY = across ? pad.sizeX : pad.sizeY;
            copper.drill = pad.hole;
            pads.append(copper);
        }
    }
    const auto round = [](CopperPad::Kind kind, const Point &at, Coord size, Coord drill) {
        CopperPad copper;
        copper.kind = kind;
        copper.at = at;
        copper.sizeX = copper.sizeY = size;
        copper.drill = drill;
        return copper;
    };
    for (const Via &via : board.vias)
        pads.append(round(CopperPad::Kind::Via, via.at, via.size, via.drill));
    if (copperOn(board.copper, side)) {
        for (const WireLink &link : board.links) {
            for (const Point &end : {link.from, link.to})
                pads.append(round(CopperPad::Kind::Link, end, link.size, link.drill));
        }
    }
    for (const MountingHole &hole : board.holes) {
        if (hole.ring > 0 && holePadOn(board, hole, side))
            pads.append(round(CopperPad::Kind::Hole, hole.at, holeOuterDiameter(hole), hole.drill));
    }
    return pads;
}

QList<DrillHole> drillHoles(const Board &board, const Library &library, const Rules &rules)
{
    QList<DrillHole> holes;
    const auto add = [&holes, &rules](const Point &at, Coord diameter, bool plated) {
        if (diameter > 0)
            holes.append(DrillHole{at, drilledDiameter(diameter, rules), plated});
    };
    for (const Placement &placement : board.placements) {
        const Footprint *footprint = library.findFootprint(placement.footprint);
        if (footprint == nullptr)
            continue;
        for (const Pad &pad : footprint->pads)
            add(placedPadPoint(placement, pad.at), pad.hole, true);
    }
    for (const Via &via : board.vias)
        add(via.at, via.drill, true);
    for (const WireLink &link : board.links) {
        add(link.from, link.drill, true);
        add(link.to, link.drill, true);
    }
    for (const MountingHole &hole : board.holes)
        add(hole.at, hole.drill, hole.plated);
    return holes;
}

QMap<Coord, int> drillCounts(const QList<DrillHole> &holes)
{
    QMap<Coord, int> counts;
    for (const DrillHole &hole : holes)
        ++counts[hole.diameter];
    return counts;
}

Coord drilledDiameter(Coord diameter, const Rules &rules)
{
    if (!rules.drillsOnly)
        return diameter;
    for (const Coord drill : rules.drills) {
        if (drill >= diameter)
            return drill;
    }
    return diameter;
}

} // namespace ppcb
