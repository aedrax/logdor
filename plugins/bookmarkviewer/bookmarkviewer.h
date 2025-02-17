#ifndef BOOKMARKVIEWER_H
#define BOOKMARKVIEWER_H

#include "../../app/src/plugininterface.h"
#include <QWidget>
#include <QString>
#include <QMap>
#include <QtPlugin>
#include <QListWidget>
#include <QTextEdit>

class BookmarkViewer : public PluginInterface {
    Q_OBJECT
    Q_INTERFACES(PluginInterface)
    Q_PLUGIN_METADATA(IID PluginInterface_iid)
public:
    explicit BookmarkViewer(QObject* parent = nullptr);
    ~BookmarkViewer();

    // PluginInterface implementation
    QString name() const override { return tr("Bookmark Viewer"); }
    QWidget* widget() override { return m_widget; }
    bool loadContent(const QVector<LogEntry>& content) override;
    void applyFilter(const FilterOptions& options) override;

public slots:
    void onPluginEvent(PluginEvent event, const QVariant& data) override;
    void onBookmarkSelectionChanged();
    void onNoteTextChanged();

private:
    void updateBookmarkDisplay(int lineNumber);
    QString getLinePreview(const QString& line, int maxLength = 50) const;

    QWidget* m_widget;
    QListWidget* m_bookmarkList;
    QTextEdit* m_noteEdit;
    QVector<LogEntry> m_entries;
    QMap<int, QString> m_bookmarkNotes;  // Maps line numbers to notes
    int m_currentBookmark;  // Currently selected bookmark line number
    int m_pendingBookmark;  // Line number of bookmark waiting for a note
};

#endif // BOOKMARKVIEWER_H
