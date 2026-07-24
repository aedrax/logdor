#include "mapviewer.h"

#include <QHBoxLayout>
#include <QHeaderView>
#include <QItemSelectionModel>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QVBoxLayout>

namespace {

constexpr int kMaxMarkers = 5000;

const char* kMapHtml = R"HTML(<!DOCTYPE html>
<html>
<head>
    <meta charset="utf-8">
    <link rel="stylesheet" href="https://unpkg.com/leaflet@1.9.4/dist/leaflet.css" />
    <script src="https://unpkg.com/leaflet@1.9.4/dist/leaflet.js"></script>
    <style>
        html, body, #map { height: 100%; margin: 0; }
        #offline { display: none; position: absolute; inset: 0; z-index: 1000;
                   align-items: center; justify-content: center;
                   font-family: sans-serif; color: #666; background: #f4f4f4; }
    </style>
</head>
<body>
<div id="map"></div>
<div id="offline">Map tiles require network access.</div>
<script>
    var map = null;
    var markerLayer = null;

    // Defined unconditionally so native calls can never hit undefined
    // functions (the old plugin's 'clearMarkers is not defined' noise).
    function clearMarkers() {
        if (markerLayer) markerLayer.clearLayers();
    }
    function setMarkers(points) {
        if (!map) return;
        clearMarkers();
        if (!points.length) return;
        var renderer = L.canvas();
        var bounds = [];
        for (var i = 0; i < points.length; i++) {
            var p = points[i];
            var m = L.circleMarker([p.lat, p.lon], {
                renderer: renderer, radius: 5, weight: 1,
                color: '#1d5fa8', fillColor: '#4a90d9', fillOpacity: 0.8
            }).addTo(markerLayer);
            m.bindPopup('Line ' + (p.line + 1) + '<br>' + p.msg);
            (function(line) {
                m.on('click', function() {
                    // Native side watches urlChanged and parses the hash.
                    location.hash = 'line-' + line + '-' + Date.now();
                });
            })(p.line);
            bounds.push([p.lat, p.lon]);
        }
        if (bounds.length) map.fitBounds(bounds, { padding: [30, 30], maxZoom: 12 });
    }
    function panToPoint(lat, lon) {
        if (map) map.panTo([lat, lon]);
    }

    // --- Area selection: drag a rectangle, report bounds via the hash. ---
    var selecting = false;
    var selStart = null;
    var dragRect = null;   // rubber band while dragging
    var areaRect = null;   // committed area

    function setSelectMode(on) {
        if (!map) return;
        selecting = on;
        map.dragging[on ? 'disable' : 'enable']();
        map.getContainer().style.cursor = on ? 'crosshair' : '';
    }
    function clearArea() {
        if (areaRect) { map.removeLayer(areaRect); areaRect = null; }
    }
    function bindAreaHandlers() {
        map.on('mousedown', function(e) {
            if (!selecting) return;
            selStart = e.latlng;
            if (dragRect) { map.removeLayer(dragRect); dragRect = null; }
        });
        map.on('mousemove', function(e) {
            if (!selecting || !selStart) return;
            var bounds = L.latLngBounds(selStart, e.latlng);
            if (!dragRect)
                dragRect = L.rectangle(bounds, { color: '#cc4444', weight: 1,
                                                 fillOpacity: 0.08 }).addTo(map);
            else
                dragRect.setBounds(bounds);
        });
        map.on('mouseup', function(e) {
            if (!selecting || !selStart) return;
            var bounds = L.latLngBounds(selStart, e.latlng);
            selStart = null;
            if (dragRect) { map.removeLayer(dragRect); dragRect = null; }
            setSelectMode(false);
            clearArea();
            areaRect = L.rectangle(bounds, { color: '#cc4444', weight: 2,
                                             fillOpacity: 0.05 }).addTo(map);
            // Commas are safe separators for negative coordinates.
            location.hash = 'area=' + bounds.getSouth() + ',' + bounds.getWest()
                + ',' + bounds.getNorth() + ',' + bounds.getEast()
                + '&t=' + Date.now();
        });
    }

    if (typeof L !== 'undefined') {
        map = L.map('map').setView([20, 0], 2);
        L.tileLayer('https://{s}.tile.openstreetmap.org/{z}/{x}/{y}.png', {
            attribution: '&copy; <a href="https://www.openstreetmap.org/copyright">OpenStreetMap</a> contributors'
        }).addTo(map);
        markerLayer = L.layerGroup().addTo(map);
        bindAreaHandlers();
    } else {
        document.getElementById('offline').style.display = 'flex';
    }
</script>
</body>
</html>)HTML";

} // namespace

