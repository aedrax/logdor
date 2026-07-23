#include "selectedlineviewer.h"

SelectedLineViewer::SelectedLineViewer(QObject* parent)
    : PluginInterface(parent)
    , m_textBrowser(new QTextBrowser())
{
    m_textBrowser->setOpenExternalLinks(false);
    m_textBrowser->setReadOnly(true);
    m_textBrowser->setText(tr("No line selected"));
    m_textBrowser->setLineWrapMode(QTextEdit::WidgetWidth);
    m_textBrowser->setMinimumSize(200, 100);
}

SelectedLineViewer::~SelectedLineViewer()
{
    delete m_textBrowser;
}

void SelectedLineViewer::setCoreSource(std::shared_ptr<logdor::FileSource> source,
                                       std::shared_ptr<const logdor::LineIndex> index)
{
    m_source = std::move(source);
    m_index = std::move(index);
    m_textBrowser->setText(tr("No line selected"));
}

bool SelectedLineViewer::setLogs(const QList<LogEntry>& content)
{
    // Legacy path unused: this plugin is fed through setCoreSource().
    Q_UNUSED(content)
    return true;
}

void SelectedLineViewer::setFilter(const FilterOptions& options)
{
    Q_UNUSED(options)
}

void SelectedLineViewer::onPluginEvent(PluginEvent event, const QVariant& data)
{
    if (event != PluginEvent::LinesSelected)
        return;
    const QList<int> selectedLines = data.value<QList<int>>();
    if (selectedLines.isEmpty() || !m_source || !m_index) {
        m_textBrowser->setText(tr("No line selected"));
        return;
    }

    QString text;
    for (int line : selectedLines) {
        if (line >= 0 && qint64(line) < m_index->lineCount()) {
            const QByteArray raw = m_source->read(m_index->offsetOf(line),
                                                  m_index->lengthOf(line));
            text += QString::fromUtf8(raw) + QLatin1Char('\n');
        }
    }
    m_textBrowser->setText(text);
}

QList<FieldInfo> SelectedLineViewer::availableFields() const
{
    return QList<FieldInfo>();
}

QSet<int> SelectedLineViewer::filteredLines() const
{
    return QSet<int>();
}

void SelectedLineViewer::synchronizeFilteredLines(const QSet<int>& lines)
{
    Q_UNUSED(lines)
}
