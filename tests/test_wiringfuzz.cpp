#include "sheetedit.h"
#include "wiring.h"

#include <QRandomGenerator>
#include <QtTest>

#include <cstdlib>

using namespace ppcb;

// Random sheets of two-pin parts and wires, each moved, turned and dragged
// by a segment at random under both ways of laying wires: every wire keeps
// the shape rules, and pins joined before stay joined.

namespace {

const Coord G = PIN_GRID;

// A part "two" with pins two pin steps left and right of its origin.
Library twoPinLibrary()
{
    Symbol symbol;
    Pin left;
    left.number = QStringLiteral("1");
    left.at = Point{-2 * G, 0};
    left.side = PinSide::Left;
    Pin right = left;
    right.number = QStringLiteral("2");
    right.side = PinSide::Right;
    right.at = Point{2 * G, 0};
    symbol.pins = {left, right};
    Library library;
    library.symbols.insert(QStringLiteral("two"), symbol);
    Part part;
    part.symbol = QStringLiteral("two");
    library.parts.insert(QStringLiteral("two"), part);
    return library;
}

qint64 cross(const Point &a, const Point &b)
{
    return qint64(a.x) * b.y - qint64(a.y) * b.x;
}

// True when the segments a-b and c-d lie on one line and share a length.
bool overlap(const Point &a, const Point &b, const Point &c, const Point &d)
{
    const Point u = b - a;
    if (u == Point() || cross(u, d - c) != 0 || cross(u, c - a) != 0)
        return false;
    const auto along = [&](const Point &p) {
        return qint64(p.x - a.x) * u.x + qint64(p.y - a.y) * u.y;
    };
    const qint64 length = along(b);
    return qMin(length, qMax(along(c), along(d))) - qMax<qint64>(0, qMin(along(c), along(d))) > 0;
}

// The wire from 'from' along 'route', or nothing when it turns straight
// back or goes on straight at a corner.
QList<Point> plainWire(const Point &from, const QList<Point> &route)
{
    QList<Point> result = {from};
    for (const Point &point : route) {
        if (result.last() == point)
            continue;
        if (result.size() >= 2
            && cross(result.last() - result.at(result.size() - 2), point - result.last()) == 0)
            return {};
        result.append(point);
    }
    return result;
}

// A sheet worth checking: no two segments along each other, no closed
// wire, no wire along the line of a pin.
bool isPlain(const Sheet &sheet, const Library &library)
{
    QList<QPair<Point, Point>> segments;
    for (const Wire &wire : sheet.wires) {
        if (wire.points.size() < 2 || wire.points.first() == wire.points.last())
            return false;
        for (int i = 1; i < wire.points.size(); ++i)
            segments.append({wire.points.at(i - 1), wire.points.at(i)});
    }
    for (const SheetPin &pin : sheetPins(sheet, library)) {
        if (pin.at != pin.bodyEnd)
            segments.append({pin.at, pin.bodyEnd});
    }
    for (int i = 0; i < segments.size(); ++i) {
        for (int j = i + 1; j < segments.size(); ++j) {
            if (overlap(segments.at(i).first, segments.at(i).second, segments.at(j).first,
                        segments.at(j).second))
                return false;
        }
    }
    return true;
}

// The net of each pin, by its owner and number, joined as nets() joins
// them: pins on one point, a pin anywhere on a wire, a wire end on a wire.
QHash<QPair<quint64, QString>, int> pinNets(const Sheet &sheet, const Library &library)
{
    const QList<SheetPin> pins = sheetPins(sheet, library);
    QList<int> parent(pins.size() + sheet.wires.size());
    std::iota(parent.begin(), parent.end(), 0);
    const auto root = [&parent](int i) {
        while (parent.at(i) != i)
            i = parent.at(i);
        return i;
    };
    const auto join = [&](int a, int b) { parent[root(a)] = root(b); };
    const int wireBase = int(pins.size());
    for (int i = 0; i < pins.size(); ++i) {
        for (int j = i + 1; j < pins.size(); ++j) {
            if (pins.at(i).at == pins.at(j).at)
                join(i, j);
        }
    }
    for (int w = 0; w < sheet.wires.size(); ++w) {
        const Wire &wire = sheet.wires.at(w);
        for (int i = 0; i < pins.size(); ++i) {
            if (isOnWire(wire, pins.at(i).at))
                join(wireBase + w, i);
        }
        for (int v = 0; v < sheet.wires.size(); ++v) {
            const Wire &other = sheet.wires.at(v);
            if (v != w && !wire.points.isEmpty()
                && (isOnWire(other, wire.points.first()) || isOnWire(other, wire.points.last())))
                join(wireBase + w, wireBase + v);
        }
    }
    QHash<QPair<quint64, QString>, int> nets;
    for (int i = 0; i < pins.size(); ++i)
        nets.insert(qMakePair(pins.at(i).owner.value, pins.at(i).number), root(i));
    return nets;
}

// True when every wire has two points or more, no step of nothing, and
// every segment level, upright or at 45 degrees.
bool keepsTheShapeRules(const Sheet &sheet)
{
    for (const Wire &wire : sheet.wires) {
        if (wire.points.size() < 2)
            return false;
        for (int i = 1; i < wire.points.size(); ++i) {
            const Point step = wire.points.at(i) - wire.points.at(i - 1);
            if (step == Point()
                || (step.x != 0 && step.y != 0 && std::llabs(step.x) != std::llabs(step.y)))
                return false;
        }
    }
    return true;
}

// The pairs of pins joined in 'before' and not in 'after'.
int lostJoins(const QHash<QPair<quint64, QString>, int> &before,
              const QHash<QPair<quint64, QString>, int> &after)
{
    int lost = 0;
    const auto keys = before.keys();
    for (int i = 0; i < keys.size(); ++i) {
        for (int j = i + 1; j < keys.size(); ++j) {
            if (before.value(keys.at(i)) == before.value(keys.at(j))
                && after.value(keys.at(i)) != after.value(keys.at(j)))
                ++lost;
        }
    }
    return lost;
}

} // namespace

