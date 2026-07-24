#ifndef LOGCATVIEWER_H
#define LOGCATVIEWER_H

#include "../../app/src/logviewerwidget.h"
#include "../../app/src/plugininterface.h"

#include <logdor/FormatParser.h>

#include <QAction>
#include <QComboBox>
#include <QFrame>
#include <QFutureWatcher>
#include <QHBoxLayout>
#include <QMap>
#include <QScrollArea>
#include <QSet>
#include <QToolBar>
#include <QVBoxLayout>
#include <QWidget>
#include <QtPlugin>

#include <array>

class LogcatViewer : public PluginInterface {
    Q_OBJECT
    Q_INTERFACES(PluginInterface)
    Q_PLUGIN_METADATA(IID PluginInterface_iid)
public:
    explicit LogcatViewer(QObject* parent = nullptr);
    ~LogcatViewer();

    QString name() const override { return tr("Logcat Viewer"); }
    QString version() const override { return "0.2.0"; }
    QString description() const override { return tr("A viewer for Android logcat logs with filtering and tag selection."); }
    QWidget* widget() override { return m_container; }

    void setCoreSource(std::shared_ptr<logdor::FileSource> source,
                       std::shared_ptr<const logdor::LineIndex> index) override;

    void setAnnotationHub(AnnotationHub* hub) override
    {
        m_viewer->setAnnotationHub(hub);
    }

    void setFilter(const FilterOptions& options) override;

public slots:
    void onPluginEvent(PluginEvent event, const QVariant& data) override;

private:
    void setupUi();
    void addTagLabel(const QString& tag);
    // Rebuilds the level/tag predicate and re-runs the filter scan.
    void updatePredicate();
    void startTagSuggestionScan();

    QWidget* m_container;
    QVBoxLayout* m_layout;
    QToolBar* m_toolbar;
    LogViewerWidget* m_viewer;
    std::shared_ptr<const logdor::FormatParser> m_parser;

    // Severity index 0..6 == logdor::Severity None..Fatal ("Unknown" is None).
    std::array<bool, 7> m_levelEnabled;
    QMap<int, QAction*> m_levelActions; // key = severity index
    QComboBox* m_tagComboBox;
    QSet<QString> m_selectedTags;
    QScrollArea* m_scrollArea;
    QFrame* m_tagsContainer;
    QHBoxLayout* m_tagsLayout;

    std::shared_ptr<logdor::FileSource> m_source;
    std::shared_ptr<const logdor::LineIndex> m_index;
    QFutureWatcher<QStringList> m_tagScanWatcher;
};

#endif // LOGCATVIEWER_H
