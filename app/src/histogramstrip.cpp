#include "histogramstrip.h"

#include "logtablemodel.h"

#include <QDateTime>
#include <QKeyEvent>
#include <QLocale>
#include <QMouseEvent>
#include <QPainter>
#include <QToolTip>

#include <algorithm>

using namespace logdor;

namespace {

// Stack from the floor up in increasing urgency so errors always sit on top
// of the pile where they are visible.
constexpr Severity kStackOrder[]
    = { Severity::None,    Severity::Verbose, Severity::Debug, Severity::Info,
        Severity::Warning, Severity::Error,   Severity::Fatal };

QColor laneColor(Severity severity)
{
    const QColor color = LogTableModel::severityColor(severity);
    return color.isValid() ? color : QColor(128, 128, 128);
}

} // namespace

HistogramStrip::HistogramStrip(QWidget* parent)
    : QWidget(parent)
{
    setFixedHeight(48);
    setMouseTracking(true);
    setFocusPolicy(Qt::ClickFocus); // Esc clears the brush
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
}

void HistogramStrip::setHistogram(const HistogramResult& result)
{
    m_result = result;
    m_hasData = result.minMs <= result.maxMs && !result.buckets.empty();
    update();
}

void HistogramStrip::clearHistogram()
{
    m_result = {};
    m_hasData = false;
    update();
}

void HistogramStrip::clearBrush()
{
    m_brushFromMs = 0;
    m_brushToMs = -1;
    update();
}

qint64 HistogramStrip::msAtX(int x) const
{
    const qint64 span = m_result.toMs - m_result.fromMs;
    const int w = std::max(1, width());
    return m_result.fromMs + span * std::clamp(x, 0, w) / w;
}

int HistogramStrip::xForMs(qint64 ms) const
{
    const qint64 span = std::max<qint64>(1, m_result.toMs - m_result.fromMs);
    return int((ms - m_result.fromMs) * width() / span);
}

int HistogramStrip::bucketAtX(int x) const
{
    if (!m_hasData)
        return -1;
    const int count = int(m_result.buckets.size());
    return std::clamp(x * count / std::max(1, width()), 0, count - 1);
}

void HistogramStrip::paintEvent(QPaintEvent*)
{
    QPainter painter(this);
    painter.fillRect(rect(), palette().base());

    if (!m_hasData) {
        painter.setPen(palette().placeholderText().color());
        painter.drawText(rect(), Qt::AlignCenter, tr("No timestamps"));
        return;
    }

    qint64 maxTotal = 1;
    for (const auto& bucket : m_result.buckets) {
        qint64 total = 0;
        for (qint64 count : bucket)
            total += count;
        maxTotal = std::max(maxTotal, total);
    }

    const int count = int(m_result.buckets.size());
    const double barWidth = double(width()) / count;
    const int floorY = height() - 1;
    for (int i = 0; i < count; ++i) {
        const auto& bucket = m_result.buckets[size_t(i)];
        const int x0 = int(i * barWidth);
        const int x1 = std::max(x0 + 1, int((i + 1) * barWidth));
        int y = floorY;
        for (Severity severity : kStackOrder) {
            const qint64 lane = bucket[size_t(severity)];
            if (lane == 0)
                continue;
            const int h = std::max<int>(
                1, int(lane * (height() - 4) / maxTotal));
            painter.fillRect(x0, y - h, x1 - x0, h, laneColor(severity));
            y -= h;
        }
    }

    // Active brush (or the drag in progress) as a translucent overlay.
    int brushX0 = -1, brushX1 = -1;
    if (m_dragging) {
        brushX0 = std::min(m_dragStartX, m_dragCurrentX);
        brushX1 = std::max(m_dragStartX, m_dragCurrentX);
    } else if (hasBrush()) {
        brushX0 = xForMs(m_brushFromMs);
        brushX1 = xForMs(m_brushToMs);
    }
    if (brushX0 >= 0) {
        QColor overlay = palette().highlight().color();
        overlay.setAlpha(70);
        painter.fillRect(QRect(QPoint(brushX0, 0),
                               QPoint(brushX1, height())), overlay);
        painter.setPen(palette().highlight().color());
        painter.drawLine(brushX0, 0, brushX0, height());
        painter.drawLine(brushX1, 0, brushX1, height());
    }

    if (m_hoverX >= 0 && !m_dragging) {
        painter.setPen(palette().text().color());
        painter.drawLine(m_hoverX, 0, m_hoverX, height());
    }
}

void HistogramStrip::mousePressEvent(QMouseEvent* event)
{
    if (!m_hasData || event->button() != Qt::LeftButton)
        return;
    m_dragging = true;
    m_dragStartX = m_dragCurrentX = event->pos().x();
    update();
}

void HistogramStrip::mouseMoveEvent(QMouseEvent* event)
{
    m_hoverX = event->pos().x();
    if (m_dragging)
        m_dragCurrentX = m_hoverX;
    update();

    const int bucket = bucketAtX(m_hoverX);
    if (bucket < 0)
        return;
    const qint64 from = m_result.fromMs + bucket * m_result.bucketWidthMs;
    qint64 total = 0, errors = 0;
    for (Severity severity : kStackOrder) {
        const qint64 lane = m_result.buckets[size_t(bucket)][size_t(severity)];
        total += lane;
        if (severity >= Severity::Warning)
            errors += lane;
    }
    const QLocale locale;
    QString tip = tr("%1\n%2 events")
                      .arg(QDateTime::fromMSecsSinceEpoch(from)
                               .toString(u"yyyy-MM-dd HH:mm:ss.zzz"),
                           locale.toString(total));
    if (errors > 0)
        tip += tr(" (%1 warnings+)").arg(locale.toString(errors));
    QToolTip::showText(event->globalPosition().toPoint(), tip, this);
}

void HistogramStrip::mouseReleaseEvent(QMouseEvent* event)
{
    if (!m_dragging || event->button() != Qt::LeftButton)
        return;
    m_dragging = false;
    const int x0 = std::min(m_dragStartX, m_dragCurrentX);
    const int x1 = std::max(m_dragStartX, m_dragCurrentX);

    if (x1 - x0 < 3) { // a click, not a brush
        if (hasBrush()) {
            clearBrush();
            emit timeRangeSelected(0, 0);
        } else {
            emit timeClicked(msAtX(x0));
        }
        update();
        return;
    }

    m_brushFromMs = msAtX(x0);
    m_brushToMs = msAtX(x1);
    update();
    emit timeRangeSelected(m_brushFromMs, m_brushToMs);
}

void HistogramStrip::keyPressEvent(QKeyEvent* event)
{
    if (event->key() == Qt::Key_Escape && hasBrush()) {
        clearBrush();
        emit timeRangeSelected(0, 0);
        return;
    }
    QWidget::keyPressEvent(event);
}

void HistogramStrip::leaveEvent(QEvent* event)
{
    m_hoverX = -1;
    update();
    QWidget::leaveEvent(event);
}
