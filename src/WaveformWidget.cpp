#include "WaveformWidget.h"

#include "DeviceState.h"

#include <QPainter>
#include <QMutexLocker>
#include <algorithm>

namespace {
// Channel colors (readable on a dark background).
const QColor kChannelColors[] = {
    QColor(31, 119, 180), QColor(255, 127, 14), QColor(44, 160, 44),
    QColor(214, 39, 40), QColor(148, 103, 189), QColor(140, 86, 75),
    QColor(227, 119, 194), QColor(127, 127, 127),
};
const int kChannelColorCount = sizeof(kChannelColors) / sizeof(kChannelColors[0]);
const int kSideMargin = 66;
}

WaveformWidget::WaveformWidget(QWidget* parent) : QWidget(parent) {
    setMinimumHeight(48);
}

void WaveformWidget::setSource(const RingBuffer* buffer, QMutex* mutex, int channel, int colorIndex) {
    _buffer = buffer;
    _mutex = mutex;
    _channel = channel;
    _colorIndex = colorIndex;
    update();
}

void WaveformWidget::setLabels(const QStringList& labels) {
    _labels = labels;
    update();
}

void WaveformWidget::setFixedYRange(double low, double high) {
    _fixedRange = true;
    _fixedLow = low;
    _fixedHigh = high;
    update();
}

void WaveformWidget::setAutoYRange() {
    _fixedRange = false;
    update();
}

void WaveformWidget::setPlaceholder(const QString& text) {
    _placeholder = text;
    update();
}

void WaveformWidget::setSideText(const QString& text, const QColor& color) {
    _sideText = text;
    _sideColor = color;
    update();
}

QSize WaveformWidget::minimumSizeHint() const {
    return QSize(200, 48);
}

void WaveformWidget::paintEvent(QPaintEvent* /*event*/) {
    QPainter p(this);
    p.fillRect(rect(), QColor(28, 28, 30));

    const QRect plot = rect().adjusted(2, 2, -kSideMargin, -2);
    p.setPen(QColor(90, 90, 90));
    p.drawRect(plot.adjusted(0, 0, -1, -1));
    p.setPen(QColor(60, 60, 60));
    p.drawLine(plot.left(), plot.center().y(), plot.right(), plot.center().y());

    if (_buffer == nullptr || _mutex == nullptr) {
        p.setPen(QColor(150, 150, 150));
        p.drawText(plot, Qt::AlignCenter, _placeholder);
        return;
    }

    QMutexLocker lock(_mutex);
    const RingBuffer& buf = *_buffer;
    if (!buf.allocated || buf.length < 2 || plot.width() < 2) {
        p.setPen(QColor(150, 150, 150));
        p.drawText(plot, Qt::AlignCenter, _placeholder);
        return;
    }

    QList<int> channels;
    if (_channel >= 0) {
        if (_channel < buf.channels) {
            channels.append(_channel);
        }
    } else {
        for (int ch = 0; ch < buf.channels; ++ch) {
            channels.append(ch);
        }
    }
    if (channels.isEmpty()) {
        p.setPen(QColor(150, 150, 150));
        p.drawText(plot, Qt::AlignCenter, _placeholder);
        return;
    }

    const int w = plot.width();
    const int len = buf.length;

    // Resolve the Y range (fixed, or auto from the visible samples).
    double low = _fixedLow;
    double high = _fixedHigh;
    if (!_fixedRange) {
        double mn = std::numeric_limits<double>::max();
        double mx = std::numeric_limits<double>::lowest();
        for (int ch : channels) {
            const auto& samples = buf.samples[ch];
            const int step = qMax(1, len / (w * 2));
            for (int i = 0; i < len; i += step) {
                const double v = samples[(buf.writeIndex + i) % len];
                mn = std::min(mn, v);
                mx = std::max(mx, v);
            }
        }
        if (mn > mx) {
            mn = -1.0;
            mx = 1.0;
        }
        double margin = std::max((mx - mn) * 0.1, 0.01);
        if (mn == mx) {
            mn -= 1.0;
            mx += 1.0;
            margin = 0.0;
        }
        low = mn - margin;
        high = mx + margin;
    }
    const double span = high - low;

    p.setClipRect(plot);
    int labelRow = 0;
    for (int ch : channels) {
        const int colorIdx = _colorIndex >= 0 ? _colorIndex : ch;
        const QColor color = kChannelColors[colorIdx % kChannelColorCount];
        QPolygonF points;
        points.reserve(w);
        const auto& samples = buf.samples[ch];
        for (int x = 0; x < w; ++x) {
            const int si = static_cast<int>(static_cast<qint64>(x) * len / w);
            const double v = samples[(buf.writeIndex + si) % len];
            double ty = plot.bottom() - 1 - (v - low) / span * (plot.height() - 2);
            points.append(QPointF(plot.left() + x, ty));
        }
        p.setPen(QPen(color, 1));
        p.drawPolyline(points);

        QString label = labelRow < _labels.size() ? _labels[labelRow]
                                                  : QStringLiteral("ch%1").arg(ch);
        p.drawText(plot.left() + 4, plot.top() + 12 + labelRow * 13, label);
        ++labelRow;
    }
    p.setClipping(false);

    if (!_sideText.isEmpty()) {
        p.setPen(_sideColor);
        const QRect sideRect(rect().right() - kSideMargin + 4, rect().top(),
                             kSideMargin - 6, rect().height());
        p.drawText(sideRect, Qt::AlignVCenter | Qt::AlignLeft, _sideText);
    }
}
