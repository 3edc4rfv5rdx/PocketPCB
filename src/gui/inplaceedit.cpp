#include "inplaceedit.h"

#include "canvasview.h"

#include <QApplication>
#include <QKeyEvent>
#include <QLineEdit>
#include <QTimer>

namespace ppcb {

namespace {

// The line holds at least this many letters.
const int EDIT_LETTERS = 12;

} // namespace

InPlaceEdit::InPlaceEdit(CanvasView *canvas, QObject *parent)
    : QObject(parent)
    , m_canvas(canvas)
{
}

void InPlaceEdit::open(const QString &text, const QRectF &rect)
{
    close(true);
    m_rect = rect;
    auto *edit = new QLineEdit(text, m_canvas->viewport());
    edit->selectAll();
    edit->installEventFilter(this);
    connect(edit, &QLineEdit::returnPressed, this, [this]() { close(true); });
    connect(m_canvas, &CanvasView::viewMoved, edit, [this]() { place(); });
    m_edit = edit;
    place();
    edit->show();
    edit->setFocus();
}

void InPlaceEdit::place()
{
    if (m_edit.isNull())
        return;
    QLineEdit *edit = m_edit;
    // Over its place, at least wide enough to type a value into.
    const QRect place = m_canvas->mapFromScene(m_rect).boundingRect();
    const int width = qMax(place.width(), edit->fontMetrics().averageCharWidth() * EDIT_LETTERS);
    const int height = edit->sizeHint().height();
    edit->setGeometry(place.center().x() - width / 2, place.center().y() - height / 2, width,
                      height);
}

void InPlaceEdit::close(bool keep)
{
    if (m_edit.isNull())
        return;
    QLineEdit *edit = m_edit;
    m_edit.clear();
    // Hiding it takes its focus away: that must not end it again.
    edit->removeEventFilter(this);
    edit->disconnect(this);
    disconnect(m_canvas, nullptr, edit, nullptr);
    const QString text = edit->text();
    edit->hide();
    edit->deleteLater();
    m_canvas->setFocus();
    if (keep)
        emit accepted(text);
}

bool InPlaceEdit::eventFilter(QObject *watched, QEvent *event)
{
    if (watched != m_edit.data())
        return QObject::eventFilter(watched, event);
    switch (event->type()) {
    case QEvent::ShortcutOverride: {
        // Esc and Enter belong to the line, not to the window's actions.
        const int key = static_cast<QKeyEvent *>(event)->key();
        if (key == Qt::Key_Escape || key == Qt::Key_Return || key == Qt::Key_Enter) {
            event->accept();
            return true;
        }
        break;
    }
    case QEvent::KeyPress:
        if (static_cast<QKeyEvent *>(event)->key() == Qt::Key_Escape) {
            close(false);
            return true;
        }
        break;
    case QEvent::FocusOut:
        // A press of the panning buttons on the canvas takes the focus, but
        // the view only moves: the line keeps it. A short right click opens
        // a menu, which takes the focus again and ends the line.
        if (static_cast<QFocusEvent *>(event)->reason() == Qt::MouseFocusReason
            && (QApplication::mouseButtons() & (Qt::RightButton | Qt::MiddleButton))
            && (QApplication::focusWidget() == m_canvas
                || QApplication::focusWidget() == m_canvas->viewport())) {
            QLineEdit *edit = m_edit;
            QTimer::singleShot(0, edit, [edit]() { edit->setFocus(); });
            break;
        }
        close(true);
        break;
    default:
        break;
    }
    return QObject::eventFilter(watched, event);
}

} // namespace ppcb
