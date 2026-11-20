#include "screensizedialog.h"

#include "appsettings.h"
#include "format.h"

#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QMouseEvent>
#include <QPainter>
#include <QPushButton>
#include <QScreen>
#include <QVBoxLayout>
#include <QWheelEvent>

#include <cmath>
#include <functional>

namespace ppcb {

namespace {

const double MM_PER_INCH = 25.4;
// What a screen that does not report its size is taken for.
const double FALLBACK_DOTS_PER_INCH = 96.0;

// The diagonals that can be typed, in inches.
const double MIN_DIAGONAL_INCHES = 10.0;
const double MAX_DIAGONAL_INCHES = 100.0;

// The line held to a ruler, in millimetres; a tick every TICK_MM, a longer
// one every MID_TICK_MM and the longest every LONG_TICK_MM.
const int LINE_MM = 100;
const int TICK_MM = 5;
const int MID_TICK_MM = 10;
const int LONG_TICK_MM = 50;

// A notch of the mouse wheel, in the units of QWheelEvent::angleDelta().
const int WHEEL_NOTCH = 120;

// The drawing of the line, in pixels: the room left and right of it, the
// mark of its end above it, its ticks below it.
const int LINE_MARGIN = 16;
const int HANDLE_SIZE = 8;
const int LINE_TOP = HANDLE_SIZE + 2;
const int SHORT_TICK = 5;
const int MID_TICK = 9;
const int LONG_TICK = 14;
const int TEXT_GAP = 2;
// The shortest the line can be drawn, and the least width of its field.
const double MIN_LINE_PIXELS = 50.0;
const int MIN_LINE_FIELD_WIDTH = 360;
// The field leaves the line this much room to grow, as a share of it.
const double LINE_ROOM = 1.5;

double systemDotsPerMm(const QScreen *screen)
{
    const double width = screen->physicalSize().width();
    if (width > 0)
        return screen->size().width() * screen->devicePixelRatio() / width;
    return FALLBACK_DOTS_PER_INCH * screen->devicePixelRatio() / MM_PER_INCH;
}

} // namespace

double screenPixelsPerMm(const QScreen *screen)
{
    const double set = AppSettings::screenDotsPerMm();
    const double dots = set > 0 ? set : systemDotsPerMm(screen);
    return dots / screen->devicePixelRatio();
}

// A line LINE_MM long as the screen size says, with ticks; its right end is
// moved by the mouse, by its wheel or by the arrow keys, a pixel a step.
class RulerLine : public QWidget
{
public:
    explicit RulerLine(QWidget *parent)
        : QWidget(parent)
    {
        setFocusPolicy(Qt::StrongFocus);
        setCursor(Qt::SizeHorCursor);
        setMinimumHeight(LINE_TOP + LONG_TICK + TEXT_GAP + fontMetrics().height());
    }

    // Called with the new length in pixels when the user moves the end.
    std::function<void(double)> moved;

    void setLength(double pixels)
    {
        m_length = pixels;
        const int width = qRound(pixels * LINE_ROOM) + 2 * LINE_MARGIN;
        if (width > minimumWidth())
            setMinimumWidth(qMax(width, MIN_LINE_FIELD_WIDTH));
        update();
    }

protected:
    void paintEvent(QPaintEvent *) override
    {
        QPainter painter(this);
        const QColor ink = palette().color(QPalette::WindowText);
        painter.setPen(QPen(ink, 1));
        const double start = LINE_MARGIN;
        const double end = start + m_length;
        painter.drawLine(QPointF(start, LINE_TOP), QPointF(end, LINE_TOP));
        for (int mm = 0; mm <= LINE_MM; mm += TICK_MM) {
            int tick = SHORT_TICK;
            if (mm % LONG_TICK_MM == 0)
                tick = LONG_TICK;
            else if (mm % MID_TICK_MM == 0)
                tick = MID_TICK;
            const double x = start + m_length * mm / LINE_MM;
            painter.drawLine(QPointF(x, LINE_TOP), QPointF(x, LINE_TOP + tick));
        }

        const int textTop = LINE_TOP + LONG_TICK + TEXT_GAP;
        const int textHeight = fontMetrics().height();
        const int textWidth = 2 * LINE_MARGIN;
        painter.drawText(QRectF(start - LINE_MARGIN, textTop, textWidth, textHeight),
                         Qt::AlignCenter, QStringLiteral("0"));
        const QString endText = QString::number(LINE_MM) + QLatin1Char(' ') + mmUnit();
        const int endWidth = fontMetrics().horizontalAdvance(endText);
        painter.drawText(QRectF(end - endWidth / 2.0, textTop, endWidth, textHeight),
                         Qt::AlignCenter, endText);

        // The end that moves: a triangle pointing down at it.
        painter.setRenderHint(QPainter::Antialiasing);
        painter.setPen(Qt::NoPen);
        painter.setBrush(palette().color(hasFocus() ? QPalette::Highlight : QPalette::Mid));
        const QPointF handle[] = {QPointF(end - HANDLE_SIZE / 2.0, 0),
                                  QPointF(end + HANDLE_SIZE / 2.0, 0),
                                  QPointF(end, HANDLE_SIZE)};
        painter.drawPolygon(handle, 3);
    }

    void mousePressEvent(QMouseEvent *event) override
    {
        if (event->button() == Qt::LeftButton)
            moveEnd(event->position().x() - LINE_MARGIN);
    }

