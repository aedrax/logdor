#include "plaintextviewer.h"
#include <QFile>
#include <QTextStream>

PlainTextViewer::PlainTextViewer()
    : QObject()
    , m_listWidget(new QListWidget())
{
}

PlainTextViewer::~PlainTextViewer()
{
    delete m_listWidget;
}

bool PlainTextViewer::loadContent(const QByteArray& content)
{
    m_originalContent = content;
    m_listWidget->clear();
    QTextStream in(content);
    while (!in.atEnd()) {
        QString line = in.readLine();
        m_listWidget->addItem(line);
    }

    return true;
}

void PlainTextViewer::applyFilter(const QString& query)
{
    m_listWidget->clear();
    QTextStream in(m_originalContent);
    while (!in.atEnd()) {
        QString line = in.readLine();
        if (query.isEmpty() || line.contains(query, Qt::CaseInsensitive)) {
            m_listWidget->addItem(line);
        }
    }
}
