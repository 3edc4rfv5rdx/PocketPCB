#include "erc.h"

#include "joins.h"
#include "wiring.h"

#include <QHash>

#include <algorithm>
#include <climits>

namespace ppcb {

namespace {

// Where to look a pin up: its component and number.
QString pinKey(const ObjectId &component, const QString &number)
{
    return idToString(component) + QLatin1Char('.') + number;
}

ErcProblem problem(ErcKind kind, int sheet, const Point &at, const ObjectId &object,
                   const QString &subject)
{
    ErcProblem result;
    result.kind = kind;
    result.sheet = sheet;
    result.at = at;
    result.object = object;
    result.subject = subject;
    return result;
}

// The ids of what gives a net a name: labels and power symbols.
QSet<ObjectId> nameGivers(const Schematic &schematic)
{
    QSet<ObjectId> ids;
    for (const Sheet &sheet : schematic.sheets) {
        for (const Label &label : sheet.labels)
            ids.insert(label.id);
        for (const PowerSymbol &power : sheet.powerSymbols)
            ids.insert(power.id);
    }
    return ids;
}

// True when a label or a power symbol of 'givers' is on 'net'.
bool hasNameGiver(const Net &net, const QSet<ObjectId> &givers)
{
    for (const QSet<ObjectId> &objects : net.objects) {
        for (const ObjectId &id : objects) {
            if (givers.contains(id))
                return true;
        }
    }
    return false;
}

void checkComponents(const Sheet &sheet, int index, const Library &library,
                     const QHash<QString, int> &designatorCount, const QSet<QString> &links,
                     QList<ErcProblem> &problems)
{
    for (const Component &component : sheet.components) {
        const Part *part = library.findPart(component.part);
        if (component.value.trimmed().isEmpty() && part != nullptr && !part->noValue) {
            problems.append(problem(ErcKind::EmptyValue, index, component.at, component.id,
                                    component.designator));
        }
    }
    for (const Component &component : sheet.components) {
        if (designatorCount.value(component.designator) > 1) {
            problems.append(problem(ErcKind::DuplicateDesignator, index, component.at,
                                    component.id, component.designator));
        }
    }
    for (const Component &component : sheet.components) {
        if (links.contains(component.designator)) {
            problems.append(problem(ErcKind::LinkDesignator, index, component.at, component.id,
                                    component.designator));
        }
    }
}

// Pins joined to nothing, and no-connect marks where they do not belong.
void checkPins(const Sheet &sheet, int index, const Library &library, const QList<Net> &all,
               const QHash<QString, int> &netOfPin, const QSet<ObjectId> &givers,
               QList<ErcProblem> &problems)
{
    QHash<ObjectId, QString> designators;
    for (const Component &component : sheet.components)
        designators.insert(component.id, component.designator);
    QList<ErcProblem> marks;
    for (const SheetPin &pin : sheetPins(sheet, library)) {
        // A power symbol's point has no number.
        if (pin.number.isEmpty())
            continue;
        const int net = netOfPin.value(pinKey(pin.owner, pin.number), -1);
        const bool joined = net >= 0
                            && (all.at(net).pins.size() > 1 || hasNameGiver(all.at(net), givers));
        const auto mark = std::find_if(sheet.noConnects.cbegin(), sheet.noConnects.cend(),
                                       [&pin](const NoConnect &m) { return m.at == pin.at; });
        const QString name = designators.value(pin.owner) + QLatin1Char('.') + pin.number;
        if (!joined && mark == sheet.noConnects.cend())
            problems.append(problem(ErcKind::UnconnectedPin, index, pin.at, pin.owner, name));
        else if (joined && mark != sheet.noConnects.cend())
            marks.append(problem(ErcKind::MisplacedNoConnect, index, mark->at, mark->id, name));
    }
    const QList<Point> partPins = partPinPoints(sheet, library);
    for (const NoConnect &mark : sheet.noConnects) {
        if (!partPins.contains(mark.at))
            marks.append(problem(ErcKind::MisplacedNoConnect, index, mark.at, mark.id, QString()));
    }
    problems.append(marks);
}

void checkLabelsAndWires(const Sheet &sheet, int index, const Library &library,
                         QList<ErcProblem> &problems)
{
    const QList<Point> pins = pinPoints(sheet, library);
    const auto onAWire = [&sheet](const Point &point, const ObjectId &except) {
        return std::any_of(sheet.wires.cbegin(), sheet.wires.cend(), [&](const Wire &wire) {
            return wire.id != except && isOnWire(wire, point);
        });
    };
    for (const Label &label : sheet.labels) {
        if (!pins.contains(label.at) && !onAWire(label.at, ObjectId()))
            problems.append(problem(ErcKind::LooseLabel, index, label.at, label.id, label.text));
    }
    for (const Wire &wire : sheet.wires) {
        if (wire.points.isEmpty())
            continue;
        QList<Point> ends = {wire.points.first()};
        if (wire.points.size() > 1)
            ends.append(wire.points.last());
        for (const Point &end : ends) {
            const bool held =
                pins.contains(end) || onAWire(end, wire.id) || isOnBus(sheet, end)
                || std::any_of(sheet.labels.cbegin(), sheet.labels.cend(),
                               [&end](const Label &label) { return label.at == end; });
            if (!held)
                problems.append(problem(ErcKind::LooseWireEnd, index, end, wire.id, QString()));
        }
    }
}

Coord cross(const Point &a, const Point &b)
{
    return a.x * b.y - a.y * b.x;
}

// The middle of the stretch the segments a1-a2 and b1-b2 share, when they
// lie on one line and share more than a point.
bool sharedMiddle(const Point &a1, const Point &a2, const Point &b1, const Point &b2, Point &middle)
{
    const Point along = a2 - a1;
    if (along == Point() || b1 == b2 || cross(b1 - a1, along) != 0 || cross(b2 - a1, along) != 0)
        return false;
    // Measured along the axis the segment runs most along.
    const bool byX = qAbs(along.x) >= qAbs(along.y);
    const auto axis = [byX](const Point &p) { return byX ? p.x : p.y; };
    const Coord low = qMax(qMin(axis(a1), axis(a2)), qMin(axis(b1), axis(b2)));
    const Coord high = qMin(qMax(axis(a1), axis(a2)), qMax(axis(b1), axis(b2)));
    if (high <= low)
        return false;
    const Coord at = low + (high - low) / 2;
    const Coord span = axis(a2) - axis(a1);
    const Coord offset = at - axis(a1);
    middle = byX ? Point{at, a1.y + along.y * offset / span}
                 : Point{a1.x + along.x * offset / span, at};
    return true;
}

void checkOverlaps(const Sheet &sheet, int index, QList<ErcProblem> &problems)
{
    for (int i = 0; i < sheet.wires.size(); ++i) {
        const QList<Point> &a = sheet.wires.at(i).points;
        for (int j = i; j < sheet.wires.size(); ++j) {
            const Wire &later = sheet.wires.at(j);
            const QList<Point> &b = later.points;
            for (int sa = 0; sa + 1 < a.size(); ++sa) {
                // On the same wire, each two segments once.
                for (int sb = i == j ? sa + 1 : 0; sb + 1 < b.size(); ++sb) {
                    Point middle;
                    if (sharedMiddle(a.at(sa), a.at(sa + 1), b.at(sb), b.at(sb + 1), middle)) {
                        problems.append(problem(ErcKind::OverlappingWires, index, middle,
                                                later.id, QString()));
                    }
                }
            }
        }
    }
}

// True when 'point' lies on the segment a-b, ends included.
bool onSegment(const Point &a, const Point &b, const Point &point)
{
    return cross(point - a, b - a) == 0 && point.x >= qMin(a.x, b.x) && point.x <= qMax(a.x, b.x)
           && point.y >= qMin(a.y, b.y) && point.y <= qMax(a.y, b.y);
}

// The wires as a graph: its nodes are the corners and ends of the wires,
// one lying on another's segment cutting it there; a segment cut this way
// gives an edge for each piece. A crossing makes no node. An edge between
// nodes already joined closes a loop; one met before (wires lying on each
// other) is left out.
void checkLoops(const Sheet &sheet, int index, QList<ErcProblem> &problems)
{
    QList<Point> nodes;
    QHash<QPair<Coord, Coord>, int> nodeAt;
    for (const Wire &wire : sheet.wires) {
        for (const Point &point : wire.points) {
            const QPair<Coord, Coord> key(point.x, point.y);
            if (!nodeAt.contains(key)) {
                nodeAt.insert(key, int(nodes.size()));
                nodes.append(point);
            }
        }
    }
    Joins joins;
    for (int i = 0; i < nodes.size(); ++i)
        joins.add();
    QSet<QPair<int, int>> edges;
    for (const Wire &wire : sheet.wires) {
        for (int s = 0; s + 1 < wire.points.size(); ++s) {
            const Point a = wire.points.at(s);
            const Point b = wire.points.at(s + 1);
            if (a == b)
                continue;
            // The nodes on the segment, from 'a' on.
            QList<int> on;
            for (int n = 0; n < nodes.size(); ++n) {
                if (onSegment(a, b, nodes.at(n)))
                    on.append(n);
            }
            const auto distance = [&](int n) {
                return qAbs(nodes.at(n).x - a.x) + qAbs(nodes.at(n).y - a.y);
            };
            std::sort(on.begin(), on.end(), [&](int l, int r) { return distance(l) < distance(r); });
            for (int k = 0; k + 1 < on.size(); ++k) {
                const QPair<int, int> edge(qMin(on.at(k), on.at(k + 1)),
                                           qMax(on.at(k), on.at(k + 1)));
                if (edges.contains(edge))
                    continue;
                edges.insert(edge);
                if (joins.root(edge.first) == joins.root(edge.second)) {
                    const Point from = nodes.at(edge.first);
                    const Point to = nodes.at(edge.second);
                    const Point middle{from.x + (to.x - from.x) / 2, from.y + (to.y - from.y) / 2};
                    problems.append(problem(ErcKind::WireLoop, index, middle, wire.id, QString()));
                } else {
                    joins.join(edge.first, edge.second);
                }
            }
        }
    }
}

// Components with two pins, both on one net.
void checkShorts(const Sheet &sheet, int index, const Library &library,
                 const QHash<QString, int> &netOfPin, QList<ErcProblem> &problems)
{
    for (const Component &component : sheet.components) {
        const Symbol *symbol = library.findSymbolOfPart(component.part);
        if (symbol == nullptr || symbol->pins.size() != 2)
            continue;
        const int first = netOfPin.value(pinKey(component.id, symbol->pins.at(0).number), -1);
        const int second = netOfPin.value(pinKey(component.id, symbol->pins.at(1).number), -1);
        if (first >= 0 && first == second) {
            problems.append(problem(ErcKind::ShortedComponent, index, component.at, component.id,
                                    component.designator));
        }
    }
}

// Nets with two names, and names given that the table gives another net.
void checkNames(const Schematic &schematic, const QList<Net> &all, QList<ErcProblem> &problems)
{
    QHash<ObjectId, int> netOfObject;
    QHash<QString, int> netOfTableName;
    for (int n = 0; n < all.size(); ++n) {
        for (const QSet<ObjectId> &objects : all.at(n).objects) {
            for (const ObjectId &id : objects)
                netOfObject.insert(id, n);
        }
        if (!all.at(n).tableName.isEmpty())
            netOfTableName.insert(all.at(n).tableName, n);
    }

    // The first label or power symbol, sheet by sheet, giving net 'net' one
    // of 'names'; 'sheet' is -1 when there is none.
    struct Giver
    {
        int sheet = -1;
        Point at;
        ObjectId id;
    };
    const auto giverOf = [&](int net, const QStringList &names) {
        for (int s = 0; s < schematic.sheets.size(); ++s) {
            const Sheet &sheet = schematic.sheets.at(s);
            for (const PowerSymbol &power : sheet.powerSymbols) {
                if (names.contains(power.name) && netOfObject.value(power.id, -1) == net)
                    return Giver{s, power.at, power.id};
            }
            for (const Label &label : sheet.labels) {
                if (names.contains(label.text) && netOfObject.value(label.id, -1) == net)
                    return Giver{s, label.at, label.id};
            }
        }
        return Giver();
    };

    for (int n = 0; n < all.size(); ++n) {
        const Net &net = all.at(n);
        if (net.otherNames.isEmpty())
            continue;
        const Giver giver = giverOf(n, net.otherNames);
        problems.append(problem(ErcKind::TwoNames, giver.sheet, giver.at, giver.id,
                                (QStringList{net.name} + net.otherNames).join(QStringLiteral(", "))));
    }

    for (int s = 0; s < schematic.sheets.size(); ++s) {
        const Sheet &sheet = schematic.sheets.at(s);
        const auto check = [&](const ObjectId &id, const Point &at, const QString &name) {
            const int owner = netOfTableName.value(name, -1);
            if (owner >= 0 && netOfObject.value(id, -1) != owner)
                problems.append(problem(ErcKind::NameOfAnotherNet, s, at, id, name));
        };
        for (const PowerSymbol &power : sheet.powerSymbols)
            check(power.id, power.at, power.name);
        for (const Label &label : sheet.labels)
            check(label.id, label.at, label.text);
    }
}

void checkTableNames(const Project &project, const QList<Net> &all, QList<ErcProblem> &problems)
{
    for (const NetByPins &byPins : project.netsByPins) {
        // A name with no class only names a net for the board: nothing is lost.
        if (!project.classOfNet.contains(byPins.name))
            continue;
        const bool held = std::any_of(all.cbegin(), all.cend(), [&byPins](const Net &net) {
            return net.tableName == byPins.name || net.mergedTableNames.contains(byPins.name);
        });
        if (!held)
            problems.append(problem(ErcKind::LostTableName, -1, Point(), ObjectId(), byPins.name));
    }
}

} // namespace

bool operator==(const ErcProblem &a, const ErcProblem &b)
{
    return a.kind == b.kind && a.sheet == b.sheet && a.at == b.at && a.object == b.object
           && a.subject == b.subject;
}

QList<ErcProblem> checkSchematic(const Schematic &schematic, const Library &library,
                                 const Project &project, const QList<Net> &all,
                                 const QStringList &links)
{
    const QSet<QString> linkSet(links.cbegin(), links.cend());
    QHash<QString, int> designatorCount;
    for (const Sheet &sheet : schematic.sheets) {
        for (const Component &component : sheet.components) {
            if (!component.designator.isEmpty())
                ++designatorCount[component.designator];
        }
    }
    QHash<QString, int> netOfPin;
    for (int n = 0; n < all.size(); ++n) {
        for (const NetPin &pin : all.at(n).pins)
            netOfPin.insert(pinKey(pin.component, pin.number), n);
    }
    const QSet<ObjectId> givers = nameGivers(schematic);

    QList<ErcProblem> problems;
    for (int s = 0; s < schematic.sheets.size(); ++s) {
        const Sheet &sheet = schematic.sheets.at(s);
        checkComponents(sheet, s, library, designatorCount, linkSet, problems);
        checkPins(sheet, s, library, all, netOfPin, givers, problems);
        checkLabelsAndWires(sheet, s, library, problems);
        checkOverlaps(sheet, s, problems);
        checkLoops(sheet, s, problems);
        checkShorts(sheet, s, library, netOfPin, problems);
    }
    checkNames(schematic, all, problems);
    checkTableNames(project, all, problems);

    // Sheet by sheet, the project's own last; kind by kind on each.
    std::stable_sort(problems.begin(), problems.end(),
                     [](const ErcProblem &a, const ErcProblem &b) {
                         const int sheetA = a.sheet < 0 ? INT_MAX : a.sheet;
                         const int sheetB = b.sheet < 0 ? INT_MAX : b.sheet;
                         if (sheetA != sheetB)
                             return sheetA < sheetB;
                         return int(a.kind) < int(b.kind);
                     });
    return problems;
}

} // namespace ppcb
