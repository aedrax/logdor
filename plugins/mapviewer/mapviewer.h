#ifndef MAPVIEWER_H
#define MAPVIEWER_H

#include "../../app/src/plugininterface.h"
#include "maptablemodel.h"

#include <logdor/FilterScan.h>
#include <logdor/GeoScan.h>

#include <QFutureWatcher>
#include <QLabel>
#include <QPushButton>
#include <QSplitter>
#include <QTableView>
#include <QWebEngineView>
#include <QtPlugin>

/**
 * Plots location-type log events on a map: a core scanCoordinates pass
 * finds every line carrying a lat/lon (decimal, labeled, or DMS), the text
 * filter narrows the set via the core filter scan, and a Leaflet/OSM map
 * shows up to 5,000 markers. Marker or row clicks broadcast LinesSelected;
 * incoming selections pan the map.
 */
class MapViewer : public PluginInterface {
    Q_OBJECT
    Q_INTERFACES(PluginInterface)
    Q_PLUGIN_METADATA(IID PluginInterface_iid)
public:
    explicit MapViewer(QObject* parent = nullptr);
    ~MapViewer();

    QString name() const override { return tr("Map Viewer"); }
    QString version() const override { return "0.2.0"; }
    QString description() const override
    {
        return tr("Extracts coordinates from logs and plots them on a map.");
    }
    QWidget* widget() override { return m_splitter; }

    void setCoreSource(std::shared_ptr<logdor::FileSource> source,
                       std::shared_ptr<const logdor::LineIndex> index) override;
    void setFilter(const FilterOptions& options) override;

public slots:
    void onPluginEvent(PluginEvent event, const QVariant& data) override;

private slots:
    void onGeoScanFinished();
    void onFilterScanFinished();
    void onMapLoadFinished(bool ok);
    void onMapUrlChanged(const QUrl& url);
    void onSelectAreaToggled(bool on);
    void clearArea();

private:
    void applyVisiblePoints();
    void applyAreaBounds(double south, double west, double north, double east);
    void broadcastAreaConstraint();
    void pushMarkers();
    void runMapJs(const QString& js);

    QSplitter* m_splitter;
    QTableView* m_tableView;
    QLabel* m_status;
    QPushButton* m_selectAreaButton;
    QPushButton* m_clearAreaButton;
    QWebEngineView* m_mapView;
    MapTableModel* m_model;

    // Active area (inclusive bounding box); no area when !m_hasArea.
    bool m_hasArea = false;
    double m_areaSouth = 0, m_areaWest = 0, m_areaNorth = 0, m_areaEast = 0;

    std::shared_ptr<logdor::FileSource> m_source;
    std::shared_ptr<const logdor::LineIndex> m_index;
    QFutureWatcher<logdor::GeoScanResult> m_geoWatcher;
    QFutureWatcher<logdor::FilterScanResult> m_filterWatcher;

    std::vector<logdor::GeoPoint> m_allPoints; // every hit in the file
    logdor::RowSet m_visibleRows;              // current filter result
    bool m_hasFilterResult = false;
    FilterOptions m_lastFilter;

    bool m_mapReady = false;
    QString m_pendingMarkerJs; // latest payload, flushed on loadFinished
    bool m_syncing = false;    // echo guard for table selection
};

#endif // MAPVIEWER_H
