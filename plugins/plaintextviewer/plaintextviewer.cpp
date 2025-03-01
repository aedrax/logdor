#include "plaintextviewer.h"
#include <QHeaderView>
#include <QSet>
#include <algorithm>
#include <QtConcurrent/QtConcurrent>
#include <numeric>

PlainTextViewer::PlainTextViewer(QObject* parent)
    : PluginInterface(parent)
    , m_tableView(new QTableView())
    , m_model(new PlainTextTableModel(this))
{
    m_tableView->setModel(m_model);
    m_tableView->horizontalHeader()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
    m_tableView->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Stretch);
    m_tableView->verticalHeader()->hide();
    m_tableView->setAlternatingRowColors(true);
    m_tableView->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_tableView->setSelectionMode(QAbstractItemView::ExtendedSelection);
    
    // Connect selection changes to our handler
    connect(m_tableView->selectionModel(), &QItemSelectionModel::selectionChanged,
            this, &PlainTextViewer::onSelectionChanged);
}

PlainTextViewer::~PlainTextViewer()
{
    delete m_tableView;
}

bool PlainTextViewer::setLogs(const QList<LogEntry>& content)
{
    m_model->setLogEntries(content);
    return true;
}

void PlainTextViewer::setFilter(const FilterOptions& options)
{
    m_model->setFilter(options);
}

QList<FieldInfo> PlainTextViewer::availableFields() const
{
    return QList<FieldInfo>({
        {tr("No."), DataType::Integer},
        {tr("Log"), DataType::String}
    });
}

QSet<int> PlainTextViewer::filteredLines() const
{
    // TODO: Implement this method to return the indices of filtered out lines
    // For now, we return an empty set
    return QSet<int>();
}

void PlainTextViewer::synchronizeFilteredLines(const QSet<int>& lines)
{
    // TODO: Implement this method to synchronize filtered lines with other plugins
    Q_UNUSED(lines);
}

void PlainTextViewer::onSelectionChanged(const QItemSelection& selected, const QItemSelection& deselected)
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

void PlainTextViewer::onPluginEvent(PluginEvent event, const QVariant& data)
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
            selection.select(m_model->index(row, PlainTextColumn::No), m_model->index(row, PlainTextColumn::Log));
        }
        m_tableView->selectionModel()->select(selection, QItemSelectionModel::ClearAndSelect);

        // Scroll to the first selected line
        if (!selectedLines.isEmpty()) {
            m_tableView->scrollTo(m_model->index(m_model->mapFromSourceRow(selectedLines.first()), 0));
        }
    }
}
