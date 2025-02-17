#include "bookmarkviewer.h"
#include <QVBoxLayout>
#include <QLabel>
#include <QSplitter>
#include <QTimer>

BookmarkViewer::BookmarkViewer(QObject* parent)
    : PluginInterface(parent)
    , m_widget(new QWidget())
    , m_bookmarkList(new QListWidget())
    , m_noteEdit(new QTextEdit())
    , m_currentBookmark(-1)
    , m_pendingBookmark(-1)
{
    // Create layout
    QVBoxLayout* mainLayout = new QVBoxLayout(m_widget);
    
    // Create splitter for resizable sections
    QSplitter* splitter = new QSplitter(Qt::Vertical);
    
    // Setup bookmark list
    QWidget* bookmarkWidget = new QWidget();
    QVBoxLayout* bookmarkLayout = new QVBoxLayout(bookmarkWidget);
    QLabel* bookmarkLabel = new QLabel(tr("Bookmarked Lines:"));
    bookmarkLayout->addWidget(bookmarkLabel);
    bookmarkLayout->addWidget(m_bookmarkList);
    bookmarkWidget->setLayout(bookmarkLayout);
    
    // Setup note editor
    QWidget* noteWidget = new QWidget();
    QVBoxLayout* noteLayout = new QVBoxLayout(noteWidget);
    QLabel* noteLabel = new QLabel(tr("Notes:"));
    noteLayout->addWidget(noteLabel);
    noteLayout->addWidget(m_noteEdit);
    noteWidget->setLayout(noteLayout);
    
    // Add widgets to splitter
    splitter->addWidget(bookmarkWidget);
    splitter->addWidget(noteWidget);
    
    // Add splitter to main layout
    mainLayout->addWidget(splitter);
    
    // Set minimum sizes for better usability
    m_widget->setMinimumSize(250, 300);
    
    // Connect signals
    connect(m_bookmarkList, &QListWidget::itemSelectionChanged,
            this, &BookmarkViewer::onBookmarkSelectionChanged);
    connect(m_noteEdit, &QTextEdit::textChanged,
            this, &BookmarkViewer::onNoteTextChanged);

    // Initially disable note editor
    m_noteEdit->setEnabled(false);
}

BookmarkViewer::~BookmarkViewer()
{
    delete m_widget;
}

bool BookmarkViewer::loadContent(const QVector<LogEntry>& content)
{
    m_entries = content;
    return true;
}

void BookmarkViewer::applyFilter(const FilterOptions& options)
{
    // Not needed for this plugin
    Q_UNUSED(options);
}

void BookmarkViewer::onPluginEvent(PluginEvent event, const QVariant& data)
{
    if (event != PluginEvent::LinesSelected || !data.isValid()) {
        return;
    }

    bool ok;
    QList<int> selectedLines;
    
    // Safely convert QVariant to QList<int>
    if (data.canConvert<QList<int>>()) {
        selectedLines = data.value<QList<int>>();
    } else if (data.canConvert<int>()) {
        // Handle single integer case
        int line = data.toInt(&ok);
        if (ok) {
            selectedLines.append(line);
        }
    }

    if (selectedLines.isEmpty()) {
        return;
    }

    int lineNumber = selectedLines.first();
    if (lineNumber < 0 || lineNumber >= m_entries.size()) {
        return;
    }

    // If this line is already bookmarked, just select it
    for (int i = 0; i < m_bookmarkList->count(); ++i) {
        QListWidgetItem* item = m_bookmarkList->item(i);
        if (item && item->data(Qt::UserRole).toInt() == lineNumber) {
            m_bookmarkList->setCurrentRow(i);
            m_currentBookmark = lineNumber;
            m_noteEdit->setEnabled(true);
            m_noteEdit->setText(m_bookmarkNotes.value(lineNumber));
            return;
        }
    }

    // If there's a pending bookmark without notes, remove it
    if (m_pendingBookmark >= 0) {
        for (int i = m_bookmarkList->count() - 1; i >= 0; --i) {
            QListWidgetItem* item = m_bookmarkList->item(i);
            if (item && item->data(Qt::UserRole).toInt() == m_pendingBookmark) {
                m_bookmarkList->takeItem(i);
                break;
            }
        }
    }
    
    // Set new pending bookmark
    m_pendingBookmark = lineNumber;
    m_currentBookmark = lineNumber;

    // Create new bookmark item
    const LogEntry& entry = m_entries[lineNumber];
    QString preview = getLinePreview(entry.getMessage());
    QString displayText = QString("Line %1: %2").arg(lineNumber + 1).arg(preview);
    
    QListWidgetItem* item = new QListWidgetItem(displayText);
    item->setData(Qt::UserRole, lineNumber);
    m_bookmarkList->addItem(item);
    m_bookmarkList->setCurrentItem(item);

    // Enable and clear note editor
    m_noteEdit->setEnabled(true);
    m_noteEdit->clear();
}

