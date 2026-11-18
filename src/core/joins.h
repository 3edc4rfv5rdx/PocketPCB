#pragma once

#include <QList>

namespace ppcb {

// Sets of nodes joined one with another (union-find): add() makes a node
// of its own, join() puts two in one set, root() names the set.
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

} // namespace ppcb
