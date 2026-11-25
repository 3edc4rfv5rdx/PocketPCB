#pragma once

#include "board.h"
#include "boardedit.h"
#include "document.h"
#include "inplaceedit.h"
#include "trackedit.h"

#include <QLineF>
#include <QObject>
#include <QPolygonF>
#include <QRectF>
#include <QTimer>

#include <optional>

namespace ppcb {

class CanvasView;

// Editing of the board shown on the canvas: its outline (see outline.h)
// and its placements (see boardedit.h). Choosing: a corner of the outline is
// dragged along the grid, and so is a side, the sides beside it stretching
// after it; a double click on a corner types its radius in place. A click
// on a placement chooses it, with Ctrl adds it to what is chosen or takes
// it away, and a drag moves what is chosen along the grid; a frame drawn
// on the empty board chooses the placements wholly inside it. A click on a
// placement's designator or value chooses that text alone, and a drag
// moves it away from the placement. Mounting holes are chosen and dragged
// as placements are, and with them; Delete takes them off and moves the
// placements off the board, as they come from the schematic. Placing
// holes: every click puts one on the grid. Laying tracks: a click on a pad
// or a track starts one on the side chosen, every click after it lays a
// straight piece and one at 45 degrees up to it (Shift: one piece at any
// angle), as one step, and a click on copper ends it; V puts a via and goes
// on on the other side. Drawing the outline: every click sets a corner on
// the grid; a click on the first corner closes it. Cutting
// out: a rectangle drawn across the edge is taken off the board. Each
// change is one step. Positions come in scene millimetres.
class BoardEditor : public QObject
{
    Q_OBJECT

public:
    BoardEditor(Document *document, CanvasView *canvas, QObject *parent = nullptr);

    // What is done to the board: the name of the menu item and of the step.
    enum class Action {
        DrawOutline,
        CutOut,
        RectangleBoard,
        MoveCorner,
        MoveSide,
        MoveOutline,
        RoundCorner,
        LockOutline,
        UnlockOutline,
        MoveParts,
        RotateParts,
        MovePartText,
        MoveLinkText,
        LockParts,
        UnlockParts,
        ShowValue,
        HideValue,
        LineToPart,
        PlaceHole,
        MoveHoles,
        DeleteHoles,
        MovePartsOff,
        HoleDiameter,
        PlatedHoles,
        CopperRing,
        LockHoles,
        UnlockHoles,
        PadSides,
        PadNet,
        LayTrack,
        ViaAndOtherSide,
        DeleteTracks,
        DeleteAllTracks,
        TrackWidth,
        Autoroute,
        MoveTrack,
        SpreadTracks,
        ReplaceLinks,
        TrackSide,
    };
    static QString actionName(Action action);
    // Asks for a hole's diameter in millimetres, after a wrong one.
    static QString diameterHint();
    // "Top layer", "Bottom layer".
    static QString sideName(BoardSide side);
    // "Tracks" and "Vias" before a count of them, as in "Vias: 3".
    static QString tracksCountName();
    static QString viasCountName();

    // While the board is shown. Leaving it drops what is being drawn or
    // dragged and goes back to choosing.
    bool isActive() const { return m_active; }
    void setActive(bool active);

