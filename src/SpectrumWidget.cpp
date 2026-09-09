#include "SpectrumWidget.h"

#include <QPainter>
#include <algorithm>

namespace {
// Channel colors (same table as WaveformWidget).
const QColor kChannelColors[] = {
    QColor(31, 119, 180), QColor(255, 127, 14), QColor(44, 160, 44),
    QColor(214, 39, 40), QColor(148, 103, 189), QColor(140, 86, 75),
    QColor(227, 119, 194), QColor(127, 127, 127),
};
const int kChannelColorCount = sizeof(kChannelColors) / sizeof(kChannelColors[0]);
}

SpectrumWidget::SpectrumWidget(QWidget* parent) : QWidget(parent) {
    setMinimumHeight(48);
}

void SpectrumWidget::setResult(const std::vector<float>& freqs,
                               const std::vector<std::vector<float>>& mags) {
    _freqs = freqs;
    _mags = mags;
    update();
}

void SpectrumWidget::clear() {
    _freqs.clear();
    _mags.clear();
    update();
}

void SpectrumWidget::setLabels(const QStringList& labels) {
    _labels = labels;
    update();
}

void SpectrumWidget::setPlaceholder(const QString& text) {
    _placeholder = text;
    update();
}

void SpectrumWidget::setColorIndex(int colorIndex) {
    _colorIndex = colorIndex;
    update();
}

QSize SpectrumWidget::minimumSizeHint() const {
    return QSize(200, 48);
}

void SpectrumWidget::paintEvent(QPaintEvent* /*event*/) {
    QPainter p(this);
    p.fillRect(rect(), QColor(28, 28, 30));

    const QRect plot = rect().adjusted(2, 2, -2, -14);  // bottom strip: axis text
    p.setPen(QColor(90, 90, 90));
    p.drawRect(plot.adjusted(0, 0, -1, -1));

    if (_freqs.size() < 2 || _mags.empty()) {
        p.setPen(QColor(150, 150, 150));
        p.drawText(plot, Qt::AlignCenter, _placeholder);
        return;
    }

    const double fMax = _freqs.back();
    if (fMax <= 0.0 || plot.width() < 2) {
        return;
    }

    // Auto Y range across all channels.
    double peak = 0.0;
    for (const auto& row : _mags) {
        for (const float v : row) {
            peak = std::max(peak, static_cast<double>(v));
        }
    }
    const double yMax = peak > 0.0 ? peak * 1.1 : 1.0;

    p.setClipRect(plot);
    int labelRow = 0;
    for (size_t ch = 0; ch < _mags.size(); ++ch) {
        const auto& row = _mags[ch];
        const int colorIdx = _colorIndex >= 0 ? _colorIndex : static_cast<int>(ch);
        const QColor color = kChannelColors[colorIdx % kChannelColorCount];
        const int count = static_cast<int>(std::min(row.size(), _freqs.size()));
        _poly.resize(count);
        for (int i = 0; i < count; ++i) {
            const double x = plot.left() + _freqs[static_cast<size_t>(i)] / fMax * (plot.width() - 1);
            const double y = plot.bottom() - 1 - row[static_cast<size_t>(i)] / yMax * (plot.height() - 2);
            _poly[i] = QPointF(x, y);
        }
        p.setPen(QPen(color, 1));
        p.drawPolyline(_poly);

        const QString label = labelRow < _labels.size() ? _labels[labelRow]
                                                        : QStringLiteral("ch%1").arg(ch);
        p.drawText(plot.left() + 4, plot.top() + 12 + labelRow * 13, label);
        ++labelRow;
    }
    p.setClipping(false);

    // Frequency axis endpoints.
    p.setPen(QColor(150, 150, 150));
    const QRect axisRect(rect().left() + 4, plot.bottom() + 1, rect().width() - 8, 12);
    p.drawText(axisRect, Qt::AlignLeft | Qt::AlignVCenter, QStringLiteral("0"));
    p.drawText(axisRect, Qt::AlignRight | Qt::AlignVCenter,
               QStringLiteral("%1 Hz").arg(fMax, 0, 'f', 1));
}
