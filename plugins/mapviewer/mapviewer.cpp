#include "mapviewer.h"
#include <QtCore/QJsonArray>
#include <QtCore/QJsonDocument>
#include <QtCore/QJsonObject>
#include <QtWidgets/QVBoxLayout>
#include <QtWidgets/QHeaderView>

MapViewer::MapViewer(QObject* parent)
    : PluginInterface(parent)
    , m_splitter(new QSplitter(Qt::Vertical))
    , m_tableView(new QTableView)
    , m_mapView(new QWebEngineView)
    , m_model(new MapTableModel(this))
{
    m_tableView->setModel(m_model);
    m_tableView->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_tableView->setSelectionMode(QAbstractItemView::ExtendedSelection);
    m_tableView->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Stretch);
    
    connect(m_tableView->selectionModel(), &QItemSelectionModel::selectionChanged,
            this, &MapViewer::onSelectionChanged);

    m_splitter->addWidget(m_tableView);
    m_splitter->addWidget(m_mapView);
    m_splitter->setStretchFactor(0, 1);
    m_splitter->setStretchFactor(1, 2);

    initializeMap();
}

MapViewer::~MapViewer()
{
    delete m_splitter;
}

bool MapViewer::setLogs(const QList<LogEntry>& content)
{
    m_model->setLogs(content);
    updateMap();
    return true;
}

void MapViewer::setFilter(const FilterOptions& options)
{
    m_model->setFilter(options);
}

QList<FieldInfo> MapViewer::availableFields() const
{
    return QList<FieldInfo>{
        {tr("Message"), DataType::String},
        {tr("Latitude"), DataType::String},
        {tr("Longitude"), DataType::String}
    };
}

QSet<int> MapViewer::filteredLines() const
{
    return m_model->filteredLines();
}

void MapViewer::synchronizeFilteredLines(const QSet<int>& lines)
{
    m_model->synchronizeFilteredLines(lines);
}

void MapViewer::onPluginEvent(PluginEvent event, const QVariant& data)
{
    if (event == PluginEvent::LinesSelected) {
        QList<int> selectedLines = data.value<QList<int>>();
        if (selectedLines.isEmpty()) {
            m_tableView->clearSelection();
            return;
        }

        // Block signals to prevent recursion
        QSignalBlocker blocker(m_tableView->selectionModel());

        // Clear current selection
        m_tableView->clearSelection();

        // Create selection ranges for matching rows
        QItemSelection selection;
        for (int row = 0; row < m_model->rowCount(); ++row) {
            int sourceRow = m_model->mapToSourceRow(row);
            if (selectedLines.contains(sourceRow)) {
                QModelIndex idx = m_model->index(row, 0);
                selection.select(idx, idx);
            }
        }

        // Apply selection
        m_tableView->selectionModel()->select(selection, QItemSelectionModel::Select | QItemSelectionModel::Rows);

        // Clear existing markers
        clearMarkers();

        // Add markers for all selected rows and scroll to first one
        bool firstValid = true;
        QJsonArray markers;
        for (int row = 0; row < m_model->rowCount(); ++row) {
            QModelIndex idx = m_model->index(row, 0);
            if (m_tableView->selectionModel()->isSelected(idx)) {
                // Scroll to first selected row
                if (firstValid) {
                    m_tableView->scrollTo(idx);
                    firstValid = false;
                }
                
                // Add marker if coordinates are valid
                QGeoCoordinate coord = m_model->getCoordinate(row);
                if (coord.isValid()) {
                    QJsonObject marker;
                    marker["lat"] = coord.latitude();
                    marker["lon"] = coord.longitude();
                    marker["message"] = m_model->getMessage(row);
                    markers.append(marker);
                }
            }
        }

        // Update map with markers
        if (!markers.isEmpty()) {
            QJsonDocument doc(markers);
            QString jsonString = doc.toJson(QJsonDocument::Compact);
            QString js = QString("clearMarkers(); var markers = %1; "
                           "markers.forEach(function(m) { addMarker(m.lat, m.lon, m.message); });"
                           "if (markers.length > 0) { centerMap(markers[0].lat, markers[0].lon, markers.length === 1 ? 12 : undefined); }")
                        .arg(jsonString);
            m_mapView->page()->runJavaScript(js);
        }
    }
}

void MapViewer::onSelectionChanged(const QItemSelection& selected, const QItemSelection& deselected)
{
    Q_UNUSED(deselected);
    
    // Get all selected rows
    QList<int> selectedLines;
    const auto indexes = m_tableView->selectionModel()->selectedRows();

    // Clear existing markers and prepare new ones
    clearMarkers();
    QJsonArray markers;

    // Process all selected rows
    bool firstValid = true;
    for (const auto& index : indexes) {
        selectedLines.append(m_model->mapToSourceRow(index.row()));

        // Scroll to first selected row
        if (firstValid) {
            m_tableView->scrollTo(index);
            firstValid = false;
        }

        // Add marker if coordinates are valid
        QGeoCoordinate coord = m_model->getCoordinate(index.row());
        if (coord.isValid()) {
            QJsonObject marker;
            marker["lat"] = coord.latitude();
            marker["lon"] = coord.longitude();
            marker["message"] = m_model->getMessage(index.row());
            markers.append(marker);
        }
    }

    // Update map with markers
    if (!markers.isEmpty()) {
        QJsonDocument doc(markers);
        QString jsonString = doc.toJson(QJsonDocument::Compact);
            QString js = QString("clearMarkers(); var markers = %1; "
                       "markers.forEach(function(m) { addMarker(m.lat, m.lon, m.message); });"
                       "if (markers.length > 0) { centerMap(markers[0].lat, markers[0].lon, markers.length === 1 ? 12 : undefined); }")
                    .arg(jsonString);
        m_mapView->page()->runJavaScript(js);
    }
    
    // Emit the plugin event with selected lines
    emit pluginEvent(PluginEvent::LinesSelected, QVariant::fromValue(selectedLines));
}

