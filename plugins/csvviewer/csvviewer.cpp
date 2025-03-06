#include "csvviewer.h"
#include <QtWidgets/QHeaderView>
#include <QtConcurrent/QtConcurrent>
#include <QtCore/QRegularExpression>

CsvViewer::CsvViewer(QObject* parent)
    : PluginInterface(parent)
    , m_container(new QWidget())
    , m_layout(new QVBoxLayout(m_container))
    , m_tableView(new QTableView())
    , m_model(new CsvTableModel(this))
{
    setupUi();
}

CsvViewer::~CsvViewer()
{
    delete m_container;
}

void CsvViewer::setupUi()
{
    m_layout->setContentsMargins(0, 0, 0, 0);

    // Setup table view
    m_tableView->setModel(m_model);
    m_tableView->horizontalHeader()->setSectionResizeMode(QHeaderView::Interactive);
    m_tableView->horizontalHeader()->setStretchLastSection(true);
    m_tableView->setShowGrid(true);
    m_tableView->setAlternatingRowColors(true);
    m_tableView->verticalHeader()->setVisible(true);
    m_tableView->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_tableView->setSortingEnabled(true);
    m_tableView->setSelectionMode(QAbstractItemView::ExtendedSelection);
    
    // Connect selection changes to our handler
    connect(m_tableView->selectionModel(), &QItemSelectionModel::selectionChanged,
            this, &CsvViewer::onSelectionChanged);

    m_layout->addWidget(m_tableView);
}

bool CsvViewer::setLogs(const QList<LogEntry>& content)
{
    m_entries = content;
    m_model->setLogEntries(content);
    return true;
}

void CsvViewer::setFilter(const FilterOptions& options)
{
    m_filterOptions = options;
    
    QList<int> linesToShow;
    for (int i = 0; i < m_entries.size(); ++i) {
        QString line = m_entries[i].getMessage();
        bool matches;
        
        if (m_filterOptions.inRegexMode) {
            QRegularExpression regex(m_filterOptions.query, 
                m_filterOptions.caseSensitivity == Qt::CaseInsensitive ? 
                QRegularExpression::CaseInsensitiveOption : 
                QRegularExpression::NoPatternOption);
            matches = regex.match(line).hasMatch();
        } else {
            matches = line.contains(m_filterOptions.query, m_filterOptions.caseSensitivity);
        }
        
        if (matches != m_filterOptions.invertFilter) {
            // Add context lines before
            for (int j = std::max(0, i - m_filterOptions.contextLinesBefore); j < i; ++j) {
                if (!linesToShow.contains(j)) {
                    linesToShow.append(j);
                }
            }
            
            // Add the matching line
            linesToShow.append(i);
            
            // Add context lines after
            for (int j = i + 1; j <= std::min(static_cast<int>(m_entries.size() - 1), i + m_filterOptions.contextLinesAfter); ++j) {
                if (!linesToShow.contains(j)) {
                    linesToShow.append(j);
                }
            }
        }
    }
    
    std::sort(linesToShow.begin(), linesToShow.end());
    m_model->setVisibleRows(linesToShow);
}

QList<FieldInfo> CsvViewer::availableFields() const
{
    QList<FieldInfo> fields;
    
    // Add row number field
    fields.append({"Row", DataType::Integer});
    
    // Add fields from CSV headers
    if (!m_entries.isEmpty()) {
        CsvEntry firstEntry(m_entries[0]);
        for (const QString& header : firstEntry.values) {
            // Try to detect if the column contains numbers
            bool hasNonNumeric = false;
            for (int i = 1; i < m_entries.size(); ++i) {
                CsvEntry entry(m_entries[i]);
                bool ok;
                entry.getValue(fields.size() - 1).toDouble(&ok);
                if (!ok) {
                    hasNonNumeric = true;
                    break;
                }
            }
            
            fields.append({header, hasNonNumeric ? DataType::String : DataType::Integer});
        }
    }
    
    return fields;
}

QSet<int> CsvViewer::filteredLines() const
{
    QSet<int> filtered;
    // Build filtered set from visible rows
    for (int i = 0; i < m_entries.size(); ++i) {
        if (m_model->mapFromSourceRow(i) < 0) {
            filtered.insert(i);
        }
    }
    
    return filtered;
}

void CsvViewer::synchronizeFilteredLines(const QSet<int>& lines)
{
    QList<int> visibleLines;
    for (int i = 0; i < m_entries.size(); ++i) {
        if (!lines.contains(i)) {
            visibleLines.append(i);
        }
    }
    m_model->setVisibleRows(visibleLines);
}

void CsvViewer::onPluginEvent(PluginEvent event, const QVariant& data)
{
    if (event == PluginEvent::LinesSelected) {
        QList<int> selectedLines = data.value<QList<int>>();
        if (selectedLines.isEmpty()) {
            return;
        }

        // For all selected lines, select them in the table view
        QItemSelection selection;
        for (int line : selectedLines) {
            const int row = m_model->mapFromSourceRow(line);
            if (row >= 0) { // Only select if row is visible
                selection.select(m_model->index(row, 0),
                               m_model->index(row, m_model->columnCount() - 1));
            }
        }
        m_tableView->selectionModel()->select(selection, QItemSelectionModel::ClearAndSelect);

        // Scroll to the first selected line
        if (!selectedLines.isEmpty()) {
            int firstVisibleRow = m_model->mapFromSourceRow(selectedLines.first());
            if (firstVisibleRow >= 0) {
                m_tableView->scrollTo(m_model->index(firstVisibleRow, 0));
            }
        }
    }
}

void CsvViewer::onSelectionChanged(const QItemSelection& selected, const QItemSelection& deselected)
{
    Q_UNUSED(deselected);
    Q_UNUSED(selected);
    
    QList<int> selectedLines;
    const auto indexes = m_tableView->selectionModel()->selectedRows();
    for (const auto& index : indexes) {
        selectedLines.append(m_model->mapToSourceRow(index.row()));
    }
    
    emit pluginEvent(PluginEvent::LinesSelected, QVariant::fromValue(selectedLines));
}
