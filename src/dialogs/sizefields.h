#pragma once

#include "core/units.h"

#include <QSize>
#include <QSizeF>
#include <QWidget>

class QSpinBox;
class QDoubleSpinBox;
class QComboBox;
class QCheckBox;
class QRadioButton;
class QLabel;

// Pixel size, print size and resolution, kept in step. Used by Resize,
// Canvas Size and New Image.
//
// A size can be typed in pixels or as a printed size in mm, cm or inches.
// Changing the resolution keeps whichever of the two was typed last ("keep"),
// so "10 x 15 cm at 300 dpi" comes out the same in any order: type the print
// size and then the resolution, or the other way round. The choice is shown
// and can be changed by hand.
class SizeFields : public QWidget {
    Q_OBJECT
public:
    struct Options {
        bool percent = false;          // offer "By percentage" (Resize, Canvas Size)
        bool keepAspect = true;        // initial state of "Maintain aspect ratio"
    };
    enum class Keep { PixelSize, PrintSize };

    SizeFields(QSize original, double dpi, Options options, QWidget *parent = nullptr);

    QSize pixelSize() const { return m_px; }
    double dpi() const { return m_dpi; }
    Keep keep() const { return m_keep; }
    bool keepsAspect() const;
    // Fits within the largest image the program makes (65535 pixels a side).
    bool isValid() const;

    // Presets and tests. Each acts like typing into the matching fields.
    void setPixelSize(QSize size);
    void setPrintSize(QSizeF size, Units::Length unit);   // also shows that unit
    void setResolution(double value, Units::Resolution unit);
    void setPercent(double percent);
    void setLengthUnit(Units::Length unit);
    void setResolutionUnit(Units::Resolution unit);
    void setKeepAspect(bool keep);
    void setKeep(Keep keep);
    void swapWidthAndHeight();

    // What the fields show (for tests and the summary line).
    double shownPrintWidth() const;
    double shownPrintHeight() const;
    double shownResolution() const;
    QSizeF printSizeInches() const { return m_printIn; }
    Units::Length lengthUnit() const { return m_length; }
    Units::Resolution resolutionUnit() const { return m_resUnit; }

    static constexpr int kMaxPixels = 65535;

signals:
    void changed();

private:
    void onPixelWidth(int v);
    void onPixelHeight(int v);
    void onPrintWidth(double v);
    void onPrintHeight(double v);
    void onResolution(double v);
    void onPercent(double v);
    void onMode();
    void onAspectToggled(bool on);
    void pixelsFromPrint();
    void printFromPixels();
    void refresh();
    void setKeepInternal(Keep keep);

    QSize m_original;
    QSize m_px;
    QSizeF m_printIn;          // print size in inches
    double m_dpi;
    double m_ratio;            // width / height while the aspect ratio is kept
    Keep m_keep = Keep::PixelSize;
    Units::Length m_length;
    Units::Resolution m_resUnit;
    Options m_options;
    bool m_overflow = false;   // a print size asked for more than kMaxPixels

    QRadioButton *m_percentRB = nullptr;
    QRadioButton *m_absoluteRB = nullptr;
    QDoubleSpinBox *m_percentSpin = nullptr;
    QCheckBox *m_aspect = nullptr;
    QSpinBox *m_widthSpin = nullptr;
    QSpinBox *m_heightSpin = nullptr;
    QDoubleSpinBox *m_printWidthSpin = nullptr;
    QDoubleSpinBox *m_printHeightSpin = nullptr;
    QComboBox *m_lengthCombo = nullptr;
    QLabel *m_heightUnitLabel = nullptr;
    QDoubleSpinBox *m_resSpin = nullptr;
    QComboBox *m_resCombo = nullptr;
    QRadioButton *m_keepPixelRB = nullptr;
    QRadioButton *m_keepPrintRB = nullptr;
    QLabel *m_summary = nullptr;
};
