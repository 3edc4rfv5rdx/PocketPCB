#include "netlist.h"

#include "sheetedit.h"
#include "wiring.h"

#include <QHash>

#include <algorithm>

namespace ppcb {

namespace {

// What a net's table name starts with, a number after it.
const char TABLE_NAME_PREFIX[] = "NN";

// Sets of nodes joined one with another.
class Joins
{
public:
    int add()
    {
        m_parent.append(int(m_parent.size()));
        return int(m_parent.size()) - 1;
    }

    int root(int node)
    {
        while (m_parent.at(node) != node) {
            m_parent[node] = m_parent.at(m_parent.at(node));
            node = m_parent.at(node);
        }
        return node;
    }

    void join(int a, int b) { m_parent[root(a)] = root(b); }

private:
    QList<int> m_parent;
};

// What a node of the nets stands for.
struct Node
{
    enum class Kind { Wire, Pin, Power, Label };
    Kind kind = Kind::Wire;
    int sheet = 0;
    ObjectId id;
    // A pin: of the component 'id'.
    NetPin pin;
    // A power symbol or a label: the name it gives.
    QString name;
};

bool pinLess(const NetPin &a, const NetPin &b)
{
    if (a.designator != b.designator)
        return naturalLess(a.designator, b.designator);
    return naturalLess(a.number, b.number);
}

} // namespace

bool operator==(const NetPin &a, const NetPin &b)
{
    return a.component == b.component && a.designator == b.designator && a.number == b.number;
}

bool netHasPin(const Net &net, const ObjectId &component, const QString &number)
{
    return std::any_of(net.pins.cbegin(), net.pins.cend(), [&](const NetPin &pin) {
        return pin.component == component && pin.number == number;
    });
}

int netAt(const QList<Net> &nets, const Schematic &schematic, const Library &library, int sheet,
          const QPointF &point, double tolerance)
{
    if (sheet < 0 || sheet >= schematic.sheets.size())
        return -1;
    const Sheet &on = schematic.sheets.at(sheet);
    // The net drawn with a wire, a label or a power symbol of the sheet.
    const auto netOf = [&](const ObjectId &id) {
        for (int i = 0; i < nets.size(); ++i) {
            if (nets.at(i).objects.value(sheet).contains(id))
                return i;
        }
        return -1;
    };
    const QList<SheetObjectShape> shapes = sheetObjectShapes(on, library);
    const ObjectId hit = objectAt(shapes, point, tolerance);
    // A wire comes first, as objectAt() takes a line before a rectangle.
    const bool byLine =
        std::any_of(shapes.cbegin(), shapes.cend(), [&hit](const SheetObjectShape &shape) {
            return shape.id == hit && !shape.polyline.isEmpty();
        });
    if (byLine) {
        const int net = netOf(hit);
        if (net >= 0)
            return net;
    }
    // Then the line of a pin, thinner than the rectangle around its owner.
    for (const SheetPin &pin : sheetPins(on, library)) {
        QPolygonF line;
        line << toMm(pin.at) << toMm(pin.bodyEnd);
        if (!isNearPolyline(line, point, tolerance))
            continue;
        // A power symbol is drawn with its net; a component's pin is on one.
        const int net = netOf(pin.owner);
        if (net >= 0)
            return net;
        for (int i = 0; i < nets.size(); ++i) {
            if (netHasPin(nets.at(i), pin.owner, pin.number))
                return i;
        }
        return -1;
    }
    return netOf(hit);
}

bool naturalLess(const QString &a, const QString &b)
{
    int i = 0;
    int j = 0;
    while (i < a.size() && j < b.size()) {
        if (a.at(i).isDigit() && b.at(j).isDigit()) {
            int ei = i;
            int ej = j;
            while (ei < a.size() && a.at(ei).isDigit())
                ++ei;
            while (ej < b.size() && b.at(ej).isDigit())
                ++ej;
            // Without the zeros in front, the longer number is the larger.
            QString x = a.mid(i, ei - i);
            QString y = b.mid(j, ej - j);
            while (x.size() > 1 && x.startsWith(QLatin1Char('0')))
                x.remove(0, 1);
            while (y.size() > 1 && y.startsWith(QLatin1Char('0')))
                y.remove(0, 1);
            if (x.size() != y.size())
                return x.size() < y.size();
            if (x != y)
                return x < y;
            i = ei;
            j = ej;
            continue;
        }
        if (a.at(i) != b.at(j))
            return a.at(i) < b.at(j);
        ++i;
        ++j;
    }
    return a.size() - i < b.size() - j;
}

QList<Net> nets(const Schematic &schematic, const Library &library, const Project &project)
{
    Joins joins;
    QList<Node> nodes;
    const auto addNode = [&](const Node &node) {
        nodes.append(node);
        return joins.add();
    };
    // The first node of each name: the others of that name join it.
    QHash<QString, int> byName;
    const auto joinName = [&](const QString &name, int node) {
        const auto it = byName.constFind(name);
        if (it == byName.constEnd())
            byName.insert(name, node);
        else
            joins.join(node, *it);
    };

    for (int s = 0; s < schematic.sheets.size(); ++s) {
        const Sheet &sheet = schematic.sheets.at(s);
        // The things with a point: pins, power symbols and labels.
        QList<QPair<Point, int>> points;
        for (const Component &component : sheet.components) {
            const Symbol *symbol = library.findSymbolOfPart(component.part);
            if (symbol == nullptr)
                continue;
            for (const Pin &pin : symbol->pins) {
                Node node;
                node.kind = Node::Kind::Pin;
                node.sheet = s;
                node.id = component.id;
                node.pin = NetPin{component.id, component.designator, pin.number};
                points.append({componentPoint(component, pin.at), addNode(node)});
            }
        }
        for (const PowerSymbol &power : sheet.powerSymbols) {
            Node node;
            node.kind = Node::Kind::Power;
            node.sheet = s;
            node.id = power.id;
            node.name = power.name;
            const int index = addNode(node);
            points.append({power.at, index});
            joinName(power.name, index);
        }
        for (const Label &label : sheet.labels) {
            Node node;
            node.kind = Node::Kind::Label;
            node.sheet = s;
            node.id = label.id;
            node.name = label.text;
            const int index = addNode(node);
            points.append({label.at, index});
            joinName(label.text, index);
        }
        // What stands at one point is joined.
        for (int i = 0; i < points.size(); ++i) {
            for (int j = i + 1; j < points.size(); ++j) {
                if (points.at(i).first == points.at(j).first)
                    joins.join(points.at(i).second, points.at(j).second);
            }
        }
        QList<int> wireNodes;
        for (const Wire &wire : sheet.wires) {
            Node node;
            node.kind = Node::Kind::Wire;
            node.sheet = s;
            node.id = wire.id;
            const int index = addNode(node);
            wireNodes.append(index);
            for (const auto &[point, other] : points) {
                if (isOnWire(wire, point))
                    joins.join(index, other);
            }
        }
        for (int i = 0; i < sheet.wires.size(); ++i) {
            const Wire &a = sheet.wires.at(i);
            if (a.points.isEmpty())
                continue;
            for (int j = 0; j < sheet.wires.size(); ++j) {
                const Wire &b = sheet.wires.at(j);
                if (i != j && (isOnWire(b, a.points.first()) || isOnWire(b, a.points.last())))
                    joins.join(wireNodes.at(i), wireNodes.at(j));
            }
        }
    }

    // The nodes gathered by net.
    QMap<int, Net> byRoot;
    QHash<int, QStringList> powerNames;
    QHash<int, QStringList> labelNames;
    for (int i = 0; i < nodes.size(); ++i) {
        const Node &node = nodes.at(i);
        const int root = joins.root(i);
        Net &net = byRoot[root];
        switch (node.kind) {
        case Node::Kind::Pin:
            net.pins.append(node.pin);
            break;
        case Node::Kind::Power:
            powerNames[root].append(node.name);
            net.objects[node.sheet].insert(node.id);
            break;
        case Node::Kind::Label:
            labelNames[root].append(node.name);
            net.objects[node.sheet].insert(node.id);
            break;
        case Node::Kind::Wire:
            net.objects[node.sheet].insert(node.id);
            break;
        }
    }

    // The nets kept, and the names given to each.
    QList<Net> kept;
    QList<QStringList> givenNames;
    for (auto it = byRoot.begin(); it != byRoot.end(); ++it) {
        Net net = it.value();
        // A pin with nothing joined to it is no net.
        if (net.objects.isEmpty() && net.pins.size() < 2)
            continue;
        std::sort(net.pins.begin(), net.pins.end(), pinLess);
        QStringList names = powerNames.value(it.key());
        names.removeDuplicates();
        std::sort(names.begin(), names.end(), naturalLess);
        QStringList labels = labelNames.value(it.key());
        labels.removeDuplicates();
        std::sort(labels.begin(), labels.end(), naturalLess);
        kept.append(net);
        // A power symbol's name first, then a label's.
        givenNames.append(names + labels);
    }

    // The table names go to the nets holding most of their pins.
    QHash<ObjectId, QHash<QString, int>> netOfPin;
    for (int i = 0; i < kept.size(); ++i) {
        for (const NetPin &pin : kept.at(i).pins)
            netOfPin[pin.component].insert(pin.number, i);
    }
    QList<NetByPins> tableNames = project.netsByPins;
    std::sort(tableNames.begin(), tableNames.end(),
              [](const NetByPins &a, const NetByPins &b) { return naturalLess(a.name, b.name); });
    // The table names each net is given, in alphabetical order.
    QMap<int, QStringList> givenTableNames;
    for (const NetByPins &tableName : tableNames) {
        QHash<int, int> held;
        for (const PinRef &pin : tableName.pins) {
            const int net = netOfPin.value(pin.component).value(pin.number, -1);
            if (net >= 0)
                ++held[net];
        }
        int best = -1;
        for (auto it = held.cbegin(); it != held.cend(); ++it) {
            if (best < 0 || it.value() > held.value(best)
                || (it.value() == held.value(best)
                    && pinLess(kept.at(it.key()).pins.first(), kept.at(best).pins.first()))) {
                best = it.key();
            }
        }
        if (best >= 0)
            givenTableNames[best].append(tableName.name);
    }
    for (auto it = givenTableNames.cbegin(); it != givenTableNames.cend(); ++it) {
        QString strongest = it.value().first();
        for (const QString &name : it.value()) {
            if (strongerClass(project, project.classOfNet.value(name),
                              project.classOfNet.value(strongest))) {
                strongest = name;
            }
        }
        Net &net = kept[it.key()];
        net.tableName = strongest;
        for (const QString &name : it.value()) {
            if (name != strongest)
                net.mergedTableNames.append(name);
        }
    }

    QList<Net> named;
    QList<Net> unnamed;
    for (int i = 0; i < kept.size(); ++i) {
        Net net = kept.at(i);
        const QStringList &given = givenNames.at(i);
        if (given.isEmpty()) {
            if (net.tableName.isEmpty()) {
                unnamed.append(net);
            } else {
                net.name = net.tableName;
                net.named = true;
                named.append(net);
            }
            continue;
        }
        net.name = given.first();
        net.named = true;
        for (const QString &name : given) {
            if (name != net.name && !net.otherNames.contains(name))
                net.otherNames.append(name);
        }
        std::sort(net.otherNames.begin(), net.otherNames.end(), naturalLess);
        named.append(net);
    }
    std::sort(named.begin(), named.end(),
              [](const Net &a, const Net &b) { return naturalLess(a.name, b.name); });
    // The nets with no name are numbered in the order of their first pins;
    // those with no pin come last, in the order of their sheet.
    std::stable_sort(unnamed.begin(), unnamed.end(), [](const Net &a, const Net &b) {
        if (a.pins.isEmpty() != b.pins.isEmpty())
            return b.pins.isEmpty();
        if (!a.pins.isEmpty())
            return pinLess(a.pins.first(), b.pins.first());
        return a.objects.firstKey() < b.objects.firstKey();
    });
    for (int i = 0; i < unnamed.size(); ++i)
        unnamed[i].name = QStringLiteral("N") + QString::number(i + 1);
    return named + unnamed;
}

QString pinsText(const QList<NetPin> &pins)
{
    QStringList texts;
    for (const NetPin &pin : pins)
        texts.append(pin.designator + QLatin1Char('.') + pin.number);
    return texts.join(QStringLiteral(", "));
}

QList<int> netSheets(const Net &net, const Schematic &schematic)
{
    QList<int> result;
    for (int s = 0; s < schematic.sheets.size(); ++s) {
        const QList<Component> &components = schematic.sheets.at(s).components;
        const bool hasPin = std::any_of(net.pins.cbegin(), net.pins.cend(), [&](const NetPin &pin) {
            return std::any_of(components.cbegin(), components.cend(),
                               [&pin](const Component &c) { return c.id == pin.component; });
        });
        if (net.objects.contains(s) || hasPin)
            result.append(s);
    }
    return result;
}

bool strongerClass(const Project &project, const QString &a, const QString &b)
{
    // The track width of a class, -1 for one not in the list.
    const auto width = [&project](const QString &name) {
        for (const NetClass &netClass : project.netClasses) {
            if (netClass.name == name)
                return netClass.trackWidth;
        }
        return Coord(-1);
    };
    return width(a) > width(b);
}

QString netClassOf(const Net &net, const Project &project, const Schematic &schematic)
{
    // The strongest of 'classes' in the project's list; empty when none is.
    const auto strongest = [&project](const QStringList &classes) {
        QString result;
        for (const QString &name : classes) {
            if (strongerClass(project, name, result))
                result = name;
        }
        return result;
    };
    // A name of "N" and a number belongs to no net for good.
    QStringList keys{net.tableName};
    if (net.named)
        keys += QStringList{net.name} + net.otherNames;
    QStringList chosen;
    for (const QString &key : keys) {
        if (!key.isEmpty() && project.classOfNet.contains(key))
            chosen.append(project.classOfNet.value(key));
    }
    const QString fromTable = strongest(chosen);
    if (!fromTable.isEmpty())
        return fromTable;
    QStringList fromLabels;
    bool powered = false;
    for (auto it = net.objects.cbegin(); it != net.objects.cend(); ++it) {
        if (it.key() < 0 || it.key() >= schematic.sheets.size())
            continue;
        const Sheet &sheet = schematic.sheets.at(it.key());
        for (const Label &label : sheet.labels) {
            if (it.value().contains(label.id))
                fromLabels.append(label.netClass);
        }
        for (const PowerSymbol &power : sheet.powerSymbols)
            powered = powered || it.value().contains(power.id);
    }
    const QString fromLabel = strongest(fromLabels);
    if (!fromLabel.isEmpty())
        return fromLabel;
    return powered ? project.powerClass : project.defaultClass;
}

Project withNetClass(const Project &project, const QList<Net> &all, const Net &net,
                     const QString &netClass)
{
    Project result = project;
    for (const QString &merged : net.mergedTableNames)
        result.classOfNet.remove(merged);
    result.netsByPins.removeIf([&net](const NetByPins &byPins) {
        return net.mergedTableNames.contains(byPins.name);
    });
    // Named by a power symbol or a label.
    if (net.named && net.name != net.tableName) {
        result.classOfNet.insert(net.name, netClass);
        return result;
    }
    QString name = net.tableName;
    if (name.isEmpty()) {
        if (net.pins.isEmpty())
            return project;
        QSet<QString> taken;
        for (const Net &other : all) {
            taken.insert(other.name);
            for (const QString &otherName : other.otherNames)
                taken.insert(otherName);
        }
        for (const NetByPins &byPins : project.netsByPins)
            taken.insert(byPins.name);
        for (auto it = project.classOfNet.cbegin(); it != project.classOfNet.cend(); ++it)
            taken.insert(it.key());
        int number = 1;
        while (taken.contains(QLatin1String(TABLE_NAME_PREFIX) + QString::number(number)))
            ++number;
        name = QLatin1String(TABLE_NAME_PREFIX) + QString::number(number);
        result.netsByPins.append(NetByPins{name, {}});
    }
    for (NetByPins &byPins : result.netsByPins) {
        if (byPins.name != name)
            continue;
        byPins.pins.clear();
        for (const NetPin &pin : net.pins)
            byPins.pins.append(PinRef{pin.component, pin.number});
    }
    result.classOfNet.insert(name, netClass);
    return result;
}

} // namespace ppcb
