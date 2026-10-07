#include "historydock.h"
#include "history.h"

#include <QListWidget>
#include <QListWidgetItem>

HistoryDock::HistoryDock(History* history, QWidget* parent)
    : QDockWidget("Verlauf", parent), m_history(history)
{
    setAllowedAreas(Qt::LeftDockWidgetArea | Qt::RightDockWidgetArea);
    setFeatures(QDockWidget::DockWidgetMovable);

    m_list = new QListWidget(this);
    m_list->setStyleSheet(
        "QListWidget{ background:#1f1f1f; color:#ddd; }"
        "QListWidget::item{ padding:4px; }"
        "QListWidget::item:selected{ background:#3a7bd5; color:white; }");
    setWidget(m_list);

    m_histConn = connect(m_history, &History::changed, this, [this]() { rebuild(); },
            Qt::QueuedConnection);
    connect(m_list, &QListWidget::currentRowChanged, this, [this](int row) {
        if (m_suppress || row < 0) return;
        m_history->jumpTo(row);
    });
    rebuild();
}

// Multi-Document: Dock auf anderen Verlauf umschwenken.
// Trennt die alte Signal-Verbindung, verbindet die neue History und baut
// die Liste neu auf.
void HistoryDock::setHistory(History* history) {
    if (!history || history == m_history) return;
    QObject::disconnect(m_histConn);
    m_history = history;
    m_histConn = connect(m_history, &History::changed, this, [this]() { rebuild(); },
            Qt::QueuedConnection);
    rebuild();
}

void HistoryDock::rebuild() {
    m_suppress = true;
    m_list->clear();
    auto* root = new QListWidgetItem("Neues Bild");
    root->setForeground(QColor("#9bd"));
    m_list->addItem(root);
    for (int i = 0; i < m_history->count(); ++i) {
        auto* item = new QListWidgetItem(m_history->nameAt(i));
        if (i >= m_history->current()) item->setForeground(QColor("#666"));
        m_list->addItem(item);
    }
    m_list->setCurrentRow(m_history->current());
    m_suppress = false;
}