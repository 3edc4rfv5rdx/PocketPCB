#pragma once

#include <QDialog>

class QDoubleSpinBox;
class QLabel;
class QPushButton;
class QScreen;

namespace ppcb {

class RulerLine;

// How many pixels of the program make a millimetre on 'screen': by the size
// set in Settings > Screen size, else as the screen reports its size.
double screenPixelsPerMm(const QScreen *screen);

// Settings > Screen size: the real size of the screen, for showing a drawing
// at its actual size. Set by typing the diagonal of the screen or by drawing
// a line out to 100 mm on a ruler held to the screen; either shows the other.
class ScreenSizeDialog : public QDialog
{
    Q_OBJECT

public:
    explicit ScreenSizeDialog(const QScreen *screen, QWidget *parent = nullptr);

    // The name of the dialog and of its command.
    static QString title();

    // The dots of the screen in a millimetre as set; 0: as the screen
    // reports its size.
    double dotsPerMm() const;

private:
    // Shows 'dots' everywhere, the diagonal and the line too.
    void setDots(double dots, bool fromSystem);
    void diagonalChanged(double inches);
    void lineMoved(double pixels);
    void showDensity();

    // Taken from the screen when the dialog opens.
    double m_pixelRatio = 1.0;
    double m_diagonalDots = 0.0;
    double m_systemDots = 0.0;

    double m_dots = 0.0;
    bool m_fromSystem = true;
    // Set while the fields are filled by the program, not by the user.
    bool m_filling = false;

    QDoubleSpinBox *m_diagonal = nullptr;
    RulerLine *m_line = nullptr;
    QLabel *m_density = nullptr;
    QPushButton *m_systemButton = nullptr;
};

} // namespace ppcb
