#include "toolsdock.h"
#include "tools.h"

#include <QToolButton>
#include <QButtonGroup>
#include <QVBoxLayout>

// ===================================================================
// ToolFlowLayout
// ===================================================================
ToolFlowLayout::~ToolFlowLayout() { qDeleteAll(m_items); }

QLayoutItem* ToolFlowLayout::takeAt(int i) {
    if (i < 0 || i >= m_items.size()) return nullptr;
    return m_items.takeAt(i);
}

void ToolFlowLayout::setGeometry(const QRect& r) {
    QLayout::setGeometry(r);
    if (m_items.isEmpty()) return;
    int x = r.x(), y = r.y();
    int rowH = 0;
    for (auto* item : m_items) {
        QSize s = item->sizeHint();
        int nextX = x + s.width() + spacing();
        if (nextX - spacing() > r.right() && x > r.x()) {
            x = r.x();
            y += rowH + spacing();
            rowH = 0;
        }
        item->setGeometry(QRect(QPoint(x, y), s));
        x += s.width() + spacing();
        rowH = qMax(rowH, s.height());
    }
}

// ===================================================================
// ToolFlowHost
// ===================================================================
ToolFlowHost::ToolFlowHost(QWidget* parent) : QWidget(parent) {
    m_flow = new ToolFlowLayout(this);
    m_flow->setContentsMargins(6, 6, 6, 6);
    m_flow->setSpacing(4);
}

void ToolFlowHost::addToolButton(QToolButton* btn) {
    m_flow->addWidget(btn);
    m_buttons.push_back(btn);
}

// ===================================================================
// ToolsDock
// ===================================================================
ToolsDock::ToolsDock(ToolManager* tm, QWidget* parent)
    : QDockWidget("Werkzeuge", parent), m_tm(tm)
{
    setAllowedAreas(Qt::LeftDockWidgetArea | Qt::RightDockWidgetArea);
    setFeatures(QDockWidget::DockWidgetMovable);

    QWidget* mainWidget = new QWidget(this);
    QVBoxLayout* mainLayout = new QVBoxLayout(mainWidget);
    mainLayout->setContentsMargins(6, 6, 6, 6);

    auto* host = new ToolFlowHost(this);

    struct ToolEntry { const char* name; const char* icon; ITool* tool; bool enabled; };
    ToolEntry entries[] = {
        { "Pixelstift",         "\xE2\x9C\x8F", m_tm->pencil(), true },
        { "Pinsel",             "\xE2\x9C\x8E", m_tm->brush(), true },
        { "Rechteckauswahl",    "\xE2\x96\xAC", m_tm->rectSelect(), true },
        { "Zauberstab",         "\xE2\x9C\xA7", m_tm->magicWand(), true },
        { "Füllen",             "\xF0\x9F\x8E\xA8", m_tm->fill(), true },
        { "Verschieber",        "\xE2\x86\x94", m_tm->mover(), true },
        { "Pipette",            "\xE2\x8F\x87", m_tm->pipette(), true },
        { "Text",               "\xE2\x9C\x8E", m_tm->text(), true },
        { "Formen",             "\xE2\x97\xAF", m_tm->form(), true }
    };
    const int toolCount = sizeof(entries) / sizeof(entries[0]);

    m_group = new QButtonGroup(this);
    m_group->setExclusive(true);
    for (int i = 0; i < toolCount; ++i) {
        auto& e = entries[i];
        auto* b = new QToolButton();
        b->setText(QString::fromUtf8(e.icon));
        b->setToolTip(QString::fromUtf8(e.name));
        b->setCheckable(true);
        b->setEnabled(e.enabled);
        b->setMinimumSize(40, 40);
        b->setStyleSheet(
            "QToolButton{ background:#2a2a2a; color:#ddd; border:1px solid #444;"
            "  border-radius:3px; font-size:20px; }"
            "QToolButton:hover{ background:#3a3a3a; }"
            "QToolButton:checked{ background:#3a7bd5; color:white; }"
            "QToolButton:checked:hover{ background:#5a9be6; }"
            "QToolButton:disabled{ color:#555; }");
        // ★ Button zur exklusiven Gruppe hinzufuegen, damit immer nur
        //   ein Werkzeug ausgewaehlt sein kann.
        m_group->addButton(b);
        if (e.enabled) {
            // Bleistift (Index 0) ist das Startwerkzeug.
            if (i == 0) b->setChecked(true);
            m_toolButtons.push_back(b);
            connect(b, &QToolButton::toggled, this, [this, e](bool checked) {
                if (checked && e.tool) {
                    m_tm->setActiveTool(e.tool);
                    emit toolActivated(e.tool);
                }
            });
        }
        host->addToolButton(b);
    }
    mainLayout->addWidget(host);

    setWidget(mainWidget);
}

void ToolsDock::selectTool(int index) {
    if (index >= 0 && index < m_toolButtons.size())
        m_toolButtons[index]->setChecked(true);
}
