#pragma once

#include <QVector>
#include <QWidget>
#include <functional>

#include "core/pensettings.h"

// Settings > Tools: a picture of the pressure curve, and a pad to try the pen.

class PressureCurveView : public QWidget {
public:
    explicit PressureCurveView(QWidget *parent = nullptr);
    void setCurve(int curve);
    QSize sizeHint() const override { return QSize(96, 96); }

protected:
    void paintEvent(QPaintEvent *) override;

private:
    int m_curve = 0;
};

// Draw on it with the pen: line width follows the pressure after the curve,
// and a flattened tip shows the tilt when "tilt flattens" is on. A mouse
// draws at full pressure. Reports what the pen sends.
class PenTestPad : public QWidget {
public:
    explicit PenTestPad(QWidget *parent = nullptr);
    void clear();
    // Called with the raw pressure, the pressure after the curve, and the tilt.
    std::function<void(double raw, double mapped, QPointF tilt)> onPenReading;
    QSize sizeHint() const override { return QSize(320, 110); }

protected:
    void paintEvent(QPaintEvent *) override;
    void tabletEvent(QTabletEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;

private:
    struct Point {
        QPointF pos;
        double pressure;
        Pen::TipShape tip;
    };
    void addPoint(const QPointF &pos, double pressure, const Pen::TipShape &tip, bool startsStroke);

    QVector<QVector<Point>> m_strokes;
    bool m_penDown = false;
    bool m_mouseDown = false;
};
