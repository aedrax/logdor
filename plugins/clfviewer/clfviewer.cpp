#include "clfviewer.h"
#include <QHeaderView>

CLFViewer::CLFViewer(QObject* parent)
    : PluginInterface(parent)
    , m_tableView(new QTableView)
    , m_model(new CLFTableModel(this))
{
    m_tableView->setModel(m_model);
    m_tableView->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_tableView->setSelectionMode(QAbstractItemView::ExtendedSelection);
    // Set all columns to resize to contents by default
    m_tableView->horizontalHeader()->setSectionResizeMode(QHeaderView::ResizeToContents);
    
    // Make the path column stretch to fill available space
    m_tableView->horizontalHeader()->setSectionResizeMode(CLFColumn::Path, QHeaderView::Stretch);
    m_tableView->verticalHeader()->hide();
    m_tableView->setAlternatingRowColors(true);

    connect(m_tableView->selectionModel(), &QItemSelectionModel::selectionChanged,
            this, &CLFViewer::onSelectionChanged);
}

CLFViewer::~CLFViewer()
{
    delete m_tableView;
}

bool CLFViewer::setLogs(const QList<LogEntry>& content)
{
    m_model->setLogEntries(content);
    return true;
}

void CLFViewer::setFilter(const FilterOptions& options)
{
    m_model->setFilter(options);
}

QList<FieldInfo> CLFViewer::availableFields() const
{
    return {
        {tr("Remote Host"), DataType::String, {}},
        {tr("Identity"), DataType::String, {}},
        {tr("User ID"), DataType::String, {}},
        {tr("Timestamp"), DataType::DateTime, {}},
        {tr("Method"), DataType::String, {}},
        {tr("Path"), DataType::String, {}},
        {tr("Protocol"), DataType::String, {}},
        {tr("Status"), DataType::Integer, {}},
        {tr("Bytes"), DataType::Integer, {}}
    };
}

QSet<int> CLFViewer::filteredLines() const
{
    QSet<int> filtered;
    for (int i = 0; i < m_model->rowCount(); ++i) {
        filtered.insert(m_model->mapToSourceRow(i));
    }
    return filtered;
}

void CLFViewer::synchronizeFilteredLines(const QSet<int>& lines)
{
    // Not implemented as we use our own filtering
}

void CLFViewer::onSelectionChanged(const QItemSelection& selected, const QItemSelection& deselected)
{
    Q_UNUSED(selected);
    Q_UNUSED(deselected);
    
    QList<int> selectedLines;
    const auto indexes = m_tableView->selectionModel()->selectedRows();
    for (const auto& index : indexes) {
        selectedLines.append(m_model->mapToSourceRow(index.row()));
    }
    
    emit pluginEvent(PluginEvent::LinesSelected, QVariant::fromValue(selectedLines));
}

void CLFViewer::onPluginEvent(PluginEvent event, const QVariant& data)
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
            if (row >= 0) {
                selection.select(m_model->index(row, 0), 
                               m_model->index(row, m_model->columnCount() - 1));
            }
        }
        m_tableView->selectionModel()->select(selection, QItemSelectionModel::ClearAndSelect);

        // Scroll to the first selected line
        if (!selectedLines.isEmpty()) {
            m_tableView->scrollTo(m_model->index(m_model->mapFromSourceRow(selectedLines.first()), 0));
        }
    }
}
