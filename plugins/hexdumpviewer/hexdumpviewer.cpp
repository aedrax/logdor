#include "hexdumpviewer.h"
#include <QFontDatabase>

HexDumpViewer::HexDumpViewer(QObject* parent)
    : PluginInterface(parent)
    , m_textEdit(new QTextEdit())
{
    m_textEdit->setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));
    m_textEdit->setReadOnly(true);
    m_textEdit->setText(tr("No line selected"));
    m_textEdit->setLineWrapMode(QTextEdit::NoWrap);
    m_textEdit->setMinimumSize(500, 200);
}

HexDumpViewer::~HexDumpViewer()
{
    delete m_textEdit;
}

void HexDumpViewer::setCoreSource(std::shared_ptr<logdor::FileSource> source,
                                  std::shared_ptr<const logdor::LineIndex> index)
{
    m_source = std::move(source);
    m_index = std::move(index);
    m_textEdit->setText(tr("No line selected"));
}

QString HexDumpViewer::generateHexDump(const QByteArray& data) const
{
    QString result;
    const int bytesPerLine = 16;
    const int dataSize = data.size();

    for (int offset = 0; offset < dataSize; offset += bytesPerLine) {
        result += QString("%1  ").arg(offset, 8, 16, QChar('0'));

        QString hexPart;
        QString asciiPart;
        for (int i = 0; i < bytesPerLine; ++i) {
            if (offset + i < dataSize) {
                const auto byte = static_cast<unsigned char>(data[offset + i]);
                hexPart += QString("%1 ").arg(byte, 2, 16, QChar('0'));
                asciiPart += (byte >= 32 && byte <= 126) ? QChar(byte) : QChar('.');
            } else {
                hexPart += "   ";
                asciiPart += " ";
            }
            if (i == 7)
                hexPart += " ";
        }

        result += hexPart + " |" + asciiPart + "|\n";
    }
    return result;
}

void HexDumpViewer::onPluginEvent(PluginEvent event, const QVariant& data)
{
    if (event != PluginEvent::LinesSelected)
        return;
    const QList<int> selectedLines = data.value<QList<int>>();
    if (selectedLines.isEmpty() || !m_source || !m_index) {
        m_textEdit->setText(tr("No line selected"));
        return;
    }

    QString output;
    for (int line : selectedLines) {
        if (line < 0 || qint64(line) >= m_index->lineCount())
            continue;
        // rawLengthOf: a hex viewer should show the real bytes incl. any \r.
        const QByteArray bytes = m_source->read(m_index->offsetOf(line),
                                                m_index->rawLengthOf(line));
        output += tr("Line %1:\n").arg(line + 1);
        output += generateHexDump(bytes);
        output += "\n";
    }
    m_textEdit->setText(output);
}
