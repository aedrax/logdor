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
    QString version() const override { return "0.1"; }
    QString description() const override { return tr("View and manage bookmarks in the log."); }
    QWidget* widget() override { return m_widget; }
    bool setLogs(const QList<LogEntry>& content) override;
    void setFilter(const FilterOptions& options) override;
    QList<FieldInfo> availableFields() const override;
    QSet<int> filteredLines() const override;
    void synchronizeFilteredLines(const QSet<int>& lines) override;

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
    QList<LogEntry> m_entries;
    QMap<int, QString> m_bookmarkNotes;  // Maps line numbers to notes
    QSet<int> m_checkedBookmarks;  // Set of checked bookmark line numbers
    int m_currentBookmark;  // Currently selected bookmark line number
};

#endif // BOOKMARKVIEWER_H
