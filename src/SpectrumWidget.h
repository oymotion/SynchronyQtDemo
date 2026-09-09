#ifndef SPECTRUMWIDGET_H
#define SPECTRUMWIDGET_H

#include <QColor>
#include <QPolygonF>
#include <QString>
#include <QStringList>
#include <QWidget>
#include <vector>

// Spectrum view below the 2D waveform: the FFT result is pushed in with
// setResult() and drawn as one magnitude curve per channel, colored like the
// waveform.
class SpectrumWidget : public QWidget {
    Q_OBJECT
public:
    explicit SpectrumWidget(QWidget* parent = nullptr);

    // Replaces the displayed spectrum and repaints. freqs is the shared
    // frequency axis (Hz), mags one amplitude row per channel.
    void setResult(const std::vector<float>& freqs,
                   const std::vector<std::vector<float>>& mags);
    void clear();
    void setLabels(const QStringList& labels);
    void setPlaceholder(const QString& text);
    // colorIndex >= 0 pins the curve color (single-channel rows); -1 colors
    // each curve by its row index.
    void setColorIndex(int colorIndex);

    QSize minimumSizeHint() const override;

protected:
    void paintEvent(QPaintEvent* event) override;

private:
    std::vector<float> _freqs;
    std::vector<std::vector<float>> _mags;
    QPolygonF _poly;  // reusable paint buffer
    QStringList _labels;
    int _colorIndex = -1;
    QString _placeholder = QStringLiteral("Not connected");
};

#endif // SPECTRUMWIDGET_H
