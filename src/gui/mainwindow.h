#pragma once

#include "appsettings.h"
#include "units.h"

#include <QMainWindow>

class QActionGroup;
class QComboBox;
class QLabel;
class QMenu;

namespace ppcb {

class CanvasView;

class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    explicit MainWindow(QWidget *parent = nullptr);

    CanvasView *canvas() const { return m_canvas; }

protected:
    void closeEvent(QCloseEvent *event) override;

private:
    void createMenus();
    void createStatusBar();
    // Adds a checkable, mutually exclusive item to a menu.
    QAction *addChoice(QMenu *menu, QActionGroup *group, const QString &text, bool checked);

    void applyGridStyle(GridStyle style);
    void applyCursorStyle(CursorStyle style);
    void applyZoomCenterAndWarp(bool on);

    void showCursorPosition(Coord x, Coord y);
    void showGridStep(Coord step);

    CanvasView *m_canvas = nullptr;
    QLabel *m_positionLabel = nullptr;
    QComboBox *m_gridStepBox = nullptr;
};

} // namespace ppcb
