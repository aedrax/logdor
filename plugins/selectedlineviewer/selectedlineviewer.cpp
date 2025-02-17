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

bool SelectedLineViewer::loadContent(const QVector<LogEntry>& content)
{
    m_entries = content;
    return true;
}

void SelectedLineViewer::applyFilter(const FilterOptions& options)
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
