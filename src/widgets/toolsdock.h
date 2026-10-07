#pragma once

#include <QLayout>
#include <QWidget>
#include <QDockWidget>
#include <QVector>
#include <QButtonGroup>

class QToolButton;
class ToolManager;
class ITool;
class QVBoxLayout;

// FlowLayout wie CSS Flexbox: Buttons brechen einzeln in die nächste Zeile um.
class ToolFlowLayout : public QLayout {
    Q_OBJECT
public:
    explicit ToolFlowLayout(QWidget* parent = nullptr) : QLayout(parent) {}
    ~ToolFlowLayout() override;

    void addItem(QLayoutItem* item) override { m_items.append(item); }
    int  count() const override { return m_items.size(); }
    QLayoutItem* itemAt(int i) const override { return m_items.value(i); }
    QLayoutItem* takeAt(int i) override;
    QSize minimumSize() const override { return QSize(52, 52); }
    QSize sizeHint() const override { return minimumSize(); }
    void  setGeometry(const QRect& r) override;

private:
    QList<QLayoutItem*> m_items;
};

// Container, der die Werkzeug-Buttons im FlowLayout anordnet.
class ToolFlowHost : public QWidget {
    Q_OBJECT
public:
    explicit ToolFlowHost(QWidget* parent = nullptr);
    void addToolButton(QToolButton* btn);

private:
    ToolFlowLayout*       m_flow;
    QVector<QToolButton*> m_buttons;
};

// Dock mit der Werkzeugleiste.
class ToolsDock : public QDockWidget {
    Q_OBJECT
public:
    explicit ToolsDock(ToolManager* tm, QWidget* parent = nullptr);

    void selectTool(int index);
    int  toolCount() const { return m_toolButtons.size(); }

signals:
    // Wird gesendet, sobald ein Werkzeug aktiviert wird.
    // MainWindow baut daraufhin die Einstell-Leiste neu auf.
    void toolActivated(ITool* tool);

private:
    ToolManager*          m_tm;
    QButtonGroup*         m_group;
    QVector<QToolButton*> m_toolButtons;
};