MapViewer::MapViewer(QObject* parent)
    : PluginInterface(parent)
    , m_splitter(new QSplitter(Qt::Horizontal))
    , m_tableView(new QTableView())
    , m_status(new QLabel())
    , m_mapView(new QWebEngineView())
    , m_model(new MapTableModel(this))
{
    auto* left = new QWidget();
    auto* leftLayout = new QVBoxLayout(left);
    leftLayout->setContentsMargins(0, 0, 0, 0);
    leftLayout->setSpacing(2);

    auto* toolbar = new QWidget();
    auto* toolbarLayout = new QHBoxLayout(toolbar);
    toolbarLayout->setContentsMargins(4, 2, 4, 2);
    m_selectAreaButton = new QPushButton(tr("Select Area"));
    m_selectAreaButton->setCheckable(true);
    m_selectAreaButton->setToolTip(
        tr("Drag a rectangle on the map; all viewers filter to lines whose "
           "coordinates fall inside it"));
    m_clearAreaButton = new QPushButton(tr("Clear Area"));
    m_clearAreaButton->setEnabled(false);
    toolbarLayout->addWidget(m_selectAreaButton);
    toolbarLayout->addWidget(m_clearAreaButton);
    toolbarLayout->addStretch();
    leftLayout->addWidget(toolbar);

    m_status->setContentsMargins(4, 2, 4, 2);
    m_status->setText(tr("No location events"));
    leftLayout->addWidget(m_status);
    leftLayout->addWidget(m_tableView);

    m_tableView->setModel(m_model);
    m_tableView->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_tableView->setSelectionMode(QAbstractItemView::ExtendedSelection);
    m_tableView->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_tableView->verticalHeader()->setVisible(false);
    m_tableView->horizontalHeader()->setSectionResizeMode(
        MapTableModel::Message, QHeaderView::Stretch);

    m_splitter->addWidget(left);
    m_splitter->addWidget(m_mapView);
    m_splitter->setStretchFactor(0, 2);
    m_splitter->setStretchFactor(1, 3);

    connect(m_tableView->selectionModel(), &QItemSelectionModel::selectionChanged,
            this, [this]() {
                if (m_syncing)
                    return;
                QList<int> lines;
                const auto rows = m_tableView->selectionModel()->selectedRows();
                for (const QModelIndex& index : rows)
                    lines.append(m_model->points()[index.row()].line);
                std::sort(lines.begin(), lines.end());
                if (!lines.isEmpty()) {
                    const logdor::GeoPoint& p =
                        m_model->points()[rows.first().row()];
                    runMapJs(QStringLiteral("panToPoint(%1, %2);")
                                 .arg(p.latitude)
                                 .arg(p.longitude));
                }
                emit pluginEvent(PluginEvent::LinesSelected,
                                 QVariant::fromValue(lines));
            });

    connect(m_selectAreaButton, &QPushButton::toggled,
            this, &MapViewer::onSelectAreaToggled);
    connect(m_clearAreaButton, &QPushButton::clicked,
            this, &MapViewer::clearArea);
    connect(m_mapView, &QWebEngineView::loadFinished,
            this, &MapViewer::onMapLoadFinished);
    connect(m_mapView, &QWebEngineView::urlChanged,
            this, &MapViewer::onMapUrlChanged);
    connect(&m_geoWatcher, &QFutureWatcherBase::finished,
            this, &MapViewer::onGeoScanFinished);
    connect(&m_filterWatcher, &QFutureWatcherBase::finished,
            this, &MapViewer::onFilterScanFinished);

    m_mapView->setHtml(QString::fromUtf8(kMapHtml),
                       QUrl(QStringLiteral("https://logdor.local/map.html")));
}

MapViewer::~MapViewer()
{
    m_geoWatcher.cancel();
    m_filterWatcher.cancel();
    delete m_splitter;
}

