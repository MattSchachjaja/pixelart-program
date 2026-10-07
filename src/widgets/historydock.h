#pragma once

#include <QDockWidget>
#include <QMetaObject>

class QListWidget;
class History;

// Dock, der die Befehlshistorie als Liste zeigt; Klick springt zum Zustand.
class HistoryDock : public QDockWidget {
    Q_OBJECT
public:
    HistoryDock(History* history, QWidget* parent = nullptr);

    // Multi-Document: Dock auf anderen Verlauf umschwenken.
    void setHistory(History* history);

private:
    void rebuild();

    History*     m_history;
    QListWidget* m_list;
    bool         m_suppress = false;

    // Signal-Verbindung zur History (für Multi-Document-Tausch).
    QMetaObject::Connection m_histConn;
};