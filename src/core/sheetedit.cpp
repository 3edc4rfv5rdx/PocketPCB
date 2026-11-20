#include "sheetedit.h"

#include "bom.h"
#include "wiring.h"

#include <QLineF>

#include <algorithm>
#include <functional>

namespace ppcb {

namespace {

// What a bus is named with a number after it (see nextBusName()).
const char BUS_NAME_PREFIX[] = "BUS";

const int QUARTER_TURN = 90;
const int HALF_TURN_DEGREES = 180;
const int FULL_TURN_DEGREES = 360;

// An angle in whole degrees brought into 0..359.
int normalAngle(int degrees)
{
    return ((degrees % FULL_TURN_DEGREES) + FULL_TURN_DEGREES) % FULL_TURN_DEGREES;
}

// 'sheet' with 'edit' done to each component of 'ids'.
Sheet withComponentsChanged(const Sheet &sheet, const QSet<ObjectId> &ids,
                            const std::function<void(Component &)> &edit)
{
    Sheet result = sheet;
    for (Component &component : result.components) {
        if (ids.contains(component.id))
            edit(component);
    }
    return result;
}

// What a turn or a mirror does to each kind of thing on a sheet.
struct Transform
{
    std::function<Point(const Point &)> point;
    // To an offset that moves with its object (a component's text places).
    std::function<Point(const Point &)> offset;
    // To a component: its rotation and its mirroring.
    std::function<void(Component &)> component;
    // To the rotation of a label and of a power symbol.
    std::function<int(int)> labelRotation;
    std::function<int(int)> powerRotation;
    // To a graphic, after its points: its arc angles and text alignment.
    std::function<void(Graphic &)> graphic;
};

Sheet transformed(const Sheet &sheet, const QSet<ObjectId> &ids, const Transform &transform)
{
    Sheet result = sheet;
    for (Component &component : result.components) {
        if (!ids.contains(component.id))
            continue;
        component.at = transform.point(component.at);
        component.refAt = transform.offset(component.refAt);
        component.valueAt = transform.offset(component.valueAt);
        transform.component(component);
    }
    for (Wire &wire : result.wires) {
        if (ids.contains(wire.id)) {
            for (Point &point : wire.points)
                point = transform.point(point);
        }
    }
    for (Bus &bus : result.buses) {
        if (ids.contains(bus.id)) {
            for (Point &point : bus.points)
                point = transform.point(point);
            for (Point &place : bus.namesAt)
                place = transform.offset(place);
        }
    }
    for (Label &label : result.labels) {
        if (ids.contains(label.id)) {
            label.at = transform.point(label.at);
            label.textAt = transform.offset(label.textAt);
            label.rotation = transform.labelRotation(label.rotation);
        }
    }
    for (PowerSymbol &power : result.powerSymbols) {
        if (ids.contains(power.id)) {
            power.at = transform.point(power.at);
            power.rotation = transform.powerRotation(power.rotation);
        }
    }
    for (NoConnect &mark : result.noConnects) {
        if (ids.contains(mark.id))
            mark.at = transform.point(mark.at);
    }
    for (SheetGraphic &item : result.graphics) {
        if (!ids.contains(item.id))
            continue;
        Graphic &graphic = item.graphic;
        graphic.a = transform.point(graphic.a);
        graphic.b = transform.point(graphic.b);
        for (Point &point : graphic.points)
            point = transform.point(point);
        transform.graphic(graphic);
    }
    return result;
}

} // namespace

double distanceToSegment(const QPointF &point, const QPointF &a, const QPointF &b)
{
    const QPointF ab = b - a;
    const double lengthSquared = QPointF::dotProduct(ab, ab);
    if (lengthSquared == 0)
        return QLineF(point, a).length();
    const double t = std::clamp(QPointF::dotProduct(point - a, ab) / lengthSquared, 0.0, 1.0);
    return QLineF(point, a + t * ab).length();
}

bool isNearPolyline(const QPolygonF &polyline, const QPointF &point, double tolerance)
{
    for (int i = 1; i < polyline.size(); ++i) {
        if (distanceToSegment(point, polyline.at(i - 1), polyline.at(i)) <= tolerance)
            return true;
    }
    return polyline.size() == 1 && QLineF(point, polyline.first()).length() <= tolerance;
}

ObjectId objectAt(const QList<SheetObjectShape> &shapes, const QPointF &point, double tolerance)
{
    for (auto it = shapes.crbegin(); it != shapes.crend(); ++it) {
        if (!it->polyline.isEmpty() && isNearPolyline(it->polyline, point, tolerance))
            return it->id;
    }
    const auto grown = [tolerance](const QRectF &rect) {
        return rect.adjusted(-tolerance, -tolerance, tolerance, tolerance);
    };
    // The drawing of a part before the texts of a part beside it, which may
    // reach over it.
    for (auto it = shapes.crbegin(); it != shapes.crend(); ++it) {
        if (it->polyline.isEmpty() && !it->body.isNull() && grown(it->body).contains(point))
            return it->id;
    }
    for (auto it = shapes.crbegin(); it != shapes.crend(); ++it) {
        if (it->polyline.isEmpty() && grown(it->rect).contains(point))
            return it->id;
    }
    return ObjectId();
}

QSet<ObjectId> objectsInside(const QList<SheetObjectShape> &shapes, const QRectF &rect)
{
    QSet<ObjectId> ids;
    const QRectF area = rect.normalized();
    for (const SheetObjectShape &shape : shapes) {
        // contains() refuses a rectangle of no width, such as a straight wire.
        const QRectF r = shape.body.isNull() ? shape.rect : shape.body;
        if (area.left() <= r.left() && r.right() <= area.right() && area.top() <= r.top()
            && r.bottom() <= area.bottom()) {
            ids.insert(shape.id);
        }
    }
    return ids;
}

QRectF selectionBounds(const QList<SheetObjectShape> &shapes, const QSet<ObjectId> &ids)
{
    QRectF bounds;
    bool empty = true;
    for (const SheetObjectShape &shape : shapes) {
        if (!ids.contains(shape.id))
            continue;
        const QRectF r = shape.rect;
        if (empty) {
            bounds = r;
            empty = false;
        } else {
            bounds.setCoords(std::min(bounds.left(), r.left()), std::min(bounds.top(), r.top()),
                             std::max(bounds.right(), r.right()),
                             std::max(bounds.bottom(), r.bottom()));
        }
    }
    return bounds;
}

TextHit textAt(const Sheet &sheet, const Library &library, const PowerShown &power,
               const QPointF &point)
{
    for (auto it = sheet.components.crbegin(); it != sheet.components.crend(); ++it) {
        for (const ComponentText text : {ComponentText::Designator, ComponentText::Value}) {
            if (componentTextShown(*it, text, library, power).isEmpty())
                continue;
            if (componentTextRect(*it, text, library, power).contains(point))
                return TextHit{it->id, text};
        }
    }
    for (auto it = sheet.labels.crbegin(); it != sheet.labels.crend(); ++it) {
        if (labelTextRect(*it).contains(point))
            return TextHit{it->id, ComponentText::Designator};
    }
    for (auto it = sheet.buses.crbegin(); it != sheet.buses.crend(); ++it) {
        for (int i = int(it->namesAt.size()) - 1; i >= 0; --i) {
            if (busNameRect(*it, i).contains(point))
                return TextHit{it->id, ComponentText::Designator, i};
        }
    }
    return TextHit();
}

namespace {

// The item of 'items' with the id 'id'; null when there is none.
template <typename Item>
Item *ownerOf(QList<Item> &items, const ObjectId &id)
{
    for (Item &item : items) {
        if (item.id == id)
            return &item;
    }
    return nullptr;
}

template <typename Item>
const Item *ownerOf(const QList<Item> &items, const ObjectId &id)
{
    for (const Item &item : items) {
        if (item.id == id)
            return &item;
    }
    return nullptr;
}

} // namespace

bool hasText(const Sheet &sheet, const TextHit &hit)
{
    if (hit.owner.isNull())
        return false;
    if (const Bus *bus = ownerOf(sheet.buses, hit.owner))
        return hit.index >= 0 && hit.index < bus->namesAt.size();
    return ownerOf(sheet.components, hit.owner) != nullptr
           || ownerOf(sheet.labels, hit.owner) != nullptr;
}

Point textPoint(const Sheet &sheet, const TextHit &hit)
{
    if (const Component *component = ownerOf(sheet.components, hit.owner))
        return componentTextPoint(*component, hit.text);
    if (const Label *label = ownerOf(sheet.labels, hit.owner))
        return labelTextPoint(*label);
    if (const Bus *bus = ownerOf(sheet.buses, hit.owner))
        return busNamePoint(*bus, hit.index);
    return Point();
}

Point textOwnerPoint(const Sheet &sheet, const TextHit &hit)
{
    if (const Component *component = ownerOf(sheet.components, hit.owner))
        return component->at;
    if (const Label *label = ownerOf(sheet.labels, hit.owner))
        return label->at;
    if (const Bus *bus = ownerOf(sheet.buses, hit.owner))
        return bus->points.value(0);
    return Point();
}

QRectF textRect(const Sheet &sheet, const Library &library, const PowerShown &power,
                const TextHit &hit)
{
    if (const Component *component = ownerOf(sheet.components, hit.owner))
        return componentTextRect(*component, hit.text, library, power);
    if (const Label *label = ownerOf(sheet.labels, hit.owner))
        return labelTextRect(*label);
    if (const Bus *bus = ownerOf(sheet.buses, hit.owner))
        return busNameRect(*bus, hit.index);
    return QRectF();
}

Sheet withTextMoved(const Sheet &sheet, const TextHit &hit, const Point &delta)
{
    Sheet result = sheet;
    if (Component *component = ownerOf(result.components, hit.owner)) {
        Point &offset =
            hit.text == ComponentText::Designator ? component->refAt : component->valueAt;
        offset = offset + delta;
    }
    if (Label *label = ownerOf(result.labels, hit.owner))
        label->textAt = label->textAt + delta;
    if (Bus *bus = ownerOf(result.buses, hit.owner)) {
        if (hit.index >= 0 && hit.index < bus->namesAt.size())
            bus->namesAt[hit.index] = bus->namesAt.at(hit.index) + delta;
    }
    return result;
}

Sheet withBusNameAdded(const Sheet &sheet, const ObjectId &bus, const Point &at)
{
    Sheet result = sheet;
    if (Bus *found = ownerOf(result.buses, bus))
        found->namesAt.append(at - found->points.value(0));
    return result;
}

bool isBusNameRemovable(const Sheet &sheet, const TextHit &hit)
{
    if (!hasText(sheet, hit) || ownerOf(sheet.buses, hit.owner) == nullptr)
        return false;
    int places = 0;
    const QSet<ObjectId> group = joinedBuses(sheet, hit.owner);
    for (const Bus &bus : sheet.buses) {
        if (group.contains(bus.id))
            places += int(bus.namesAt.size());
    }
    return places > 1;
}

Sheet withoutBusName(const Sheet &sheet, const TextHit &hit)
{
    if (!isBusNameRemovable(sheet, hit))
        return sheet;
    Sheet result = sheet;
    ownerOf(result.buses, hit.owner)->namesAt.removeAt(hit.index);
    return result;
}

Sheet withComponentValue(const Sheet &sheet, const QSet<ObjectId> &ids, const QString &value)
{
    const QString trimmed = value.trimmed();
    return withComponentsChanged(sheet, ids, [&trimmed](Component &c) { c.value = trimmed; });
}

Sheet withPowerName(const Sheet &sheet, const QSet<ObjectId> &ids, const QString &name)
{
    const QString trimmed = name.trimmed();
    if (trimmed.isEmpty())
        return sheet;
    Sheet result = sheet;
    for (PowerSymbol &power : result.powerSymbols) {
        if (ids.contains(power.id))
            power.name = trimmed;
    }
    return result;
}

Sheet withLabelText(const Sheet &sheet, const QSet<ObjectId> &ids, const QString &text)
{
    const QString trimmed = text.trimmed();
    if (trimmed.isEmpty())
        return sheet;
    Sheet result = sheet;
    for (Label &label : result.labels) {
        if (ids.contains(label.id))
            label.text = trimmed;
    }
    return result;
}

Sheet withBusName(const Sheet &sheet, const QSet<ObjectId> &ids, const QString &name)
{
    const QString trimmed = name.trimmed();
    if (trimmed.isEmpty())
        return sheet;
    // A bus and its branches have one name.
    QSet<ObjectId> named;
    for (const ObjectId &id : ids)
        named.unite(joinedBuses(sheet, id));
    Sheet result = sheet;
    for (Bus &bus : result.buses) {
        if (named.contains(bus.id))
            bus.name = trimmed;
    }
    return result;
}

QString nextBusName(const Schematic &schematic)
{
    const QString prefix = QLatin1String(BUS_NAME_PREFIX);
    QSet<QString> taken;
    for (const Sheet &sheet : schematic.sheets) {
        for (const Bus &bus : sheet.buses)
            taken.insert(bus.name);
    }
    int number = 1;
    while (taken.contains(prefix + QString::number(number)))
        ++number;
    return prefix + QString::number(number);
}

QString nextLabelText(const QString &text)
{
    int digits = 0;
    while (digits < text.size() && text.at(text.size() - 1 - digits).isDigit())
        ++digits;
    if (digits == 0)
        return text;
    const QString head = text.left(text.size() - digits);
    bool ok = false;
    const qulonglong number = text.right(digits).toULongLong(&ok);
    // Too long a number to count on: left as it is.
    if (!ok)
        return text;
    return head + QStringLiteral("%1").arg(number + 1, digits, 10, QLatin1Char('0'));
}

Sheet withComponentDesignator(const Sheet &sheet, const QSet<ObjectId> &ids,
                              const QString &designator)
{
    const QString trimmed = designator.trimmed();
    if (trimmed.isEmpty())
        return sheet;
    return withComponentsChanged(sheet, ids,
                                 [&trimmed](Component &c) { c.designator = trimmed; });
}

Sheet withComponentFootprint(const Sheet &sheet, const QSet<ObjectId> &ids,
                             const QString &footprint)
{
    return withComponentsChanged(sheet, ids,
                                 [&footprint](Component &c) { c.footprint = footprint; });
}

Sheet withComponentOffBoard(const Sheet &sheet, const QSet<ObjectId> &ids, bool offBoard)
{
    return withComponentsChanged(sheet, ids, [offBoard](Component &c) { c.offBoard = offBoard; });
}

Sheet withComponentComment(const Sheet &sheet, const QSet<ObjectId> &ids, const QString &comment)
{
    const QString trimmed = comment.trimmed();
    return withComponentsChanged(sheet, ids, [&trimmed](Component &c) { c.comment = trimmed; });
}

Sheet withComponentPower(const Sheet &sheet, const QSet<ObjectId> &ids, int powerMw,
                         const Library &library)
{
    return withComponentsChanged(sheet, ids, [powerMw, &library](Component &c) {
        const Part *part = library.findPart(c.part);
        if (part != nullptr && part->powerMw > 0)
            c.powerMw = qBound(0, powerMw, MAX_POWER_MW);
    });
}

bool isDesignatorTaken(const Schematic &schematic, const QString &designator,
                       const ObjectId &except)
{
    for (const Sheet &sheet : schematic.sheets) {
        for (const Component &component : sheet.components) {
            if (component.id != except && component.designator == designator)
                return true;
        }
    }
    return false;
}

Sheet movedObjects(const Sheet &sheet, const QSet<ObjectId> &ids, const Point &delta)
{
    Transform transform;
    transform.point = [&delta](const Point &p) { return p + delta; };
    transform.offset = [](const Point &p) { return p; };
    transform.component = [](Component &) {};
    transform.labelRotation = [](int rotation) { return rotation; };
    transform.powerRotation = [](int rotation) { return rotation; };
    transform.graphic = [](Graphic &) {};
    return transformed(sheet, ids, transform);
}

Sheet rotatedObjects(const Sheet &sheet, const QSet<ObjectId> &ids, const Point &center)
{
    // A quarter turn clockwise on screen, where Y grows downwards: what was
    // to the right of the centre goes below it.
    const auto turnOffset = [](const Point &p) { return Point{-p.y, p.x}; };
    Transform transform;
    transform.point = [&center, turnOffset](const Point &p) {
        return center + turnOffset(p - center);
    };
    transform.offset = turnOffset;
    transform.component = [](Component &c) { c.rotation = normalAngle(c.rotation + QUARTER_TURN); };
    transform.labelRotation = [](int r) { return normalAngle(r + QUARTER_TURN); };
    transform.powerRotation = [](int r) { return normalAngle(r + QUARTER_TURN); };
    transform.graphic = [](Graphic &g) {
        if (g.kind == GraphicKind::Arc)
            g.startAngle = normalAngle(g.startAngle + QUARTER_TURN);
    };
    return transformed(sheet, ids, transform);
}

Sheet mirroredObjects(const Sheet &sheet, const QSet<ObjectId> &ids, const Point &center)
{
    const auto flipOffset = [](const Point &p) { return Point{-p.x, p.y}; };
    Transform transform;
    transform.point = [&center, flipOffset](const Point &p) {
        return center + flipOffset(p - center);
    };
    transform.offset = flipOffset;
    // A mirror before a turn equals the opposite turn before the mirror.
    transform.component = [](Component &c) {
        c.mirrored = !c.mirrored;
        c.rotation = normalAngle(-c.rotation);
    };
    // A label's text runs along its direction, which flips about the vertical.
    transform.labelRotation = [](int r) { return normalAngle(HALF_TURN_DEGREES - r); };
    // A power symbol's stem points up at 0: up stays up, right becomes left.
    transform.powerRotation = [](int r) { return normalAngle(-r); };
    transform.graphic = [](Graphic &g) {
        if (g.kind == GraphicKind::Arc)
            g.startAngle = normalAngle(HALF_TURN_DEGREES - g.startAngle - g.spanAngle);
        if (g.kind == GraphicKind::Text) {
            if (g.align == TextAlign::Left)
                g.align = TextAlign::Right;
            else if (g.align == TextAlign::Right)
                g.align = TextAlign::Left;
        }
    };
    return transformed(sheet, ids, transform);
}

PowerShown powerShown(const Project &project, const Schematic &schematic)
{
    PowerShown power;
    power.marks = project.designatorStyle == DesignatorStyle::Gost;
    power.commonMw = commonPowerMw(schematic);
    return power;
}

QString designatorPrefix(const Part &part, DesignatorStyle style)
{
    return style == DesignatorStyle::Gost ? part.prefixGost : part.prefix;
}

Schematic withDesignatorStyle(const Schematic &schematic, const Library &library,
                              DesignatorStyle from, DesignatorStyle to)
{
    // A component whose designator takes the new prefix.
    struct Renamed
    {
        int sheet;
        int index;
        QString old;
        QString prefix;
        QString rest;
    };
    Schematic result = schematic;
    QSet<QString> taken;
    QList<Renamed> renamed;
    for (int s = 0; s < schematic.sheets.size(); ++s) {
        const QList<Component> &components = schematic.sheets.at(s).components;
        for (int i = 0; i < components.size(); ++i) {
            const QString &designator = components.at(i).designator;
            const Part *part = library.findPart(components.at(i).part);
            const QString oldPrefix = part != nullptr ? designatorPrefix(*part, from) : QString();
            // A part whose prefix stays keeps its designator as it is.
            if (!oldPrefix.isEmpty() && oldPrefix != designatorPrefix(*part, to)
                && designator.startsWith(oldPrefix)
                && designator.size() > oldPrefix.size() && designator.at(oldPrefix.size()).isDigit()) {
                renamed.append({s, i, designator, designatorPrefix(*part, to),
                                designator.mid(oldPrefix.size())});
            } else if (!designator.isEmpty()) {
                taken.insert(designator);
            }
        }
    }
    std::stable_sort(renamed.begin(), renamed.end(), [](const Renamed &a, const Renamed &b) {
        return designatorLess(a.old, b.old);
    });
    // Those whose new designator is taken wait for a free number, without
    // one meanwhile.
    QList<Renamed> repeated;
    for (const Renamed &r : renamed) {
        QString &designator = result.sheets[r.sheet].components[r.index].designator;
        designator = r.prefix + r.rest;
        if (taken.contains(designator)) {
            designator.clear();
            repeated.append(r);
        } else {
            taken.insert(designator);
        }
    }
    for (const Renamed &r : repeated)
        result.sheets[r.sheet].components[r.index].designator = nextDesignator(result, r.prefix);
    return result;
}

QString nextDesignator(const Schematic &schematic, const QString &prefix)
{
    QSet<int> taken;
    for (const Sheet &sheet : schematic.sheets) {
        for (const Component &component : sheet.components) {
            if (!component.designator.startsWith(prefix))
                continue;
            bool ok = false;
            const int number = component.designator.mid(prefix.size()).toInt(&ok);
            // "R1" is taken for "R", but "RV1" is not.
            if (ok && number > 0 && component.designator == prefix + QString::number(number))
                taken.insert(number);
        }
    }
    int number = 1;
    while (taken.contains(number))
        ++number;
    return prefix + QString::number(number);
}

Component newComponent(const Library &library, const QString &partName, const QString &footprint)
{
    Component component;
    component.id = newObjectId();
    component.part = partName;
    component.footprint = footprint;
    if (const Part *part = library.findPart(partName)) {
        component.value = part->value;
        component.offBoard = part->footprints.isEmpty();
        component.powerMw = part->powerMw;
    }
    if (const Symbol *symbol = library.findSymbolOfPart(partName)) {
        component.refAt = symbol->refAt;
        component.valueAt = symbol->valueAt;
    }
    return component;
}

Library withPartFrom(const Library &project, const Library &source, const QString &partName)
{
    Library result = project;
    const Part *part = source.findPart(partName);
    if (part == nullptr)
        return result;
    if (!result.parts.contains(partName))
        result.parts.insert(partName, *part);
    // The part as the project has it names what else it needs.
    const Part &kept = result.parts.value(partName);
    if (!result.symbols.contains(kept.symbol) && source.symbols.contains(kept.symbol))
        result.symbols.insert(kept.symbol, source.symbols.value(kept.symbol));
    for (const QString &footprint : kept.footprints) {
        if (!result.footprints.contains(footprint) && source.footprints.contains(footprint))
            result.footprints.insert(footprint, source.footprints.value(footprint));
    }
    return result;
}

QList<PartDifference> partDifferences(const Design &design, const Library &source)
{
    QStringList used;
    for (const Sheet &sheet : design.schematic.sheets) {
        for (const Component &component : sheet.components) {
            if (!used.contains(component.part))
                used.append(component.part);
        }
    }
    used.sort();
    // The points wires join a symbol at, by pin number.
    const auto connections = [](const Symbol *symbol) {
        QMap<QString, Point> points;
        if (symbol != nullptr) {
            for (const Pin &pin : symbol->pins)
                points.insert(pin.number, pin.at);
        }
        return points;
    };
    const Library &project = design.library;
    QList<PartDifference> result;
    for (const QString &name : used) {
        const Part *fresh = source.findPart(name);
        if (fresh == nullptr)
            continue;
        const Part *kept = project.findPart(name);
        PartDifference difference;
        difference.part = name;
        difference.fields = kept == nullptr || *kept != *fresh;
        const Symbol *oldSymbol = kept != nullptr ? project.findSymbol(kept->symbol) : nullptr;
        const Symbol *newSymbol = source.findSymbol(fresh->symbol);
        difference.pins = connections(oldSymbol) != connections(newSymbol);
        difference.drawing = (oldSymbol == nullptr) != (newSymbol == nullptr)
                             || (oldSymbol != nullptr && *oldSymbol != *newSymbol);
        for (const QString &footprint : fresh->footprints) {
            const Footprint *oldFootprint = project.findFootprint(footprint);
            const Footprint *newFootprint = source.findFootprint(footprint);
            if (newFootprint != nullptr && (oldFootprint == nullptr || *oldFootprint != *newFootprint))
                difference.footprints = true;
        }
        if (difference.fields || difference.drawing || difference.pins || difference.footprints)
            result.append(difference);
    }
    return result;
}

Design withPartsUpdated(const Design &design, const Library &source, const QStringList &parts)
{
    Design result = design;
    Library &library = result.library;
    for (const QString &name : parts) {
        const Part *part = source.findPart(name);
        if (part == nullptr)
            continue;
        library.parts.insert(name, *part);
        if (const Symbol *symbol = source.findSymbol(part->symbol))
            library.symbols.insert(part->symbol, *symbol);
        for (const QString &footprint : part->footprints) {
            if (const Footprint *found = source.findFootprint(footprint))
                library.footprints.insert(footprint, *found);
        }
        for (Sheet &sheet : result.schematic.sheets) {
            for (Component &component : sheet.components) {
                if (component.part != name)
                    continue;
                if (component.powerMw == 0)
                    component.powerMw = part->powerMw;
                if (!part->footprints.contains(component.footprint))
                    component.footprint = part->footprints.value(0);
            }
        }
    }
    return result;
}

Library withSymbolFrom(const Library &project, const Library &source, const QString &symbolName)
{
    Library result = project;
    if (!result.symbols.contains(symbolName) && source.symbols.contains(symbolName))
        result.symbols.insert(symbolName, source.symbols.value(symbolName));
    return result;
}

Sheet withoutObjects(const Sheet &sheet, const QSet<ObjectId> &ids)
{
    Sheet result = sheet;
    const auto selected = [&ids](const auto &object) { return ids.contains(object.id); };
    result.components.removeIf(selected);
    result.wires.removeIf(selected);
    result.buses.removeIf(selected);
    result.labels.removeIf(selected);
    result.powerSymbols.removeIf(selected);
    result.noConnects.removeIf(selected);
    result.graphics.removeIf(selected);
    return result;
}

QSet<ObjectId> marksOnPinsOf(const Sheet &sheet, const QSet<ObjectId> &ids,
                             const Library &library)
{
    Sheet going;
    Sheet staying;
    for (const Component &component : sheet.components)
        (ids.contains(component.id) ? going : staying).components.append(component);
    const QList<Point> pins = partPinPoints(going, library);
    const QList<Point> kept = partPinPoints(staying, library);
    QSet<ObjectId> marks;
    for (const NoConnect &mark : sheet.noConnects) {
        if (pins.contains(mark.at) && !kept.contains(mark.at))
            marks.insert(mark.id);
    }
    return marks;
}

Point dragAnchor(const Sheet &sheet, const ObjectId &id, const QPointF &near)
{
    for (const Component &component : sheet.components) {
        if (component.id == id)
            return component.at;
    }
    const auto nearest = [&near](const QList<Point> &points) {
        Point best = points.first();
        for (const Point &point : points) {
            if (QLineF(near, toMm(point)).length() < QLineF(near, toMm(best)).length())
                best = point;
        }
        return best;
    };
    for (const Wire &wire : sheet.wires) {
        if (wire.id == id && !wire.points.isEmpty())
            return nearest(wire.points);
    }
    for (const Bus &bus : sheet.buses) {
        if (bus.id == id && !bus.points.isEmpty())
            return nearest(bus.points);
    }
    for (const Label &label : sheet.labels) {
        if (label.id == id)
            return label.at;
    }
    for (const PowerSymbol &power : sheet.powerSymbols) {
        if (power.id == id)
            return power.at;
    }
    for (const NoConnect &mark : sheet.noConnects) {
        if (mark.id == id)
            return mark.at;
    }
    for (const SheetGraphic &item : sheet.graphics) {
        if (item.id == id)
            return item.graphic.a;
    }
    return Point{mmToNm(near.x()), mmToNm(near.y())};
}

int segmentAt(const QList<Point> &points, const QPointF &point)
{
    int nearest = -1;
    double nearestDistance = 0;
    for (int i = 1; i < points.size(); ++i) {
        const double distance =
            distanceToSegment(point, toMm(points.at(i - 1)), toMm(points.at(i)));
        if (nearest < 0 || distance < nearestDistance) {
            nearest = i - 1;
            nearestDistance = distance;
        }
    }
    return nearest;
}

int segmentAt(const Wire &wire, const QPointF &point)
{
    return segmentAt(wire.points, point);
}

QList<Point> linePoints(const Sheet &sheet, const ObjectId &line)
{
    if (const Wire *wire = ownerOf(sheet.wires, line))
        return wire->points;
    if (const Bus *bus = ownerOf(sheet.buses, line))
        return bus->points;
    return QList<Point>();
}

namespace {

// withoutSegment() on a list of wires or of buses.
template <typename Line>
void cutSegment(QList<Line> &lines, const ObjectId &line, int index)
{
    for (int i = 0; i < lines.size(); ++i) {
        const Line cut = lines.at(i);
        if (cut.id != line || index < 0 || index + 1 >= cut.points.size())
            continue;
        lines.removeAt(i);
        Line after = cut;
        after.id = newObjectId();
        after.points = cut.points.mid(index + 1);
        if (after.points.size() >= 2)
            lines.insert(i, after);
        Line before = cut;
        before.points = cut.points.mid(0, index + 1);
        if (before.points.size() >= 2)
            lines.insert(i, before);
        return;
    }
}

// segmentOf() on a list of wires or of buses.
template <typename Line>
void takeSegment(const QList<Line> &lines, QList<Line> &piece, const ObjectId &line, int index)
{
    for (const Line &whole : lines) {
        if (whole.id != line || index < 0 || index + 1 >= whole.points.size())
            continue;
        Line segment = whole;
        segment.points = whole.points.mid(index, 2);
        piece.append(segment);
    }
}

} // namespace

Sheet withoutSegment(const Sheet &sheet, const ObjectId &line, int index)
{
    Sheet result = sheet;
    cutSegment(result.wires, line, index);
    cutSegment(result.buses, line, index);
    return result;
}

Sheet segmentOf(const Sheet &sheet, const ObjectId &line, int index)
{
    Sheet piece;
    takeSegment(sheet.wires, piece.wires, line, index);
    takeSegment(sheet.buses, piece.buses, line, index);
    return piece;
}

Sheet objectsOf(const Sheet &sheet, const QSet<ObjectId> &ids)
{
    return withoutObjects(sheet, objectIds(withoutObjects(sheet, ids)));
}

QSet<ObjectId> objectIds(const Sheet &sheet)
{
    QSet<ObjectId> ids;
    const auto add = [&ids](const auto &objects) {
        for (const auto &object : objects)
            ids.insert(object.id);
    };
    add(sheet.components);
    add(sheet.wires);
    add(sheet.buses);
    add(sheet.labels);
    add(sheet.powerSymbols);
    add(sheet.noConnects);
    add(sheet.graphics);
    return ids;
}

Sheet withNewIds(const Sheet &piece)
{
    Sheet result = piece;
    const auto renew = [](auto &objects) {
        for (auto &object : objects)
            object.id = newObjectId();
    };
    renew(result.components);
    renew(result.wires);
    renew(result.buses);
    renew(result.labels);
    renew(result.powerSymbols);
    renew(result.noConnects);
    renew(result.graphics);
    return result;
}

Sheet withObjectsAdded(const Sheet &sheet, const Sheet &piece)
{
    Sheet result = sheet;
    result.components.append(piece.components);
    result.wires.append(piece.wires);
    result.buses.append(piece.buses);
    result.labels.append(piece.labels);
    result.powerSymbols.append(piece.powerSymbols);
    result.noConnects.append(piece.noConnects);
    result.graphics.append(piece.graphics);
    return result;
}

Sheet numberedOn(const Sheet &piece, const Schematic &schematic, const Library &library,
                 DesignatorStyle style)
{
    Sheet result = piece;
    // The schematic with the components numbered so far, so that two of
    // them do not get one designator.
    Schematic taken = schematic;
    taken.sheets.append(Sheet());
    for (Component &component : result.components) {
        const Part *part = library.findPart(component.part);
        if (part == nullptr)
            continue;
        component.designator = nextDesignator(taken, designatorPrefix(*part, style));
        taken.sheets.last().components.append(component);
    }
    return result;
}

} // namespace ppcb
