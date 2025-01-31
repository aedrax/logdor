#include "plaintextviewer.h"
#include <QFile>
#include <QHeaderView>
#include <QTextStream>
#include <QClipboard>
#include <QApplication>

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
    m_tableWidget->setSelectionMode(QAbstractItemView::ExtendedSelection);
    m_tableWidget->setSelectionBehavior(QAbstractItemView::SelectRows);
}

void PlainTextViewer::copySelectedText()
{
    QModelIndexList selection = m_tableWidget->selectionModel()->selectedIndexes();
    if (selection.isEmpty()) {
        return;
    }

    // Sort selections by row and column to maintain order
    std::sort(selection.begin(), selection.end(), 
              [](const QModelIndex& a, const QModelIndex& b) {
                  if (a.row() != b.row())
                      return a.row() < b.row();
                  return a.column() < b.column();
              });

    QString text;
    QMap<int, QString> rowData; // Store data for each row

    // Collect data for each selected cell
    for (const QModelIndex& index : selection) {
        int row = index.row();
        // debug the row number
        qDebug() << "row number: " << row;
        // Only collect the log content column (column 1)
        if (index.column() == 1) {
            rowData[row] = index.data().toString();
        }
    }

    // Build the text with proper line breaks
    for (auto it = rowData.begin(); it != rowData.end(); ++it) {
        if (!text.isEmpty()) {
            text += "\n";
        }
        text += it.value();
    }

    if (!text.isEmpty()) {
        QClipboard* clipboard = QApplication::clipboard();
        clipboard->setText(text);
    }
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

    QTextStream in(content);
    int lineNumber = 1;
    while (!in.atEnd()) {
        QString line = in.readLine();
        m_lines.append(LineInfo(lineNumber, line));

        m_tableWidget->insertRow(lineNumber - 1);
        m_tableWidget->setItem(lineNumber - 1, 0, new QTableWidgetItem(QString::number(lineNumber)));
        m_tableWidget->setItem(lineNumber - 1, 1, new QTableWidgetItem(line));
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
