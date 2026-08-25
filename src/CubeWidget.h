#ifndef CUBEWIDGET_H
#define CUBEWIDGET_H

#include <QWidget>

// 3D cube visualization of the device orientation, driven by the quaternion
// stream.
class CubeWidget : public QWidget {
    Q_OBJECT
public:
    explicit CubeWidget(QWidget* parent = nullptr);

    void setQuaternion(double w, double x, double y, double z);
    void clearQuaternion();   // back to the placeholder state
    void setPlaceholder(const QString& text);

    QSize minimumSizeHint() const override;

protected:
    void paintEvent(QPaintEvent* event) override;

private:
    double _quat[4] = {1.0, 0.0, 0.0, 0.0}; // w, x, y, z
    bool _hasQuaternion = false;
    QString _placeholder = QStringLiteral("Not connected");
};

#endif // CUBEWIDGET_H
