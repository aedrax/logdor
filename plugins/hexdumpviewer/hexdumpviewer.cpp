#include "hexdumpviewer.h"
#include <QVBoxLayout>
#include <QFontDatabase>
#include <QDebug>

HexDumpViewer::HexDumpViewer(QObject* parent)
    : PluginInterface(parent)
    , m_textEdit(new QTextEdit())
{
    // Use monospace font for better hexdump display
    QFont font = QFontDatabase::systemFont(QFontDatabase::FixedFont);
    m_textEdit->setFont(font);
    
    m_textEdit->setReadOnly(true);
    m_textEdit->setText(tr("No line selected"));
    m_textEdit->setLineWrapMode(QTextEdit::NoWrap);
    
    // Set minimum size for better visibility
    m_textEdit->setMinimumSize(500, 200);
}

HexDumpViewer::~HexDumpViewer()
{
    delete m_textEdit;
}

bool HexDumpViewer::setLogs(const QList<LogEntry>& logs)
{
    m_entries = logs;
    return true;
}

void HexDumpViewer::setFilter(const FilterOptions& options)
{
    // Not needed for this plugin
    Q_UNUSED(options);
}

QString HexDumpViewer::generateHexDump(const QByteArray& data) const
{
    QString result;
    const int bytesPerLine = 16;
    const int dataSize = data.size();
    
    for (int offset = 0; offset < dataSize; offset += bytesPerLine) {
        // Add offset
        result += QString("%1  ").arg(offset, 8, 16, QChar('0'));
        
        QString hexPart;
        QString asciiPart;
        
        // Process up to 16 bytes per line
        for (int i = 0; i < bytesPerLine; ++i) {
            if (offset + i < dataSize) {
                unsigned char byte = static_cast<unsigned char>(data[offset + i]);
                
                // Add hex representation
                hexPart += QString("%1 ").arg(byte, 2, 16, QChar('0'));
                
                // Add ASCII representation
                if (byte >= 32 && byte <= 126) {
                    asciiPart += QChar(byte);
                } else {
                    asciiPart += '.';
                }
            } else {
                // Padding for incomplete lines
                hexPart += "   ";
                asciiPart += " ";
            }
            
            // Add extra space between 8-byte groups
            if (i == 7) {
                hexPart += " ";
            }
        }
        
        result += hexPart;
        result += " |";
        result += asciiPart;
        result += "|\n";
    }
    
    return result;
}

void HexDumpViewer::onPluginEvent(PluginEvent event, const QVariant& data)
{
    if (event == PluginEvent::LinesSelected) {
        QList<int> selectedLines = data.value<QList<int>>();
        if (selectedLines.isEmpty()) {
            m_textEdit->setText(tr("No line selected"));
            return;
        }

        QString output;
        for (int line : selectedLines) {
            if (line >= 0 && line < m_entries.size()) {
                const LogEntry& entry = m_entries[line];
                output += tr("Line %1:\n").arg(line + 1);
                output += generateHexDump(QByteArray(entry.message, static_cast<int>(entry.length)));
                output += "\n";
            }
        }
        m_textEdit->setText(output);
    }
}

QList<FieldInfo> HexDumpViewer::availableFields() const
{
    // This plugin does not provide field information
    return QList<FieldInfo>();
}

QSet<int> HexDumpViewer::filteredLines() const
{
    // This plugin does not filter lines
    return QSet<int>();
}

void HexDumpViewer::synchronizeFilteredLines(const QSet<int>& lines)
{
    // This plugin does not synchronize filtered lines
    Q_UNUSED(lines);
}
