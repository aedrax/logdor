#include "plaintextviewer.h"
#include <QFile>
#include <QTextStream>

PlainTextViewer::PlainTextViewer()
    : QObject()
    , m_listWidget(new QListWidget())
    , m_lineNumberDelegate(new LineNumberDelegate(this))
{
    m_listWidget->setItemDelegate(m_lineNumberDelegate);
    m_listWidget->setFont(QFont("Monospace"));
}

PlainTextViewer::~PlainTextViewer()
{
    delete m_listWidget;
}

bool PlainTextViewer::loadContent(const QByteArray& content)
{
    m_originalContent = content;
    m_listWidget->clear();
    m_lines.clear();
    m_lineNumberDelegate->clearLineNumbers();
    
    QTextStream in(content);
    int lineNumber = 1;
    while (!in.atEnd()) {
        QString line = in.readLine();
        m_lines.append(qMakePair(lineNumber, line));
        m_listWidget->addItem(line);
        m_lineNumberDelegate->setLineNumber(lineNumber - 1, lineNumber);
        lineNumber++;
    }

    return true;
}

void PlainTextViewer::applyFilter(const QString& query)
{
    m_listWidget->clear();
    m_lineNumberDelegate->clearLineNumbers();
    
    int currentRow = 0;
    for (const auto& pair : m_lines) {
        if (query.isEmpty() || pair.second.contains(query, Qt::CaseInsensitive)) {
            m_listWidget->addItem(pair.second);
            m_lineNumberDelegate->setLineNumber(currentRow, pair.first);
            currentRow++;
        }
    }
}
