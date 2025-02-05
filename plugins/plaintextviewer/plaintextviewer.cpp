#include "plaintextviewer.h"
#include <QFile>
#include <QHeaderView>
#include <QTextStream>

PlainTextViewer::PlainTextViewer()
    : QObject()
    , m_tableWidget(new QTableWidget())
{
    m_tableWidget->setColumnCount(2);
    m_tableWidget->setHorizontalHeaderLabels({ "No.", "Log" });
    m_tableWidget->horizontalHeader()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
    m_tableWidget->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Stretch);
    m_tableWidget->setFont(QFont("Monospace"));
    m_tableWidget->verticalHeader()->hide();
}

PlainTextViewer::~PlainTextViewer()
{
    delete m_tableWidget;
}

bool PlainTextViewer::loadContent(const QVector<LogEntry>& content)
{
    m_tableWidget->clearContents();
    m_tableWidget->setRowCount(0);
    m_lines.clear();

    int lineNumber = 1;
    for (const LogEntry& entry : content) {
        m_lines.append(LineInfo(lineNumber, entry.message));

        m_tableWidget->insertRow(lineNumber - 1);
        m_tableWidget->setItem(lineNumber - 1, 0, new QTableWidgetItem(QString::number(lineNumber)));
        m_tableWidget->setItem(lineNumber - 1, 1, new QTableWidgetItem(entry.message));
        lineNumber++;
    }

    return true;
}

void PlainTextViewer::applyFilter(const FilterOptions& options)
{
    m_tableWidget->clearContents();
    m_tableWidget->setRowCount(0);

    // First pass: identify direct matches
    for (LineInfo& line : m_lines) {
        line.isDirectMatch = options.query.isEmpty() || line.content.contains(options.query, Qt::CaseInsensitive);
    }

    // Second pass: show matching lines and their context
    int currentRow = 0;
    for (int i = 0; i < m_lines.size(); ++i) {
        bool shouldShow = m_lines[i].isDirectMatch;

        if (!shouldShow) {
            // Check if this line should be shown as context before a match
            for (int j = 1; j <= options.contextLinesBefore && i + j < m_lines.size(); ++j) {
                if (m_lines[i + j].isDirectMatch) {
                    shouldShow = true;
                    break;
                }
            }
        }

        if (!shouldShow) {
            // Check if this line should be shown as context after a match
            for (int j = 1; j <= options.contextLinesAfter && i - j >= 0; ++j) {
                if (m_lines[i - j].isDirectMatch) {
                    shouldShow = true;
                    break;
                }
            }
        }

        if (shouldShow) {
            m_tableWidget->insertRow(currentRow);
            m_tableWidget->setItem(currentRow, 0, new QTableWidgetItem(QString::number(m_lines[i].number)));
            m_tableWidget->setItem(currentRow, 1, new QTableWidgetItem(m_lines[i].content));
            currentRow++;
        }
    }
}
