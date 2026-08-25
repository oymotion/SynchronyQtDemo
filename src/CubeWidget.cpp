#include "CubeWidget.h"

#include <QPainter>
#include <QPainterPath>
#include <QtMath>
#include <algorithm>
#include <cmath>

namespace {
// Unit cube vertices
const double kVertices[8][3] = {
    {-1, -1, -1}, {1, -1, -1}, {1, 1, -1}, {-1, 1, -1},
    {-1, -1, 1}, {1, -1, 1}, {1, 1, 1}, {-1, 1, 1},
};
// Faces as vertex index quads
const int kFaces[6][4] = {
    {0, 1, 2, 3}, {4, 5, 6, 7}, {0, 1, 5, 4},
    {2, 3, 7, 6}, {0, 3, 7, 4}, {1, 2, 6, 5},
};
const QColor kFaceColors[6] = {
    QColor(0, 180, 180), QColor(200, 60, 200), QColor(220, 200, 40),
    QColor(200, 60, 60), QColor(60, 160, 60), QColor(60, 100, 220),
};
const double kCameraDist = 4.5;
}

CubeWidget::CubeWidget(QWidget* parent) : QWidget(parent) {
    setMinimumHeight(160);
}

void CubeWidget::setQuaternion(double w, double x, double y, double z) {
    _quat[0] = w;
    _quat[1] = x;
    _quat[2] = y;
    _quat[3] = z;
    _hasQuaternion = true;
    update();
}

void CubeWidget::clearQuaternion() {
    _hasQuaternion = false;
    _quat[0] = 1.0;
    _quat[1] = _quat[2] = _quat[3] = 0.0;
    update();
}

void CubeWidget::setPlaceholder(const QString& text) {
    _placeholder = text;
    update();
}

QSize CubeWidget::minimumSizeHint() const {
    return QSize(200, 160);
}

void CubeWidget::paintEvent(QPaintEvent* /*event*/) {
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    p.fillRect(rect(), QColor(28, 28, 30));

    if (!_hasQuaternion) {
        p.setPen(QColor(150, 150, 150));
        p.drawText(rect(), Qt::AlignCenter, _placeholder);
        return;
    }

    // Rotation matrix from the quaternion
    double w = _quat[0], x = _quat[1], y = _quat[2], z = _quat[3];
    const double norm = std::sqrt(w * w + x * x + y * y + z * z);
    if (norm <= 0.0) {
        return;
    }
    w /= norm;
    x /= norm;
    y /= norm;
    z /= norm;
    const double R[3][3] = {
        {1 - 2 * (y * y + z * z), 2 * (x * y - w * z), 2 * (x * z + w * y)},
        {2 * (x * y + w * z), 1 - 2 * (x * x + z * z), 2 * (y * z - w * x)},
        {2 * (x * z - w * y), 2 * (y * z + w * x), 1 - 2 * (x * x + y * y)},
    };

    const double scale = qMin(width(), height()) * 0.28;
    const QPointF center(width() / 2.0, height() / 2.0);

    auto rotate = [&](const double v[3], double out[3]) {
        for (int i = 0; i < 3; ++i) {
            out[i] = R[i][0] * v[0] + R[i][1] * v[1] + R[i][2] * v[2];
        }
    };
    auto project = [&](const double v[3]) {
        const double persp = kCameraDist / (kCameraDist + v[2]);
        return QPointF(center.x() + v[0] * scale * persp,
                       center.y() - v[1] * scale * persp);
    };

    double rotated[8][3];
    for (int i = 0; i < 8; ++i) {
        rotate(kVertices[i], rotated[i]);
    }

    // Draw far faces first
    int order[6] = {0, 1, 2, 3, 4, 5};
    std::sort(order, order + 6, [&](int a, int b) {
        double za = 0, zb = 0;
        for (int k = 0; k < 4; ++k) {
            za += rotated[kFaces[a][k]][2];
            zb += rotated[kFaces[b][k]][2];
        }
        return za > zb; // larger z = farther
    });

    for (int f : order) {
        QPainterPath path;
        for (int k = 0; k < 4; ++k) {
            const QPointF pt = project(rotated[kFaces[f][k]]);
            if (k == 0) {
                path.moveTo(pt);
            } else {
                path.lineTo(pt);
            }
        }
        path.closeSubpath();
        p.setPen(QPen(QColor(20, 20, 20), 1));
        p.setBrush(kFaceColors[f]);
        p.drawPath(path);
    }

    // Body axes: X red, Y green, Z blue
    const double kAxes[3][3] = {{1.5, 0, 0}, {0, 1.5, 0}, {0, 0, 1.5}};
    const QColor kAxisColors[3] = {QColor(220, 60, 60), QColor(60, 200, 60), QColor(80, 120, 255)};
    const double origin[3] = {0, 0, 0};
    const QPointF o = project(origin);
    for (int a = 0; a < 3; ++a) {
        double tip[3];
        rotate(kAxes[a], tip);
        p.setPen(QPen(kAxisColors[a], 2));
        p.drawLine(o, project(tip));
    }
}
