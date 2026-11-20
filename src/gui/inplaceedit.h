#pragma once

#include <QObject>
#include <QPointer>
#include <QRectF>

class QLineEdit;

namespace ppcb {

class CanvasView;

// A line on the canvas over a place of the scene, to type a text into where
// it stands: it goes with that place when the view is zoomed or scrolled.
// Enter or leaving it keeps what it holds, Esc drops it; a press of the
// panning buttons on the canvas leaves it open.
class InPlaceEdit : public QObject
{
    Q_OBJECT

public:
    // The canvas must outlive the edit.
    explicit InPlaceEdit(CanvasView *canvas, QObject *parent = nullptr);

    bool isOpen() const { return !m_edit.isNull(); }
    // Opens the line over 'rect' (scene millimetres) holding 'text', all of
    // it chosen. A line still open is closed first, keeping what it holds.
    void open(const QString &text, const QRectF &rect);
    // Closes the line, if one is open; 'keep' sends what it holds through
    // accepted() once it is closed.
    void close(bool keep);

signals:
    void accepted(const QString &text);

protected:
    bool eventFilter(QObject *watched, QEvent *event) override;

private:
    // Puts the line over its place as the view shows it now.
    void place();

    CanvasView *m_canvas;
    QPointer<QLineEdit> m_edit;
    QRectF m_rect;
};

} // namespace ppcb
