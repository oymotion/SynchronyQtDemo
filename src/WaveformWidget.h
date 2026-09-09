#ifndef WAVEFORMWIDGET_H
#define WAVEFORMWIDGET_H

#include <QColor>
#include <QPolygonF>
#include <QString>
#include <QStringList>
#include <QWidget>
#include <QMutex>
#include <vector>

struct RingBuffer;

// Lightweight QPainter waveform view. It does not own sample data; it pulls
// from a RingBuffer (guarded by the caller's mutex) during paintEvent, so it
// renders whatever device is currently selected.
class WaveformWidget : public QWidget {
    Q_OBJECT
public:
    explicit WaveformWidget(QWidget* parent = nullptr);

    // channel == -1 draws all channels overlaid, otherwise only that channel.
    // colorIndex >= 0 decouples the curve color from the data channel index
    // (e.g. the PPG plot set gives each plot its own color).
    void setSource(const RingBuffer* buffer, QMutex* mutex, int channel, int colorIndex = -1);
    bool hasSource() const { return _buffer != nullptr; }

    void setLabels(const QStringList& labels);
    void setFixedYRange(double low, double high);
    void setAutoYRange();
    void setPlaceholder(const QString& text);
    // Right-margin text (EMG impedance), drawn in the given color.
    void setSideText(const QString& text, const QColor& color);

    QSize minimumSizeHint() const override;

protected:
    void paintEvent(QPaintEvent* event) override;

private:
    const RingBuffer* _buffer = nullptr;
    QMutex* _mutex = nullptr;
    int _channel = -1;
    int _colorIndex = -1;
    QStringList _labels;
    bool _fixedRange = false;
    double _fixedLow = -1.0;
    double _fixedHigh = 1.0;
    QString _placeholder = QStringLiteral("Not connected");
    QString _sideText;
    QColor _sideColor = Qt::white;

    // Reusable paint buffers.
    struct ChannelSnapshot {
        int channel = -1;
        std::vector<float> samples;  // oldest -> newest
    };
    std::vector<ChannelSnapshot> _snapshot;
    QPolygonF _poly;
};

#endif // WAVEFORMWIDGET_H