    // What the mouse does now.
    enum class Tool { Select, Outline, CutOut, Hole, Track };
    Tool tool() const { return m_tool; }
    void chooseSelectTool();
    // Drawing a new outline, which takes the place of the old one once
    // closed.
    void startDrawingOutline();
    void startCuttingOut();
    // Placing holes of the diameter holeDrill().
    void startPlacingHoles();
    // The diameter of the holes placed: the one chosen last, DEFAULT_HOLE_DRILL
    // at first. Choosing one starts placing them.
    Coord holeDrill() const { return m_holeDrill; }
    void setHoleDrill(Coord drill);
    // Laying tracks on the side chosen (trackSide()).
    void startLayingTracks();
    // The side tracks are laid on, and shown brighter than the other: the
    // board's only side of copper, or the one chosen.
    BoardSide trackSide() const;
    // V: while a track is laid, it goes to the cursor, a via is put there
    // and the track goes on on the other side; else the other side is
    // chosen. Nothing on a board with copper on one side.
    void viaAndOtherSide();
    // Chooses 'side', as V does when it is the other one.
    void setTrackSide(BoardSide side);
    // While the silkscreen is hidden, parts and their texts are not chosen;
    // hiding it drops them from what is chosen.
    void setSilkscreenShown(bool shown);
    // While the tracks and the vias are hidden, they are not chosen and the
    // mouse resting on them lights nothing; hiding them drops them from what
    // is chosen and ends laying tracks.
    bool tracksShown() const { return m_tracksShown; }
    void setTracksShown(bool shown);
    // Space: the piece at 45 degrees goes first or last.
    void swapTrackCorner();
    // Drawing the outline or a cut-out, placing holes or laying tracks is
    // going on: a short right click ends it, as Esc does.
    bool isDrawing() const
    {
        return !m_outlinePoints.isEmpty() || m_cutting
               || (m_active && (m_tool == Tool::Hole || m_tool == Tool::Track));
    }
    // The pieces of the track being laid, from its last point to the
    // cursor; empty when none is.
    QList<Track> trackPreview() const;
    // Where the next hole goes, as it would stand; nothing while not placing.
    std::optional<MountingHole> holeToPlace() const;
    // The outline becomes a rectangle 'width' by 'height' with its top left
    // corner at the origin, as one step; choosing comes back.
    void makeRectangleBoard(Coord width, Coord height);
    // The outline's size; empty for a board without one.
    QRectF outlineBounds() const;
    // The outline may not be changed or moved (Board::outlineLocked).
    bool isOutlineLocked() const { return m_document->design().board.outlineLocked; }

    // The outline is chosen whole: by a double click on a side or by
    // chooseAll(); a drag on any of its sides or corners then moves it all,
    // with the placements chosen. Esc or a click off it drops it.
    bool outlineChosen() const { return m_active && m_outlineChosen && !outline().isEmpty(); }
    // The placements chosen, of those the board has.
    QSet<ObjectId> chosenPlacements() const;
    // The holes chosen, of those the board has.
    QSet<ObjectId> chosenHoles() const;
    // The tracks, the vias and the wire links chosen, of those the board has.
    QSet<ObjectId> chosenTracks() const;
    // The nets of the pads of the placements chosen, lit on the board.
    QSet<QString> litNets() const;
    // The net of the copper chosen, when it is all the copper joined to it
    // (joinedCopper(), as Shift and a click choose): its unrouted
    // connections are drawn bold. Empty otherwise.
    QString joinedChosenNet() const;
    // The text chosen alone; null when none is.
    PlacementText chosenText() const { return m_active ? m_chosenText : PlacementText(); }
    // The lines from a placement's point to where its texts stand, to show
    // whose they are: for the text chosen alone, and for both shown texts of
    // every placement chosen, as the board stands while dragged.
    QList<QLineF> textGuides() const;
    // Something is chosen that may be locked: the outline, placements or
    // holes.
    bool hasChosen() const
    {
        return outlineChosen() || !chosenPlacements().isEmpty() || !chosenHoles().isEmpty();
    }
    // The outline, every placement and every hole.
    void chooseAll();
    // The placements and holes 'ids' and nothing else, as after an Undo of
    // a step that touched them.
    void setChosen(const QSet<ObjectId> &ids);
    // Everything chosen is locked (the lock button is pressed); locking or
    // unlocking it is one step.
    bool chosenLocked() const;
    void setChosenLocked(bool locked);
    // The placements chosen turned by 90 degrees clockwise as one step:
    // one about its own point, several about the middle of them on the grid.
    // While they are dragged, they turn about the point of the one taken,
    // and the drag goes on. Locked ones stay.
    void rotateChosen();
    // Delete, as one step: the holes chosen are taken off the board, and
    // the placements chosen, which come and go with the schematic, are
    // packed off it to the right (withPlacementsPacked()). Locked ones stay.
    void deleteChosen();
    // Every track and via of the board deleted as one step.
    void deleteAllTracks();
    // The tracks of the board put in the middle of the room they run in
    // (withTracksSpread()) on its grid, as one step.
    void spreadTracks();
    // The same for the tracks chosen alone.
    void spreadChosenTracks();
    // The wire links chosen taken off and their connections laid by the
    // autorouter as tracks, with no link, as one step; nothing changes, said
    // in the status bar, when that leaves a connection unrouted.
    void replaceChosenLinks();
    // Some of what is chosen is a wire link.
    bool linksChosen() const;
    // That for the tracks 'ids', every one when empty.
    void spreadTracksOf(const QSet<ObjectId> &ids);
    // The nets of what is chosen: of the pads of the parts and holes, and of
    // the tracks and vias.
    QSet<QString> chosenNets() const;
    // 'board', laid by the autorouter, made the board as one step.
    void applyRouted(const Board &board);

