#include "selectedlineviewer.h"
#include <QVBoxLayout>

SelectedLineViewer::SelectedLineViewer(QObject* parent)
    : PluginInterface(parent)
    , m_textBrowser(new QTextBrowser())
{
    m_textBrowser->setOpenExternalLinks(false);
    m_textBrowser->setReadOnly(true);
    m_textBrowser->setText(tr("No line selected"));
    m_textBrowser->setLineWrapMode(QTextEdit::WidgetWidth);
    
    // Set minimum size for better visibility
    m_textBrowser->setMinimumSize(200, 100);
}

SelectedLineViewer::~SelectedLineViewer()
{
    delete m_textBrowser;
}

bool SelectedLineViewer::setLogs(const QList<LogEntry>& logs)
{
    m_entries = logs;
    return true;
}

void SelectedLineViewer::setFilter(const FilterOptions& options)
{
    // Not needed for this plugin
    Q_UNUSED(options);
}

void SelectedLineViewer::onPluginEvent(PluginEvent event, const QVariant& data)
{
    if (event == PluginEvent::LinesSelected) {
        QList<int> selectedLines = data.value<QList<int>>();
        if (selectedLines.isEmpty()) {
            m_textBrowser->setText(tr("No line selected"));
            return;
        }

        // For all selected lines, show them in the text browser
        QString text;
        for (int line : selectedLines) {
            if (line >= 0 && line < m_entries.size()) {
                const LogEntry& entry = m_entries[line];
                text += entry.getMessage() + "\n";
            }
        }
        m_textBrowser->setText(text);
    }
}

QList<FieldInfo> SelectedLineViewer::availableFields() const
{
    // This plugin does not provide field information
    return QList<FieldInfo>();
}

QSet<int> SelectedLineViewer::filteredLines() const
{
    // This plugin does not filter lines
    return QSet<int>();
}

void SelectedLineViewer::synchronizeFilteredLines(const QSet<int>& lines)
{
    // This plugin does not synchronize filtered lines
    Q_UNUSED(lines);
}