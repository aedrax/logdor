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
    m_directMatches.clear();
    m_lineNumberDelegate->clearLineNumbers();
    
    QTextStream in(content);
    int lineNumber = 1;
    while (!in.atEnd()) {
        QString line = in.readLine();
        m_lines.append(qMakePair(lineNumber, line));
        m_directMatches.append(false);
        m_listWidget->addItem(line);
        m_lineNumberDelegate->setLineNumber(lineNumber - 1, lineNumber);
        lineNumber++;
    }

    return true;
}

void PlainTextViewer::applyFilter(const QString& query, int contextLinesBefore, int contextLinesAfter)
{
    m_listWidget->clear();
    m_lineNumberDelegate->clearLineNumbers();
    
    // First pass: identify direct matches
    m_directMatches.resize(m_lines.size());
    for (int i = 0; i < m_lines.size(); ++i) {
        m_directMatches[i] = query.isEmpty() || m_lines[i].second.contains(query, Qt::CaseInsensitive);
    }

    // Second pass: show matching lines and their context
    int currentRow = 0;
    for (int i = 0; i < m_lines.size(); ++i) {
        bool shouldShow = m_directMatches[i];

        if (!shouldShow) {
            // Check if this line should be shown as context before a match
            for (int j = 1; j <= contextLinesBefore && i + j < m_lines.size(); ++j) {
                if (m_directMatches[i + j]) {
                    shouldShow = true;
                    break;
                }
            }
        }

        if (!shouldShow) {
            // Check if this line should be shown as context after a match
            for (int j = 1; j <= contextLinesAfter && i - j >= 0; ++j) {
                if (m_directMatches[i - j]) {
                    shouldShow = true;
                    break;
                }
            }
        }

        if (shouldShow) {
            m_listWidget->addItem(m_lines[i].second);
            m_lineNumberDelegate->setLineNumber(currentRow, m_lines[i].first);
            currentRow++;
        }
    }
}
