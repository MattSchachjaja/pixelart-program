#include "history.h"
#include "commands.h"

#include <QtGlobal>

void History::clear() {
    qDeleteAll(m_stack);
    m_stack.clear();
    m_current = 0;
    emit changed();
}

void History::push(ICommand* cmd) {
    while (m_stack.size() > m_current) delete m_stack.takeLast();
    cmd->execute();
    m_stack.append(cmd);
    ++m_current;
    emit changed();
}

void History::undo() {
    if (m_current == 0) return;
    --m_current;
    m_stack[m_current]->undo();
    emit changed();
}

void History::redo() {
    if (m_current >= m_stack.size()) return;
    m_stack[m_current]->execute();
    ++m_current;
    emit changed();
}

void History::jumpTo(int target) {
    target = qBound(0, target, m_stack.size());
    while (m_current > target) { --m_current; m_stack[m_current]->undo(); }
    while (m_current < target) { m_stack[m_current]->execute(); ++m_current; }
    emit changed();
}

QString History::nameAt(int i) const {
    return (i >= 0 && i < m_stack.size()) ? m_stack[i]->name() : QString();
}
