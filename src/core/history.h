#pragma once

#include <QObject>
#include <QList>
#include <QString>

class ICommand;

// Undo/Redo-Stapel. Übernimmt Eigentum an den Commands.
class History : public QObject {
    Q_OBJECT
public:
    explicit History(QObject* parent = nullptr) : QObject(parent) {}

    void clear();
    void push(ICommand* cmd);
    void undo();
    void redo();
    void jumpTo(int target);

    int     count()   const { return m_stack.size(); }
    int     current() const { return m_current; }
    QString nameAt(int i) const;

signals:
    void changed();

private:
    QList<ICommand*> m_stack;
    int              m_current = 0;
};
