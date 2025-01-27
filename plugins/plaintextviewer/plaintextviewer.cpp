#include "plaintextviewer.h"
#include <QFile>
#include <QTextStream>
#include <QHeaderView>

PlainTextViewer::PlainTextViewer()
    : QObject()
    , m_tableWidget(new QTableWidget())
{
    m_tableWidget->setColumnCount(2);
    m_tableWidget->setHorizontalHeaderLabels({"No.", "Log"});
    m_tableWidget->horizontalHeader()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
    m_tableWidget->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Stretch);
    m_tableWidget->setFont(QFont("Monospace"));
    m_tableWidget->verticalHeader()->hide();
}

PlainTextViewer::~PlainTextViewer()
{
    delete m_tableWidget;
}

bool PlainTextViewer::loadContent(const QByteArray& content)
{
    m_originalContent = content;
    m_tableWidget->clearContents();
    m_tableWidget->setRowCount(0);
    m_lines.clear();
    m_directMatches.clear();
    
    QTextStream in(content);
    int lineNumber = 1;
    while (!in.atEnd()) {
        QString line = in.readLine();
        m_lines.append(qMakePair(lineNumber, line));
        m_directMatches.append(false);
        
        m_tableWidget->insertRow(lineNumber - 1);
        m_tableWidget->setItem(lineNumber - 1, 0, new QTableWidgetItem(QString::number(lineNumber)));
        m_tableWidget->setItem(lineNumber - 1, 1, new QTableWidgetItem(line));
        lineNumber++;
    }

    return true;
}

void PlainTextViewer::applyFilter(const QString& query, int contextLinesBefore, int contextLinesAfter)
{
    m_tableWidget->clearContents();
    m_tableWidget->setRowCount(0);
    
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
            m_tableWidget->insertRow(currentRow);
            m_tableWidget->setItem(currentRow, 0, new QTableWidgetItem(QString::number(m_lines[i].first)));
            m_tableWidget->setItem(currentRow, 1, new QTableWidgetItem(m_lines[i].second));
            currentRow++;
        }
    }
}