    // What a short right click at 'at' is on, choosing it as a click
    // would, unless it is chosen already: a placement (also by one of its
    // texts), a hole, a corner or a side of the outline, or nothing (what is
    // chosen then stays).
    enum class MenuTarget { None, Placement, Hole, Track, Corner, Side };
    MenuTarget chooseForMenu(const QPointF &at);
    // The corner of the outline the menu was opened on; -1 for none.
    int menuCorner() const { return m_menuCorner; }
    // Types the radius of that corner in place.
    void editMenuCornerRadius()
    {
        if (m_menuCorner >= 0)
            editRadius(m_menuCorner);
    }
    // The width of every track chosen; 0 when they differ or none is.
    Coord chosenTracksWidth() const;
    // Given to the tracks chosen as one step.
    void setChosenTracksWidth(Coord width);
    void setChosenTracksSide(BoardSide side);
    // Types the width of the tracks chosen in place, where the menu was
    // opened.
    void editMenuTrackWidth();
    // The diameter of every hole chosen; 0 when they differ or none is.
    Coord chosenHolesDrill() const;
    // The holes chosen get the diameter 'drill' as one step.
    void setChosenHolesDrill(Coord drill);
    // Types the diameter of the holes chosen in place, over the hole the
    // menu was opened on.
    void editMenuHoleDrill() { editHoleValue(Typed::Drill); }
    // Every hole chosen is plated (areHolesPlated()); made plated or bare as
    // one step.
    bool chosenHolesPlated() const;
    // Every hole chosen is locked: its diameter, plating and ring stay.
    bool chosenHolesLocked() const;
    // Every hole chosen has a copper pad, and none of them is plated.
    bool chosenHolesHavePads() const;
    bool chosenHolesPlatedNone() const;
    // The sides of the pads of the holes chosen, or the net of the pads,
    // when all of them have the same; nothing otherwise.
    std::optional<CopperSides> chosenHolesPadSides() const;
    std::optional<QString> chosenHolesNet() const;
    // Given to the holes chosen as one step (withHolesPadSides(),
    // withHolesNet()).
    void setChosenHolesPadSides(CopperSides sides);
    void setChosenHolesNet(const QString &net);
    void setChosenHolesPlated(bool plated);
    // Types the diameter of the copper pad of the holes chosen in place; 0
    // takes it off, except from the plated ones.
    void editMenuHolePad() { editHoleValue(Typed::Pad); }
    // Every placement chosen shows its value; shown or hidden as one step.
    bool chosenValuesShown() const;
    void setChosenValuesShown(bool shown);
    // Every placement chosen has the lines from its texts to it
    // (Placement::refLine, valueLine); set or taken off as one step.
    bool chosenLined() const;
    void setChosenLined(bool lined);

    // The left mouse button; with 'adding' (Ctrl) a placement clicked is
    // added to what is chosen or taken away, and a frame adds what it holds.
    // A second click in a row is a press as well; in choosing, a corner
    // under it gets its radius typed and a side under it chooses the
    // outline whole. With 'joined' (Shift) a track or a via clicked is
    // chosen with all the copper joined to it (joinedCopper()).
    void press(const QPointF &at, bool adding = false, bool joined = false);
    void move(const QPointF &at);
    void release(const QPointF &at);
    void doubleClick(const QPointF &at);
    // Where the cursor is, on the grid: the side being drawn follows it.
    void hover(const Point &at);
    // The mouse over the board with no button held: the corner or the side
    // under it is lit.
    void hoverMouse(const QPointF &at);
    // Esc: a drag is called off, or the outline or the cut-out being drawn
    // is dropped, or the outline chosen whole; else drawing ends and
    // choosing comes back.
    void cancel();