class TestWiringFuzz : public QObject
{
    Q_OBJECT

private slots:
    void randomEditsKeepJoinsAndShapes()
    {
        const Library library = twoPinLibrary();
        QRandomGenerator random(1);
        const auto between = [&random](int low, int high) {
            return int(random.bounded(low, high + 1));
        };
        int sheets = 0;
        int badShapes = 0;
        QHash<WireRouting, int> lost;
        for (int run = 0; run < 6000; ++run) {
            Sheet sheet;
            const int parts = between(2, 5);
            while (sheet.components.size() < parts) {
                Component component;
                component.id = newObjectId();
                component.part = QStringLiteral("two");
                component.at = Point{between(0, 5) * 6 * G, between(0, 5) * 6 * G};
                component.rotation = between(0, 1) * 90;
                const bool taken = std::any_of(
                    sheet.components.cbegin(), sheet.components.cend(),
                    [&component](const Component &other) { return other.at == component.at; });
                if (!taken)
                    sheet.components.append(component);
            }
            const QList<Point> pins = pinPoints(sheet, library);
            const int wires = between(1, 5);
            for (int i = 0; i < wires; ++i) {
                const Point from = pins.at(between(0, int(pins.size()) - 1));
                Point to = pins.at(between(0, int(pins.size()) - 1));
                if (between(0, 2) == 0 && !sheet.wires.isEmpty())
                    to = sheet.wires.at(between(0, int(sheet.wires.size()) - 1)).points.first();
                if (between(0, 3) == 0)
                    to = Point{between(-2, 18) * G, between(-2, 18) * G};
                Wire wire;
                wire.id = newObjectId();
                wire.points = plainWire(
                    from, wireRoute(from, to, between(0, 1) == 1, between(0, 4) == 0));
                if (wire.points.size() >= 2)
                    sheet.wires.append(wire);
            }
            sheet = withWiresMerged(sheet);
            if (sheet.wires.isEmpty() || !keepsTheShapeRules(sheet) || !isPlain(sheet, library))
                continue;
            ++sheets;
            const auto netsBefore = pinNets(sheet, library);
            const Component &part = sheet.components.at(between(0, int(parts) - 1));
            const QSet<ObjectId> ids = {part.id};
            const Point delta{between(-4, 4) * G, between(-4, 4) * G};
            const Wire &dragged = sheet.wires.at(between(0, int(sheet.wires.size()) - 1));
            const int segment = between(0, int(dragged.points.size()) - 2);
            const Point segmentDelta{between(-4, 4) * G, between(-4, 4) * G};
            for (const WireRouting routing : {WireRouting::Hand, WireRouting::Grid}) {
                const QList<Sheet> edited = {
                    withWiresFollowing(sheet, movedObjects(sheet, ids, delta), ids, library,
                                       routing),
                    withWiresFollowing(sheet, rotatedObjects(sheet, ids, part.at), ids, library,
                                       routing),
                    withSegmentMoved(sheet, library, dragged.id, segment, segmentDelta, routing)};
                for (const Sheet &after : edited) {
                    if (!keepsTheShapeRules(after))
                        ++badShapes;
                    lost[routing] += lostJoins(netsBefore, pinNets(after, library));
                }
            }
        }
        QVERIFY2(sheets > 1000, qPrintable(QString::number(sheets)));
        QCOMPARE(badShapes, 0);
        QCOMPARE(lost.value(WireRouting::Hand), 0);
        // Remove once tofix2 finding 9 is fixed: it then passes unexpectedly.
        QEXPECT_FAIL("", "tofix2 finding 9: wires laid on the grid lose a few joins", Continue);
        QCOMPARE(lost.value(WireRouting::Grid), 0);
    }
};

QTEST_APPLESS_MAIN(TestWiringFuzz)
#include "test_wiringfuzz.moc"
