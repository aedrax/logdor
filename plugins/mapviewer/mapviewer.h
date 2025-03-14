#ifndef MAPVIEWER_H
#define MAPVIEWER_H

#include "../../app/src/plugininterface.h"
#include "maptablemodel.h"
#include <QtWebEngineWidgets/QWebEngineView>
#include <QtWidgets/QSplitter>
#include <QtWidgets/QTableView>
#include <QtWidgets/QAbstractItemView>
#include <QtWebEngineCore/QWebEnginePage>

class MapViewer : public PluginInterface {
    Q_OBJECT
    Q_INTERFACES(PluginInterface)
    Q_PLUGIN_METADATA(IID PluginInterface_iid)
public:
    explicit MapViewer(QObject* parent = nullptr);
    ~MapViewer();

    // PluginInterface implementation
    QString name() const override { return tr("Map Viewer"); }
    QString version() const override { return "0.1.0"; }
    QString description() const override { 
        return tr("A viewer that parses coordinates from logs and displays them on a map."); 
    }
    QWidget* widget() override { return m_splitter; }
    bool setLogs(const QList<LogEntry>& content) override;
    void setFilter(const FilterOptions& options) override;
    QList<FieldInfo> availableFields() const override;
    QSet<int> filteredLines() const override;
    void synchronizeFilteredLines(const QSet<int>& lines) override;

public slots:
    void onPluginEvent(PluginEvent event, const QVariant& data) override;

private slots:
    void onSelectionChanged(const QItemSelection& selected, const QItemSelection& deselected);
    void updateMap();

private:
    void initializeMap();
    void loadMapHtml();
    QString generateMapHtml() const;
    void centerMapOnCoordinate(const QGeoCoordinate& coord);
    void addMarkerToMap(const QGeoCoordinate& coord, const QString& message);
    void clearMarkers();

    QSplitter* m_splitter;
    QTableView* m_tableView;
    QWebEngineView* m_mapView;
    MapTableModel* m_model;
};

#endif // MAPVIEWER_H