    // What the canvas shows: the board as a drag would leave it.
    Board shownBoard() const;
    // The frame drawn to choose placements; empty when none is.
    QRectF chooserFrame() const { return m_framing ? m_frame : QRectF(); }
    // The outline being drawn up to the cursor; empty when none is.
    QPolygonF drawnOutline() const;
    // The cut-out being drawn; empty when none is.
    QRectF cutRect() const;
    // Where the next corner of the outline or of the cut-out goes, or where
    // a dragged corner is, so that its coordinates can be shown: the cursor
    // while drawing, from before the first click on; the dragged corner, or
    // the end of a dragged side nearer to where it was taken; nothing
    // otherwise.
    std::optional<Point> drawnPoint() const;
    // The corners are marked while choosing; one of them or a side is lit
    // when under the mouse or dragged: an index, or -1.
    bool showsCorners() const
    {
        return m_active && m_tool == Tool::Select && !isOutlineLocked();
    }
    int litCorner() const { return m_litCorner; }
    int litSide() const { return m_litSide; }

signals:
    // What the canvas shows of the editing changed: redraw.
    void changed();
    // Why something could not be done, for the status bar.
    void message(const QString &text);

private:
    // What the line typed in place is for.
    enum class Typed { None, Radius, Drill, Pad, TrackWidth };

    const QList<OutlineCorner> &outline() const { return m_document->design().board.outline; }
    int boardPage() const;
    // Makes 'outline' the board's as one step named after 'action'.
    void apply(Action action, const QList<OutlineCorner> &outline);
    // A click while drawing the outline, at the grid point 'at'.
    void outlineClick(const Point &at);
    void cutOut(const Point &a, const Point &b);
    // Opens the line to type the radius of 'corner' in.
    void editRadius(int corner);
    void radiusTyped(const QString &text);
    // What the line typed holds, in millimetres, as the diameter of the
    // holes chosen or of their pads; refused in the status bar when it is
    // not a size, or is 0 for a hole.
    void holeValueTyped(Typed typed, const QString &text);
    // Says in the status bar that the outline is locked; true when it is.
    bool refuseLocked();
    // Says in the status bar that the holes chosen are locked, when holes
    // are chosen and all of them are; true then.
    bool refuseHolesLocked();
    // Makes 'board' the board as one step named after 'action', with the
    // placements 'ids' as those it touched.
    void applyBoard(Action action, const Board &board, const QSet<ObjectId> &ids);
    // A click while laying tracks at 'at' (millimetres): starts a track, or
    // lays the pieces up to it.
    void trackClick(const QPointF &at);
    // Where the track being laid goes from its last point when the mouse
    // is at 'at': the middle of the copper under it on the side, else the
    // grid point.
    Point trackTarget(const QPointF &at) const;
    // The pieces from the last point to 'target'.
    QList<Point> trackPieces(const Point &target) const;
    // Lays the pieces to 'target' as tracks, and 'via' with them when
    // given, as one step named after 'action'.
    void layTrack(const Point &target, Action action, const std::optional<Via> &via);
    // A press in choosing on a placement's text, a hole or a placement;
    // false when there is none of them under 'at'.
    bool pressChoosable(const QPointF &at, bool adding, bool joined);
    // The hole under 'at', as the mouse reaches.
    ObjectId holeUnder(const QPointF &at) const;
    // The text of a placement, and the placement, under 'at', as far as
    // what is shown lets them be taken (setSilkscreenShown()).
    PlacementText textUnder(const QPointF &at) const;
    ObjectId placementUnder(const QPointF &at) const;
    // Opens the line over the hole the menu was opened on.
    void editHoleValue(Typed typed);
    void valueTyped(const QString &text);
    // Places a hole of holeDrill() at 'at', unless one stands there.
    void placeHole(const Point &at);
    // The board as a drag of placements or of a text to 'at' leaves it.
    Board draggedBoard(const QPointF &at) const;
    // Drops what is chosen: the outline, the placements, the text.
    void dropChosen();
    // Chooses the outline whole, and nothing else.
    void chooseOutline();
    // Drops a drag and what is lit: the board changed under them.
    void dropDrag();
    void setLit(int corner, int side);

    Document *m_document;
    CanvasView *m_canvas;
    bool m_active = false;
    Tool m_tool = Tool::Select;

    // Drawing the outline: the corners set so far, and the cursor.
    QList<Point> m_outlinePoints;
    Point m_cursor;

    // Cutting out: the rectangle's corners while the button is held.
    bool m_cutting = false;
    Point m_cutFrom;
    Point m_cutTo;