void BookmarkViewer::onBookmarkSelectionChanged()
{
    qDebug() << "onBookmarkSelectionChanged called";
    QListWidgetItem* item = m_bookmarkList->currentItem();
    if (!item) {
        qDebug() << "No current item";
        m_noteEdit->clear();
        m_noteEdit->setEnabled(false);
        m_currentBookmark = -1;
        return;
    }

    // Extract line number from item data
    m_currentBookmark = item->data(Qt::UserRole).toInt();
    qDebug() << "Current bookmark set to:" << m_currentBookmark;
    m_noteEdit->setEnabled(true);
    qDebug() << "Note editor enabled";
    m_noteEdit->setText(m_bookmarkNotes.value(m_currentBookmark));
    qDebug() << "Note text set";
}

void BookmarkViewer::onNoteTextChanged()
{
    if (m_currentBookmark < 0) {
        return;
    }

    QString note = m_noteEdit->toPlainText();
    
    if (m_currentBookmark == m_pendingBookmark) {
        if (note.isEmpty()) {
            // Schedule removal for next event loop iteration
            QTimer::singleShot(0, this, [this]() {
                if (m_currentBookmark >= 0 && m_currentBookmark == m_pendingBookmark) {
                    for (int i = 0; i < m_bookmarkList->count(); ++i) {
                        QListWidgetItem* item = m_bookmarkList->item(i);
                        if (item && item->data(Qt::UserRole).toInt() == m_currentBookmark) {
                            m_bookmarkList->setCurrentItem(nullptr);
                            m_bookmarkList->takeItem(i);
                            m_currentBookmark = -1;
                            m_pendingBookmark = -1;
                            m_noteEdit->setEnabled(false);
                            break;
                        }
                    }
                }
            });
        } else {
            // Note added, make bookmark permanent
            m_bookmarkNotes[m_currentBookmark] = note;
            m_pendingBookmark = -1;
        }
    } else {
        // Update existing bookmark's note
        if (note.isEmpty()) {
            m_bookmarkNotes.remove(m_currentBookmark);
        } else {
            m_bookmarkNotes[m_currentBookmark] = note;
        }
    }
}

void BookmarkViewer::updateBookmarkDisplay(int lineNumber)
{
    qDebug() << "updateBookmarkDisplay called with line:" << lineNumber;
    if (lineNumber < 0 || lineNumber >= m_entries.size()) {
        qDebug() << "Invalid line number";
        return;
    }

    // Check if this line is already bookmarked
    for (int i = 0; i < m_bookmarkList->count(); ++i) {
        QListWidgetItem* item = m_bookmarkList->item(i);
        if (item && item->data(Qt::UserRole).toInt() == lineNumber) {
            qDebug() << "Line already bookmarked, selecting it";
            m_bookmarkList->setCurrentRow(i);
            return;
        }
    }

    qDebug() << "Creating new bookmark";
    // If not already bookmarked, add it
    const LogEntry& entry = m_entries[lineNumber];
    QString preview = getLinePreview(entry.getMessage());
    QString displayText = QString("Line %1: %2").arg(lineNumber + 1).arg(preview);
    
    // Create and configure item before adding to list
    QListWidgetItem* item = new QListWidgetItem();
    item->setText(displayText);
    item->setData(Qt::UserRole, lineNumber);
    
    // Add item to list and set as current
    m_bookmarkList->addItem(item);
    qDebug() << "Item added to list";
    m_currentBookmark = lineNumber;
    qDebug() << "Current bookmark set to:" << lineNumber;
    m_noteEdit->setEnabled(true);
    qDebug() << "Note editor enabled";
    m_noteEdit->clear();
    qDebug() << "Note editor cleared";
    m_bookmarkList->setCurrentItem(item);
    qDebug() << "Current item set";
}

QString BookmarkViewer::getLinePreview(const QString& line, int maxLength) const
{
    if (line.length() <= maxLength) {
        return line;
    }
    return line.left(maxLength - 3) + "...";
}
