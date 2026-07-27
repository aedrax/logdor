#ifndef CUSTOMFORMATVIEWER_H
#define CUSTOMFORMATVIEWER_H

#include "../../app/src/logviewerwidget.h"
#include "../../app/src/plugininterface.h"

#include <logdor/FormatSpec.h>

#include <QLabel>
#include <QLineEdit>
#include <QTimer>
#include <QWidget>
#include <QtPlugin>

/**
 * Interactive format authoring: type a regex with named captures and watch
 * the table re-shape live - each capture becomes a column, `message`/`msg`
 * becomes the stretch column, `level`/`severity` gets the standard severity
 * color mapping. "Save as Format..." writes the spec to the user formats
 * directory, making it permanent, shareable, and auto-detectable.
 * Replaces the old Regex Viewer.
 */
class CustomFormatViewer : public PluginInterface {
    Q_OBJECT
    Q_INTERFACES(PluginInterface)
    Q_PLUGIN_METADATA(IID PluginInterface_iid)
public:
    explicit CustomFormatViewer(QObject* parent = nullptr);
    ~CustomFormatViewer();

    QString name() const override { return tr("Custom Format Viewer"); }
    QString version() const override { return "v1.0.0"; }
    QString description() const override { return tr("Author log formats interactively with a live regex."); }
    QWidget* widget() override { return m_container; }

    void setCoreSource(std::shared_ptr<logdor::FileSource> source,
                       std::shared_ptr<const logdor::LineIndex> index) override;

    void setAnnotationHub(AnnotationHub* hub) override
    {
        m_viewer->setAnnotationHub(hub);
    }

    void setFilter(const FilterOptions& options) override;

    void setHighlightRules(const QList<HighlightRule>& rules) override
    {
        m_viewer->setHighlightRules(rules);
    }

    // The regex pattern itself stays global (QSettings) - only table view
    // state is per-file.
    QJsonObject saveViewState() const override
    {
        return m_viewer->saveViewState();
    }
    void restoreViewState(const QJsonObject& state) override
    {
        m_viewer->restoreViewState(state);
    }

public slots:
    void onPluginEvent(PluginEvent event, const QVariant& data) override;

private slots:
    void rebuildParser();
    void saveAsFormat();

private:
    std::optional<logdor::FormatSpec> buildSpec(const QString& id,
                                                const QString& displayName) const;

    QWidget* m_container;
    LogViewerWidget* m_viewer;
    QLineEdit* m_patternEdit;
    QLabel* m_status;
    QTimer* m_debounce;
    FilterOptions m_lastFilter;
};

#endif // CUSTOMFORMATVIEWER_H
