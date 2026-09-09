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

    const int w = plot.width();

    // Snapshot the drawn channels (oldest -> newest), then range and draw
    // from the copies.
    int snapLen = 0;
    {
        QMutexLocker lock(_mutex);
        const RingBuffer& buf = *_buffer;
        if (!buf.allocated || buf.length < 2 || w < 2) {
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

        snapLen = buf.length;
        _snapshot.resize(static_cast<size_t>(channels.size()));
        for (size_t k = 0; k < _snapshot.size(); ++k) {
            ChannelSnapshot& snap = _snapshot[k];
            snap.channel = channels[static_cast<int>(k)];
            snap.samples.resize(static_cast<size_t>(snapLen));
            const auto& src = buf.samples[snap.channel];
            const int wi = buf.writeIndex;
            for (int i = 0; i < snapLen; ++i) {
                snap.samples[static_cast<size_t>(i)] = src[(wi + i) % snapLen];
            }
        }
    }

    // Resolve the Y range (fixed, or auto from the visible samples).
    double low = _fixedLow;
    double high = _fixedHigh;
    if (!_fixedRange) {
        double mn = std::numeric_limits<double>::max();
        double mx = std::numeric_limits<double>::lowest();
        const int step = qMax(1, snapLen / (w * 2));
        for (const ChannelSnapshot& snap : _snapshot) {
            for (int i = 0; i < snapLen; i += step) {
                const double v = snap.samples[static_cast<size_t>(i)];
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
    for (const ChannelSnapshot& snap : _snapshot) {
        const int colorIdx = _colorIndex >= 0 ? _colorIndex : snap.channel;
        const QColor color = kChannelColors[colorIdx % kChannelColorCount];
        _poly.resize(w);
        for (int x = 0; x < w; ++x) {
            const int si = static_cast<int>(static_cast<qint64>(x) * snapLen / w);
            const double v = snap.samples[static_cast<size_t>(si)];
            double ty = plot.bottom() - 1 - (v - low) / span * (plot.height() - 2);
            _poly[x] = QPointF(plot.left() + x, ty);
        }
        p.setPen(QPen(color, 1));
        p.drawPolyline(_poly);

        QString label = labelRow < _labels.size() ? _labels[labelRow]
                                                  : QStringLiteral("ch%1").arg(snap.channel);
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