void MapViewer::setCoreSource(std::shared_ptr<logdor::FileSource> source,
                              std::shared_ptr<const logdor::LineIndex> index)
{
    m_geoWatcher.cancel();
    m_filterWatcher.cancel();
    m_source = std::move(source);
    m_index = std::move(index);
    m_allPoints.clear();
    m_visibleRows = {};
    m_hasFilterResult = false;
    // Areas don't survive a file switch (viewers clear their constraint on
    // setCoreSource themselves, so no broadcast is needed).
    m_hasArea = false;
    m_selectAreaButton->setChecked(false);
    m_clearAreaButton->setEnabled(false);
    runMapJs(QStringLiteral("clearArea(); setSelectMode(false);"));
    m_model->setSource(m_source, m_index);
    m_status->setText(tr("No location events"));
    pushMarkers();

    if (m_source && m_index) {
        m_status->setText(tr("Scanning for coordinates..."));
        m_geoWatcher.setFuture(logdor::scanCoordinates(m_source, m_index));
    }
}

void MapViewer::setFilter(const FilterOptions& options)
{
    m_lastFilter = options;
    if (!m_source || !m_index)
        return;

    // Query mode has no schema here; show all coordinates and say so.
    if (options.inQueryMode || options.query.isEmpty()) {
        m_hasFilterResult = false;
        m_filterWatcher.cancel();
        applyVisiblePoints();
        return;
    }

    logdor::LineFilter filter;
    filter.query = options.query;
    filter.caseSensitive = options.caseSensitivity == Qt::CaseSensitive;
    filter.regexMode = options.inRegexMode;
    filter.invert = options.invertFilter;
    filter.contextBefore = options.contextLinesBefore;
    filter.contextAfter = options.contextLinesAfter;

    m_filterWatcher.cancel();
    m_filterWatcher.setFuture(
        logdor::scanFilter(m_source, m_index, std::move(filter)));
}

void MapViewer::onGeoScanFinished()
{
    if (m_geoWatcher.future().isCanceled())
        return;
    m_allPoints = m_geoWatcher.future().result().points;
    applyVisiblePoints();
    // An area drawn while the scan ran was computed against no points.
    if (m_hasArea)
        broadcastAreaConstraint();
    // A filter may already be active; refresh against it.
    if (!m_lastFilter.query.isEmpty() && !m_lastFilter.inQueryMode)
        setFilter(m_lastFilter);
}

void MapViewer::onFilterScanFinished()
{
    if (m_filterWatcher.future().isCanceled())
        return;
    m_visibleRows = m_filterWatcher.future().result().rows;
    m_hasFilterResult = true;
    applyVisiblePoints();
}

void MapViewer::onSelectAreaToggled(bool on)
{
    runMapJs(QStringLiteral("setSelectMode(%1);").arg(on ? u"true" : u"false"));
}

void MapViewer::clearArea()
{
    if (!m_hasArea)
        return;
    m_hasArea = false;
    m_clearAreaButton->setEnabled(false);
    runMapJs(QStringLiteral("clearArea();"));
    applyVisiblePoints();
    broadcastAreaConstraint(); // empty payload lifts the restriction
}

void MapViewer::applyAreaBounds(double south, double west, double north,
                                double east)
{
    m_hasArea = true;
    m_areaSouth = south;
    m_areaWest = west;
    m_areaNorth = north;
    m_areaEast = east;
    m_clearAreaButton->setEnabled(true);
    m_selectAreaButton->setChecked(false); // JS already left select mode
    applyVisiblePoints();
    broadcastAreaConstraint();
}

void MapViewer::broadcastAreaConstraint()
{
    QList<int> lines;
    if (m_hasArea) {
        for (const logdor::GeoPoint& point : m_allPoints) {
            if (point.latitude >= m_areaSouth && point.latitude <= m_areaNorth
                && point.longitude >= m_areaWest
                && point.longitude <= m_areaEast)
                lines.append(point.line); // m_allPoints is line-ascending
        }
        // An empty payload means "lift the restriction", but an area with no
        // events must show NOTHING - send an impossible line instead.
        if (lines.isEmpty())
            lines.append(-1);
    }
    emit pluginEvent(PluginEvent::LinesConstrained,
                     QVariant::fromValue(lines));
}

void MapViewer::applyVisiblePoints()
{
    QList<logdor::GeoPoint> visible;
    for (const logdor::GeoPoint& point : m_allPoints) {
        if (m_hasFilterResult && m_visibleRows.rowForSourceLine(point.line) < 0)
            continue;
        if (m_hasArea
            && !(point.latitude >= m_areaSouth && point.latitude <= m_areaNorth
                 && point.longitude >= m_areaWest
                 && point.longitude <= m_areaEast))
            continue;
        visible.append(point);
    }

    m_model->setPoints(visible);
    m_tableView->resizeColumnToContents(MapTableModel::Line);

    const qsizetype total = visible.size();
    QString status = total == 0
        ? tr("No location events")
        : tr("%n location event(s)", nullptr, int(total));
    if (total > kMaxMarkers)
        status += tr(" - plotting the first %1").arg(kMaxMarkers);
    if (m_hasArea)
        status += tr(" - area active, all viewers filtered");
    if (m_lastFilter.inQueryMode && !m_lastFilter.query.isEmpty())
        status += tr(" (field queries are not applied to the map)");
    m_status->setText(status);

    pushMarkers();
}