    void mouseMoveEvent(QMouseEvent *event) override
    {
        if (event->buttons() & Qt::LeftButton)
            moveEnd(event->position().x() - LINE_MARGIN);
    }

    void keyPressEvent(QKeyEvent *event) override
    {
        if (event->key() == Qt::Key_Left)
            moveEnd(m_length - 1);
        else if (event->key() == Qt::Key_Right)
            moveEnd(m_length + 1);
        else
            QWidget::keyPressEvent(event);
    }

    void wheelEvent(QWheelEvent *event) override
    {
        event->accept();
        const int delta = event->angleDelta().y();
        if (delta == 0)
            return;
        // A touchpad sends parts of a notch: each still moves a pixel.
        const int steps = delta / WHEEL_NOTCH;
        moveEnd(m_length + (steps != 0 ? steps : (delta > 0 ? 1 : -1)));
    }

    void focusInEvent(QFocusEvent *event) override
    {
        QWidget::focusInEvent(event);
        update();
    }

    void focusOutEvent(QFocusEvent *event) override
    {
        QWidget::focusOutEvent(event);
        update();
    }

private:
    void moveEnd(double length)
    {
        length = qBound(MIN_LINE_PIXELS, length, double(width() - 2 * LINE_MARGIN));
        if (length == m_length)
            return;
        m_length = length;
        update();
        if (moved)
            moved(length);
    }

    double m_length = 0.0;
};

ScreenSizeDialog::ScreenSizeDialog(const QScreen *screen, QWidget *parent)
    : QDialog(parent)
    , m_pixelRatio(screen->devicePixelRatio())
    , m_diagonalDots(std::hypot(screen->size().width(), screen->size().height())
                     * screen->devicePixelRatio())
    , m_systemDots(systemDotsPerMm(screen))
{
    setWindowTitle(title());

    const QString lineLength = QString::number(LINE_MM) + QLatin1Char(' ') + mmUnit();
    QLabel *intro = new QLabel(
        tr("Type the diagonal of the screen, or drag the end of the line until it is %1 long on a ruler held to the screen")
                .arg(lineLength)
            + QLatin1Char('.'),
        this);
    intro->setWordWrap(true);

    m_diagonal = new QDoubleSpinBox(this);
    m_diagonal->setObjectName(QStringLiteral("screenDiagonal"));
    m_diagonal->setDecimals(1);
    m_diagonal->setSingleStep(0.1);
    m_diagonal->setRange(MIN_DIAGONAL_INCHES, MAX_DIAGONAL_INCHES);
    m_diagonal->setSuffix(QLatin1Char(' ') + tr("in"));
    QLabel *diagonalLabel = new QLabel(tr("Screen diagonal") + QLatin1Char(':'), this);
    diagonalLabel->setBuddy(m_diagonal);
    QHBoxLayout *diagonalRow = new QHBoxLayout;
    diagonalRow->addWidget(diagonalLabel);
    diagonalRow->addWidget(m_diagonal);
    diagonalRow->addStretch(1);

    m_line = new RulerLine(this);
    m_line->setObjectName(QStringLiteral("screenRulerLine"));
    m_line->moved = [this](double pixels) { lineMoved(pixels); };

    m_density = new QLabel(this);

    QDialogButtonBox *buttons =
        new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    m_systemButton = buttons->addButton(tr("As the screen reports"), QDialogButtonBox::ResetRole);
    connect(m_systemButton, &QPushButton::clicked, this, [this] { setDots(m_systemDots, true); });
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);

    connect(m_diagonal, &QDoubleSpinBox::valueChanged, this, &ScreenSizeDialog::diagonalChanged);

    QVBoxLayout *layout = new QVBoxLayout(this);
    layout->addWidget(intro);
    layout->addLayout(diagonalRow);
    layout->addWidget(m_line);
    layout->addWidget(m_density);
    layout->addWidget(buttons);

    const double set = AppSettings::screenDotsPerMm();
    setDots(set > 0 ? set : m_systemDots, set <= 0);
}

QString ScreenSizeDialog::title()
{
    return tr("Screen size");
}

double ScreenSizeDialog::dotsPerMm() const
{
    return m_fromSystem ? 0.0 : m_dots;
}

void ScreenSizeDialog::setDots(double dots, bool fromSystem)
{
    m_dots = dots;
    m_fromSystem = fromSystem;
    m_filling = true;
    m_diagonal->setValue(m_diagonalDots / dots / MM_PER_INCH);
    m_filling = false;
    m_line->setLength(LINE_MM * dots / m_pixelRatio);
    showDensity();
}

void ScreenSizeDialog::diagonalChanged(double inches)
{
    if (m_filling)
        return;
    m_dots = m_diagonalDots / (inches * MM_PER_INCH);
    m_fromSystem = false;
    m_line->setLength(LINE_MM * m_dots / m_pixelRatio);
    showDensity();
}

void ScreenSizeDialog::lineMoved(double pixels)
{
    m_dots = pixels * m_pixelRatio / LINE_MM;
    m_fromSystem = false;
    m_filling = true;
    m_diagonal->setValue(m_diagonalDots / m_dots / MM_PER_INCH);
    m_filling = false;
    showDensity();
}

void ScreenSizeDialog::showDensity()
{
    m_density->setText(tr("%1 dots per inch").arg(qRound(m_dots * MM_PER_INCH)));
    m_systemButton->setEnabled(!m_fromSystem);
}

} // namespace ppcb
