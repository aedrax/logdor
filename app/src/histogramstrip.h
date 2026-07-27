#ifndef HISTOGRAMSTRIP_H
#define HISTOGRAMSTRIP_H

#include "logdorexport.h"

#include <logdor/HistogramScan.h>

#include <QWidget>

/**
 * The timeline minimap: severity-stacked bars over a HistogramResult.
 * Hovering shows the bucket's time range and counts; a click emits
 * timeClicked (jump-to-time); a drag brushes a range and emits
 * timeRangeSelected; Esc (or a click with an active brush) clears the brush
 * and emits timeRangeSelected(0, 0). The strip renders whatever result it
 * is given - the owner runs scanHistogram and calls setHistogram().
 */
class LOGDOR_INTERFACE_EXPORT HistogramStrip : public QWidget {
    Q_OBJECT
public:
    explicit HistogramStrip(QWidget* parent = nullptr);

    void setHistogram(const logdor::HistogramResult& result);
    void clearHistogram();
    void clearBrush();
    bool hasBrush() const { return m_brushFromMs <= m_brushToMs; }

signals:
    /// A brushed range in UTC epoch ms; (0, 0) means "clear the range".
    void timeRangeSelected(qint64 fromUtcMs, qint64 toUtcMs);
    /// A plain click on the strip at this time.
    void timeClicked(qint64 utcMs);

protected:
    void paintEvent(QPaintEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void keyPressEvent(QKeyEvent* event) override;
    void leaveEvent(QEvent* event) override;

private:
    qint64 msAtX(int x) const;
    int xForMs(qint64 ms) const;
    int bucketAtX(int x) const;

    logdor::HistogramResult m_result;
    bool m_hasData = false;

    bool m_dragging = false;
    int m_dragStartX = -1;
    int m_dragCurrentX = -1;
    qint64 m_brushFromMs = 0;
    qint64 m_brushToMs = -1; // from > to => no brush
    int m_hoverX = -1;
};

#endif // HISTOGRAMSTRIP_H
