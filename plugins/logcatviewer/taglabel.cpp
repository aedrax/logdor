#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>

#include "taglabel.h"

TagLabel::TagLabel(const QString& tag, QWidget* parent)
    : QFrame(parent)
    , m_tag(tag)
    , m_layout(new QHBoxLayout(this))
    , m_label(new QLabel(tag))
    , m_removeButton(new QPushButton("✕"))
{
    setFrameStyle(QFrame::StyledPanel | QFrame::Raised);
    setStyleSheet("QFrame { background: #e0e0e0; border-radius: 4px; margin: 2px; }"
                  "QLabel { color: black; }");

    m_layout->setContentsMargins(6, 2, 2, 2);
    m_layout->setSpacing(4);

    m_removeButton->setFixedSize(16, 16);
    m_removeButton->setStyleSheet(
        "QPushButton { border: none; color: #666; background: transparent; padding: 0; }"
        "QPushButton:hover { color: #000; }");

    m_layout->addWidget(m_label);
    m_layout->addWidget(m_removeButton);

    connect(m_removeButton, &QPushButton::clicked, this, &TagLabel::removed);
}