void MapViewer::pushMarkers()
{
    QJsonArray markers;
    const auto& points = m_model->points();
    for (qsizetype i = 0; i < points.size() && i < kMaxMarkers; ++i) {
        QJsonObject marker;
        marker.insert(u"line", points[i].line);
        marker.insert(u"lat", points[i].latitude);
        marker.insert(u"lon", points[i].longitude);
        QString message;
        if (m_source && m_index)
            message = QString::fromUtf8(
                m_source->read(m_index->offsetOf(points[i].line),
                               qMin<qsizetype>(
                                   m_index->lengthOf(points[i].line), 200)));
        marker.insert(u"msg", message.toHtmlEscaped());
        markers.append(marker);
    }
    const QString js = QStringLiteral("setMarkers(%1);")
        .arg(QString::fromUtf8(
            QJsonDocument(markers).toJson(QJsonDocument::Compact)));

    if (m_mapReady)
        runMapJs(js);
    else
        m_pendingMarkerJs = js; // flushed when the page finishes loading
}

void MapViewer::runMapJs(const QString& js)
{
    if (m_mapReady)
        m_mapView->page()->runJavaScript(js);
}

void MapViewer::onMapLoadFinished(bool ok)
{
    m_mapReady = ok;
    if (ok && !m_pendingMarkerJs.isEmpty()) {
        m_mapView->page()->runJavaScript(m_pendingMarkerJs);
        m_pendingMarkerJs.clear();
    }
}

void MapViewer::onMapUrlChanged(const QUrl& url)
{
    const QString hash = url.fragment();

    // Area drags set location.hash to "area=S,W,N,E&t=<nonce>".
    if (hash.startsWith(QLatin1String("area="))) {
        const QString bounds = hash.mid(5).section(u'&', 0, 0);
        const QStringList parts = bounds.split(u',');
        if (parts.size() != 4)
            return;
        bool ok0 = false, ok1 = false, ok2 = false, ok3 = false;
        const double south = parts[0].toDouble(&ok0);
        const double west = parts[1].toDouble(&ok1);
        const double north = parts[2].toDouble(&ok2);
        const double east = parts[3].toDouble(&ok3);
        if (ok0 && ok1 && ok2 && ok3)
            applyAreaBounds(south, west, north, east);
        return;
    }

    // Marker clicks set location.hash to "line-<n>-<nonce>".
    if (!hash.startsWith(QLatin1String("line-")))
        return;
    const QStringList parts = hash.split(u'-');
    if (parts.size() < 2)
        return;
    bool ok = false;
    const int line = parts[1].toInt(&ok);
    if (!ok)
        return;

    m_syncing = true;
    const int row = m_model->rowForLine(line);
    if (row >= 0)
        m_tableView->selectRow(row);
    m_syncing = false;
    emit pluginEvent(PluginEvent::LinesSelected,
                     QVariant::fromValue(QList<int> { line }));
}

void MapViewer::onPluginEvent(PluginEvent event, const QVariant& data)
{
    if (event != PluginEvent::LinesSelected)
        return;
    const QList<int> lines = data.value<QList<int>>();
    if (lines.isEmpty())
        return;

    m_syncing = true;
    m_tableView->clearSelection();
    int firstRow = -1;
    for (int line : lines) {
        const int row = m_model->rowForLine(line);
        if (row < 0)
            continue;
        if (firstRow < 0)
            firstRow = row;
        m_tableView->selectionModel()->select(
            m_model->index(row, 0),
            QItemSelectionModel::Select | QItemSelectionModel::Rows);
    }
    if (firstRow >= 0) {
        m_tableView->scrollTo(m_model->index(firstRow, 0));
        const logdor::GeoPoint& point = m_model->points()[firstRow];
        runMapJs(QStringLiteral("panToPoint(%1, %2);")
                     .arg(point.latitude)
                     .arg(point.longitude));
    }
    m_syncing = false;
}