void MapViewer::initializeMap()
{
    loadMapHtml();
}

void MapViewer::loadMapHtml()
{
    m_mapView->setHtml(generateMapHtml(), QUrl("qrc:/"));
}

QString MapViewer::generateMapHtml() const
{
    return R"(
<!DOCTYPE html>
<html>
<head>
    <title>Log Coordinates Map</title>
    <link rel="stylesheet" href="https://unpkg.com/leaflet@1.7.1/dist/leaflet.css" />
    <script src="https://unpkg.com/leaflet@1.7.1/dist/leaflet.js"></script>
    <style>
        html, body {
            height: 100%;
            margin: 0;
            padding: 0;
        }
        #map {
            height: 100%;
            width: 100%;
        }
    </style>
</head>
<body>
    <div id="map"></div>
    <script>
        var map = L.map('map').setView([0, 0], 2);
        L.tileLayer('https://{s}.tile.openstreetmap.org/{z}/{x}/{y}.png', {
            attribution: '&copy; <a href="https://www.openstreetmap.org/copyright">OpenStreetMap</a> contributors'
        }).addTo(map);
        
        var markerGroup = L.featureGroup().addTo(map);
        
        function clearMarkers() {
            markerGroup.clearLayers();
        }
        
        function addMarker(lat, lon, message) {
            var marker = L.marker([lat, lon], {
                title: `${lat}, ${lon}`,
                riseOnHover: true,
                autoPanOnFocus: true
            });
            
            // Create a popup with formatted content
            var content = `
                <div style="max-width: 300px;">
                    <p style="margin: 0;"><strong>Coordinates:</strong></p>
                    <p style="margin: 0;">Lat: ${lat.toFixed(6)}°</p>
                    <p style="margin: 0;">Lon: ${lon.toFixed(6)}°</p>
                    <hr style="margin: 5px 0;">
                    <p style="margin: 0;"><strong>Log Entry:</strong></p>
                    <p style="margin: 0; white-space: pre-wrap;">${message}</p>
                </div>
            `;
            
            marker.bindPopup(content, {
                maxWidth: 350,
                autoPan: true
            });
            
            marker.addTo(markerGroup);
            marker.on('mouseover', function(e) {
                this.openPopup();
            });
        }
        
        function centerMap(lat, lon, zoom) {
            if (markerGroup.getLayers().length > 1) {
                // If we have multiple markers, fit bounds to show all
                map.fitBounds(markerGroup.getBounds(), {
                    padding: [50, 50], // Add some padding around the bounds
                    maxZoom: 12        // Don't zoom in too far
                });
            } else {
                // For single marker, center on it
                map.setView([lat, lon], zoom || 12);
            }
            // Find and highlight the marker at these coordinates
            markerGroup.eachLayer(function(marker) {
                var pos = marker.getLatLng();
                if (Math.abs(pos.lat - lat) < 0.000001 && Math.abs(pos.lng - lon) < 0.000001) {
                    marker.openPopup();
                }
            });
        }
    </script>
</body>
</html>
)";
}

void MapViewer::updateMap()
{
    clearMarkers();
    
    // Create a JSON array of markers
    QJsonArray markers;
    for (int i = 0; i < m_model->rowCount(); ++i) {
        QGeoCoordinate coord = m_model->getCoordinate(i);
        if (!coord.isValid()) {
            continue;
        }
        
        QJsonObject marker;
        marker["lat"] = coord.latitude();
        marker["lon"] = coord.longitude();
        marker["message"] = m_model->getMessage(i);
        markers.append(marker);
    }
    
    // Convert to JSON string
    QJsonDocument doc(markers);
    QString jsonString = doc.toJson(QJsonDocument::Compact);
    
    // Add markers to the map
    QString js = QString("clearMarkers(); var markers = %1; "
                       "markers.forEach(function(m) { addMarker(m.lat, m.lon, m.message); });"
                       "if (markers.length > 0) { centerMap(markers[0].lat, markers[0].lon, markers.length === 1 ? 12 : undefined); }")
                    .arg(jsonString);
    
    m_mapView->page()->runJavaScript(js);
}

void MapViewer::centerMapOnCoordinate(const QGeoCoordinate& coord)
{
    if (!coord.isValid()) {
        return;
    }
    
    QString js = QString("centerMap(%1, %2);")
                    .arg(coord.latitude())
                    .arg(coord.longitude());
    
    m_mapView->page()->runJavaScript(js);
}

void MapViewer::addMarkerToMap(const QGeoCoordinate& coord, const QString& message)
{
    if (!coord.isValid()) {
        return;
    }
    
    QString escapedMessage = message;
    escapedMessage.replace("'", "\\'");
    QString js = QString("addMarker(%1, %2, '%3');")
                    .arg(coord.latitude())
                    .arg(coord.longitude())
                    .arg(escapedMessage);
    
    m_mapView->page()->runJavaScript(js);
}

void MapViewer::clearMarkers()
{
    m_mapView->page()->runJavaScript("clearMarkers();");
}