    // Choosing: the press, what it took, and the outline as dragged so far.
    bool m_pressed = false;
    bool m_dragging = false;
    QPointF m_pressAt;
    Point m_pressGrid;
    int m_dragCorner = -1;
    int m_dragSide = -1;
    // The side was taken nearer to its end corner than to its start.
    bool m_dragSideEnd = false;
    // The press took the outline chosen whole: a drag moves it all.
    bool m_dragWhole = false;
    // A drag of the locked outline was refused once since the press.
    bool m_lockTold = false;
    bool m_outlineChosen = false;
    std::optional<QList<OutlineCorner>> m_preview;

    // The placements and the holes chosen, and a text chosen alone.
    QSet<ObjectId> m_chosen;
    PlacementText m_chosenText;
    // The press took a placement chosen, or a text: a drag moves them from
    // 'm_dragAnchor', the point of the one taken, which lands on the grid.
    bool m_dragPlacements = false;
    bool m_dragText = false;
    // A piece of track taken: dragged across itself, or by its corner at
    // its start (0) or its end (1); -1 for the piece.
    ObjectId m_dragTrack;
    int m_dragTrackCorner = -1;
    // The board with the piece or the corner taken where 'at' puts it.
    Board trackDragged(const QPointF &at) const;
    Point m_dragAnchor;
    std::optional<Board> m_movedBoard;
    // The board the drag starts from, with the turns made while dragging,
    // and where the mouse was last.
    Board m_dragBase;
    QPointF m_lastAt;
    // The press was on the empty board: a drag draws a frame choosing the
    // placements inside it, added to those chosen with 'm_frameAdding'.
    bool m_framing = false;
    bool m_frameAdding = false;
    QRectF m_frame;
    bool m_pressEmpty = false;

    int m_litCorner = -1;
    int m_litSide = -1;

    // The line typed in place: a corner's radius, the diameter of the holes
    // chosen or of their pads.
    InPlaceEdit m_valueEdit;
    Typed m_typed = Typed::None;
    int m_radiusCorner = -1;
    int m_menuCorner = -1;
    // The hole the menu was opened on, and where it was opened.
    ObjectId m_menuHole;
    QPointF m_menuAt;

    // The net under the mouse, lit once the mouse has stayed on it a while
    // (CanvasView::HOVER_DELAY_MS), and its name shown beside the cursor.
    QTimer m_hoverTimer;
    QString m_pendingNet;
    QString m_hoveredNet;
    // Lights the net under 'at' after a while; one lit goes out off it.
    void hoverNet(const QPointF &at);
    void setHoveredNet(const QString &net);

    Coord m_holeDrill = DEFAULT_HOLE_DRILL;
    bool m_silkscreenShown = true;
    bool m_tracksShown = true;

    // Laying tracks: the last point laid (none before the first click),
    // the net, the width and the side of the track, its corner, and where
    // the mouse is.
    std::optional<Point> m_trackFrom;
    QString m_trackNet;
    Coord m_trackWidth = 0;
    BoardSide m_trackSide = BoardSide::Top;
    bool m_diagonalFirst = false;
    QPointF m_mouseAt;
    // The points the track being laid went through, the start first, each
    // with the side it goes on from and the project as it was there. An
    // Undo gives back the project of the point before, whatever the limit
    // of the history, so that is how it is told.
    struct LaidPoint
    {
        Point at;
        BoardSide side = BoardSide::Top;
        Design design;
    };
    QList<LaidPoint> m_laid;
    // A step of laying is being made: the change is its own.
    bool m_layingStep = false;
    // After a click or V laid a step: the point and the side laying goes on
    // from are remembered, when the step changed the project.
    void noteLaid();
    // The project changed while laying: an Undo steps back to the point
    // before; back past the start, or a Redo or another change, ends laying.
    void laidChanged();
    // The steps that put a via and went to the other side: the project and
    // the side before and after each, the newest last.
    struct ViaStep
    {
        Design before;
        Design after;
        BoardSide sideBefore = BoardSide::Top;
        BoardSide sideAfter = BoardSide::Top;
    };
    QList<ViaStep> m_viaSteps;
    // The project changed by Undo or Redo to one before or after a via
    // step: the side is the one it had there.
    void sideFollowed();
};

} // namespace ppcb
