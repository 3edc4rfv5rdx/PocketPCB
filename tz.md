# Specification: PocketPCB — a small schematic and PCB editor

Draft, started 2026-11-12, updated 2026-11-13. Will be extended. This is the English translation of `tz.ru.md`; the Russian version is the original.

## Goal and scope

A small, simple editor for schematic diagrams and double-sided printed circuit boards with manual routing. Not a KiCad competitor: minimum features, maximum simplicity.

- A few built-in schematic symbols and footprints (not a large library).
- Double-sided boards, manual routing, no autorouter.
- Required output: Gerber + drill files for ordering boards from a manufacturer, and 1:1 printing of layers for making boards at home (laser toner transfer, photoresist).
- Everything postponed to later versions is collected in the "Postponed" section. For those features, the data model and file format are prepared up front where possible, so nothing needs to be redesigned later.

## Decisions

- Schematic and board are one program with a shared data model (2026-11-12).
- **Project files** (2026-11-13), all JSON, in one project folder:
    - `name.ppcb` — project: properties and title block fields, net classes, design rules, grid settings; references to the schematic and board files;
    - `name.ppsch` — schematic (all sheets);
    - `name.ppbrd` — board.
  The user opens the `.ppcb` file; the schematic and board are loaded with it. "Save" saves all modified files at once. Autosave and `.bak` backups apply to each file.
- The parts library is a set of `.json` files in the folders `symbols/`, `footprints/`, `parts/`, written by hand (2026-11-12). There are two libraries: built-in (shipped with the program, read-only, inside the AppImage) and user (in the user's data folder, located via `QStandardPaths`). Both are loaded; the user library can override built-in parts.
- One project = one schematic (possibly several sheets) + one board. A separate module (e.g. a board that plugs into the main board) is a separate project. The parts library is shared by all projects (2026-11-13).
- Language: C++ (Free Pascal/Lazarus was considered — the author knows it better — but C++/Qt6 was chosen for the ready-made `QGraphicsScene` canvas) (2026-11-12).
- Code style: simple and readable. No template magic, complex metaprogramming or exotic C++ features. Clear names, small classes and functions — the code should be understandable to someone with a Pascal background.
- All code and comments are in English. The specification is written in Russian and translated to English. Communication with the author is in Russian (2026-11-12).
- The user interface is multilingual: English, Ukrainian, Russian. The language is selected in settings; the default is the system language (if it is one of these three, otherwise English). Implemented with standard Qt tools (`tr()`, Qt Linguist, .ts/.qm files). All UI strings in the code are in English and wrapped in `tr()` (2026-11-12).
- First-version parts are through-hole (THT) only; the list is in "Parts library". The data model allows SMD from the start: a pad may have no hole and exist on one layer only, and a part may be placed on the bottom side (mirrored) (2026-11-12).
- Name: **PocketPCB**; executable and repository name: `pocketpcb` (2026-11-13).
- Open source, license GPL-3.0-or-later. The code will be published on GitHub (when is up to the author; until then a local git repository). Dependency compatibility: Qt6 (LGPLv3) — yes, Clipper2 (Boost) — yes, Newstroke stroke font from KiCad (GPL-2.0-or-later, drawn after the Hershey fonts) — yes (2026-11-13, font changed 2026-11-19).
- All files are JSON, read and written with Qt (`QJsonDocument`), in a consistent style (2026-11-12).
- Connectivity from the first version: schematic → netlist → ratsnest on the board. The data model is built around nets from the start (2026-11-12).

## Platform and stack

C++ and Qt6, built with CMake. Development on Linux. Target OS: Linux now, Windows 10+ later. The code is portable from the start, with no platform-specific calls (paths via `QDir`/`QStandardPaths`, no hard-coded separators), so a Windows build will not require rework later.

- **Build and distribution (2026-11-13):** for now everything is built locally on the author's machine (Linux Mint 22.3, based on Ubuntu 24.04 noble), without GitHub Actions. The output is an AppImage. Qt is not built statically: the Qt libraries and plugins are bundled inside the AppImage (linuxdeploy + linuxdeploy-plugin-qt). The AppImage runs on systems based on Ubuntu 24.04 and newer; older systems (e.g. Mint 21) are not supported in the first version.
- The repository contains an AppImage build script (one command: build, test, package), so the process is repeatable and easy to move to GitHub Actions later.
- **Qt version:** the one in the Ubuntu 24.04 repositories (package `qt6-base-dev`; from memory Qt 6.4 — check the exact version with `apt policy qt6-base-dev`). The code does not use features newer than this version.

- Editor canvas on `QGraphicsScene` / `QGraphicsView`: zoom, pan, selection, layers.
- Undo/Redo via `QUndoStack` from day one.
- Geometry: own simple distance functions (segment, circle, rectangle). Clipper2 is added together with copper fills.

## Units and grid

Internally, all coordinates and sizes are stored as integers in nanometres (`int64`), not `double`. Reasons: no accumulation of rounding errors, exact coordinate comparison, and both metric and imperial values are whole numbers in nanometres (1 mil = 25,400 nm). In all JSON files (project, schematic, board, library) sizes are written in millimetres as decimal numbers (e.g. `2.54`) so they are easy to read and write by hand; on load they are converted to nanometres with rounding.

- The grid is imperial, matching the pin pitch of DIP packages and pin headers. Grid steps: 0.635, 1.27, **2.54 (default)**, 5.08, 10.16 mm. The step is changed with hotkeys and from a drop-down list (the set of larger steps is preliminary).
- All sizes in the UI are shown in millimetres.

## Parts library

Three kinds of objects, each in its own JSON file:

1. **Symbol:** graphics (lines, rectangles, circles, arcs, text) and pins. Each pin has a number, name, position and direction (the side the wire connects from). Pins sit on the 2.54 mm grid.
2. **Footprint:** pads (number, shape, size, hole, layers) and a body outline for the silkscreen.
3. **Part:** links a symbol to one or more allowed footprints and sets the reference designator prefix (R, C, Q, U …), the default value and the name for the library panel in every interface language (`"name": {"en": …, "uk": …, "ru": …}`; without a translation the English name is shown, without that the file name) (2026-11-14). Connectors and wire pads have a "connector" flag (used by ERC). Pins are mapped to pads by number (pin 1 → pad 1).

In the first version an IC is a single symbol with all its pins (2026-11-13).

Symbols and footprints are separate because one part can come in different packages (a resistor with 7.62 or 10.16 mm pitch). The footprint is chosen when the part is placed on the schematic and can be changed in its properties.

**Parts are copied into the project** (2026-11-13): when a part is placed, the part, its symbol and its footprint are copied into the `library/` folder beside the project file (in the same `symbols/`, `footprints/`, `parts/` layout), one copy per part however many times it is placed. A component in the schematic file keeps only a reference to the part and the chosen footprint, its designator, value and position (2026-11-16). The project does not depend on the library: it opens exactly as it was saved, even if the part was changed or removed from the library. The "Update parts from library" command shows the differences and updates the selected parts (as one Undo step).

**Reference designator prefixes:** every part in the library has two prefixes — international (R, C, D, LED, Q, U, J, SW, BT, LS …) and GOST, the Russian/Soviet drafting standard (R, C, VD, HL, VT, DD, X, SA, GB, BA …). The style is selected in the project settings, international by default. Switching renames the designators in the project (numbers are kept; one Undo step).

**First-version library contents** (through-hole parts only):

- passive: resistor, variable resistor (potentiometer), trimmer resistor, capacitor, electrolytic capacitor, inductor, crystal (HC-49), fuse;
- semiconductors: diode, LED, transistors (TO-92, TO-220), thyristor (TO-220), ICs DIP-8 / DIP-14 / DIP-16;
- switching: toggle switch, push button, rotary switch (several position/pole variants);
- sound and power: cell / battery, speaker, buzzer;
- connectors: 1×N pin header, screw terminal blocks (2 and 3 contacts), DC power jack, audio jack (3.5 / 6.3 mm), wire pad.

**Panel-mount parts** (2026-11-13) — DC power jack, audio jack, rotary switch, toggle switch, potentiometer, speaker, battery — have only a symbol in the library, no footprint, and are "off-board" by default. They are connected to the board with wires via wire pads. No footprints are drawn for them, so there is no need to check their dimensions against datasheets.

The "off-board" flag can be turned on for **any** part on the schematic — for example a power transistor, diode or thyristor on a heatsink, or a large power resistor or capacitor mounted on the enclosure. It can be turned off only for a part that has a footprint. Parts with footprints: resistors, trimmer resistor, capacitors, inductor, crystal, fuse, diode, LED, transistor (TO-92, TO-220), thyristor (TO-220), DIP ICs, push button, buzzer, pin header, screw terminal blocks (5.0 / 5.08 mm pitch), wire pad.

## Schematic editor

The schematic uses the same imperial grid; symbol pins always sit on the 2.54 mm grid.

- **Sheets:** a schematic can have several sheets (flat structure, no hierarchy). Sheets are switched with tabs; they can be added, deleted, renamed and reordered. Each sheet has its own paper size (A4/A3) and its own title in the title block. Reference designators are unique across the whole project (R1…Rn, no duplicates between sheets).
- **Symbol style:** GOST / IEC 60617 (resistor is a rectangle, inductor per GOST, logic gates are rectangles with a function symbol `&`, `1`, `=1`). No American (ANSI) symbols. Exact GOST dimensions are not required — what matters is the look of the symbols.
- **Wires:** horizontal, vertical and at 45°. A wire is a polyline of segments.
- **Connections:** a wire connects to a pin or another wire only if its end lands exactly on the point. A junction dot is placed automatically at a T-junction.
- **Net labels:** wires with the same label are connected even with no wire between them — across all sheets of the project. A label has a scope attribute "global" / "local" in the data model from the start; only global labels work in the first version.
- **Power and ground symbols** (GND, +5V, +3.3V …): a special kind of net label — all symbols with the same name are connected across the whole project.
- **Designators and values:** every part has a reference designator (R1, C3, U2) and a value (10k, 100n, NE555). A new part gets the next free number of its type. There is a renumber command (left to right, top to bottom). The designator and value are shown on the schematic and on the board silkscreen and can be moved with the mouse.
- **Nets without a label** get an automatic name (e.g. `N1`, `N2` …); the name is shown in the net list and on hover.
- **Buses:** a bus is a graphic object with a name like `D[0..7]`. It connects nothing by itself; a wire connects to a bus through a bus entry with a label (`D0`, `D1` …), and matching labels form the connection. This way the netlist is built by one rule — by labels.

- **Free graphics:** text (comments), lines and rectangles (solid and dashed — e.g. frames around functional blocks). They do not affect connectivity.
- **Off-board parts:** a part has an "off-board" flag (a potentiometer on the front panel, a switch, a panel jack, etc.). Such a part is on the schematic and in the BOM but is not transferred to the board and needs no footprint. It is connected to the board via a connector or wire pads (contact parts) on the schematic (2026-11-13):
    - contacts can be placed by hand on the wire going to the part;
    - the **"Place contacts"** command automatically places a contact at each pin of an off-board part whose net goes to the board (contains on-board parts) and has no contact yet. Pins connected only to other off-board parts (e.g. battery → toggle switch) are skipped. Afterwards contacts can be moved, deleted and added by hand; the command is one Undo step;
    - on the board, each wire pad always shows a label on screen saying where it goes (e.g. `R5.2`, `J1.1`); the label is printed on the silkscreen optionally.

## Schematic check (ERC)

Run by a command; the result is a list, and clicking a row jumps to the location on the schematic.

- Warnings: part pin not connected to anything; net with a single pin; wire with a dangling end; duplicate reference designators; pin of an off-board part connected to a net that has no connector (connector or wire pad).
- Error: part without a footprint (except off-board parts).
- A "no connect" marker (cross) on a pin suppresses the unconnected-pin warning.
- No pin type checks (input/output/power).

## Main window

- Centre: the canvas; above it, tabs for the schematic sheets and the board.
- Left: toolbar of the current mode (wire, label, track, via …).
- Right: collapsible panels: properties of the selection; parts library (search and preview); layers (board); net list (with net classes).
- Bottom: status bar with cursor coordinates (mm), grid step, current layer and track width.
- Panel layout and sizes are remembered between sessions (`QDockWidget`, `QSettings`).

## Navigation (schematic and board)

- **Pan:** move the mouse with the right button held. A short right click without movement opens the context menu. The middle button also pans.
- **Zoom:** mouse wheel. On each wheel step the point under the cursor is moved to the centre of the screen, the zoom changes around it, and the mouse cursor is also moved to the centre of the screen so it stays over the same point of the drawing (like the "Center and warp cursor on zoom" option in KiCad). A setting switches to the ordinary mode, where the point under the cursor stays in place and nothing is centred.
- **Hotkeys:** based on the KiCad layout (as a common standard); the exact list is checked against the KiCad documentation during implementation. All keys can be reassigned in settings (via `QAction`).

## Editing (schematic and board)

- **Selection:** click — one object; Shift + click — add to / remove from selection; rubber-band rectangle — everything inside it; Ctrl+A — everything on the sheet/board. Any number of objects can be selected.
- **Operations on the selection (one object or a group):** move, rotate by 90° (a group rotates around its centre), mirror, copy and paste (including between schematic sheets), delete. All operations can be undone.
- **Drag with connections (required):** when a part is moved, connected wires (schematic) and tracks (board) stay attached and follow it. This is the default mode. A separate key moves without connections (wires/tracks stay in place and are disconnected).
    - On the schematic the wire is rerouted to keep horizontal/vertical/45° segments (a bend is added if needed).
    - On the board the last track segment at the pad is stretched (it may end up at any angle); clearance violations are reported by DRC. There is no smart track rerouting.

## Schematic–board synchronisation

One direction only: schematic → board.

- The "Update board from schematic" command is run manually; a list of changes is shown before applying. The whole update is one Undo step.
- New parts appear on the board next to the board outline.
- Parts removed from the schematic are removed from the board together with connected tracks (with confirmation).
- Connectivity changes update the ratsnest; tracks that now connect the wrong things are highlighted as errors but not deleted.

## Board editor

Two copper layers (top and bottom), manual routing on the grid.

- **Layers:** copper top/bottom, solder mask top/bottom, silkscreen top/bottom, board outline.
- **Track:** segments with a width and round ends. The file format allows arcs in tracks from the start.
- **Routing:** angles — horizontal, vertical, 45°; with a held key — any angle. While routing, the via key places a via and switches the track to the other layer. The track snaps to the centre of a pad when the cursor is near. While routing, all pads of the same net are highlighted.
- **Pads:** circle, rectangle, oval; with a hole (or without — for future SMD).
- **Via:** a circle with a hole.
- **Board outline:** a closed polygon of horizontal, vertical and 45° segments — this gives rectangular notches on the edge and chamfers. Drawn with the same tool as tracks, on the grid. For a quick start there is a "rectangular board" command (width, height). Any outline corner can be rounded (radius set per corner, 0 — no rounding).
- **Mounting holes:** round, any diameter, anywhere on the board, any number. A "plated" flag (off by default); when on, a copper ring surrounds the hole (width configurable) and can be connected to a net (e.g. GND).
- **Silkscreen:**
    - part body outlines come from the library footprints;
    - reference designators are shown by default; values are hidden and can be turned on per part or for all parts;
    - custom text (board name, version, date) and simple lines can be added;
    - on the bottom side it is mirrored (for bottom-side parts and text);
    - text supports Latin and Cyrillic;
    - the font is a built-in stroke font (letters made of lines of a set width), identical on screen, in print and in Gerber, on any computer. The Newstroke font from KiCad is chosen (2026-11-19, instead of Hershey): it is drawn after Hershey and has Latin, Cyrillic with the Ukrainian letters, Greek and the signs ± ° µ Ω in one style.

## Design rules and DRC

- **DRC** checks clearances between tracks, pads, vias and the board edge, track widths and pad annular rings.
- **The level of each check** is configurable: error / warning / off. Copper of different nets touching or overlapping (a short circuit) is always an error and cannot be turned off. Defaults: clearance violation — error; annular ring, track narrower than its net class, board edge clearance — warnings. The author decides about minor issues.
- **Solder mask:** the mask opening is 0.1 mm larger than the pad on each side (a rule value). Vias are covered (tented) by default; a project rule switches between "tented / open".
- **Rules** are stored in the project and edited in the settings dialog.
- **Rule sets:** a "Manufacturing" / "Toner transfer" switch. After switching, individual values can be adjusted by hand. Existing tracks and vias are not changed when switching — DRC simply checks them against the new rules.

| Parameter | Manufacturing (default) | Toner transfer |
| --- | --- | --- |
| Track widths to choose from, mm | 0.3, 0.5, 0.8, 1.0, 1.5 | 0.5, 0.8, 1.0, 1.5, 2.0 |
| Default width, mm | 0.5 | 0.8 |
| Copper-to-copper clearance, mm | 0.3 | 0.5 |
| Via: drill / pad, mm | 0.6 / 1.2 | 0.8 / 2.0 (for a soldered wire through the via) |
| Minimum pad annular ring, mm | 0.3 | 0.6 |
| Clearance to board edge, mm | 0.5 | 1.0 |

The "Manufacturing" values are conservative, not tied to a specific manufacturer.

- **Net classes (track types):** each class has a name, track width, clearance and via size. For example: "Signal" — 0.5 mm, "Power" — 1.5 mm, "HighCurrent" — 5 mm. Classes are created and edited in the project settings dialog.
    - Every net belongs to one class; the default is "Signal".
    - The class is assigned in the net table (a list of all nets with a class drop-down) only; a net named by a label keeps its class by that name.
    - Nets of power and ground symbols (GND, +5V …) go to the "Power" class by default.
    - When routing, the track width is taken from its net class automatically; it can be changed by hand for an individual segment.
    - Clearance violation — error. A track narrower than its class — **warning** (sometimes a track is deliberately narrowed at a pin). A track narrower than the minimum width of the rule set — error.

## Printing for toner transfer and photoresist

Default sizes and rules target manufacturing (Gerber), but a board can also be made at home — for that there is layer printing.

- **1:1 printing** to a laser printer and to PDF, exactly to scale (with a test ruler on the page to check the printer's scale).
- Layer selection: copper top / copper bottom. The top layer is printed **mirrored**, the bottom **not mirrored** (can be overridden by hand).
- Copper black, everything else white; an invert option (for photoresist).
- **Drill centre marks:** a small hole is printed in every pad and via (diameter configurable, 0.5 mm by default) so the drill does not wander when drilling by hand.
- Layer alignment marks for double-sided boards: printed outside the outline on both layers.
- The board outline is printed as a thin line.
- Several copies can be printed on one page.

## Schematic documents

- **Schematic printing** to a printer and to PDF; paper size A4/A3, orientation, "fit to page" scaling.
- A **frame** around the drawing area and a **simplified title block** in the bottom-right corner. Not GOST-compliant, just a neat table. Fields come from the project properties (edited in a dialog): project name, sheet title, author, organisation, contacts (free text: e-mail, website, etc.), revision, date, "sheet N of M", notes. Author, organisation and contacts default to values from the program settings and are filled into every new project; they can be changed in the project. The date defaults to the last save date and can be set by hand. Empty fields are not printed.
- The frame and title block are visible while editing the schematic, not only in print.
- **Bill of materials (BOM):** a table "designators, value, package, quantity", identical parts grouped (R1, R5, R7 — 10k — 3 pcs). Export to CSV.

## Export

- Gerber RS-274X — one file per layer.
- Drill — Excellon, two files: plated holes (PTH) and non-plated holes (NPTH).
- Output is checked in gerbv or in the board manufacturer's online viewer.

## Data safety

- **Autosave** at a set interval (5 minutes by default): for each modified project file, a separate copy next to it (the original files are not overwritten). After a crash, recovery is offered when the project is opened.
- **Backups:** on every save the previous version of the file is kept as `.bak`; several recent ones are kept (3 by default, configurable).
- A list of **recent projects** in the File menu.
- When closing with unsaved changes — a prompt to save.

## Automated tests

- **Architecture requirement:** the data model and all logic (geometry, netlist, DRC/ERC, file reading/writing, export) live in a separate library that does not depend on QtWidgets (QtCore/QtGui only). The GUI is a thin layer on top of it. This way the logic can be tested without windows.
- **Tool:** Qt Test (part of Qt, no extra dependencies), run with CTest (`ctest`) locally; the AppImage build script runs the tests before packaging.
- **What is tested:**
    - geometry: segment–segment, segment–circle, rectangle distances, etc.; mm ↔ nm conversion and rounding;
    - netlist construction on small test schematics: wires, T-junctions, labels, power symbols, buses, multiple sheets;
    - DRC and ERC: test boards and schematics with known violations;
    - files: save → load → compare (nothing lost); loading files of older format versions;
    - Gerber and Excellon: "golden files" — the output for a test board is checked by hand in gerbv once and committed to the repository; afterwards the test compares new output with the golden file;
    - Undo/Redo: action → undo → state equals the original;
    - schematic → board synchronisation.
- **No GUI tests in the first version**, except a check that the program starts and opens a test project (headless mode, `QT_QPA_PLATFORM=offscreen`).
- **Development rule:** every new logic feature comes with tests; when fixing a bug, a test reproducing it is written first.
- **Format version:** all JSON files (`.ppcb`, `.ppsch`, `.ppbrd`, library) have a format version field, so newer program versions can read older files.

## Development order (preliminary)

0. Skeleton: git repository, CMake, split into logic library and GUI, empty window with a canvas (grid, zoom, pan), first tests, translations (`tr()` from day one).
1. Data model (parts, pins, nets), Undo/Redo, saving and loading files.
2. Library: JSON format and the first parts.
3. Schematic editor: placing symbols, wires, labels, building nets; ERC.
4. Board editor: outline, footprints, synchronisation with the schematic, ratsnest, tracks, vias; DRC.
5. Gerber + Excellon export.
6. Printing for toner transfer (1:1, mirroring, drill centre marks).
7. Schematic documents: print/PDF with frame and title block, BOM.
8. AppImage build script, first release.

## Postponed

Not part of the first version. "Groundwork" is what is already done in the data model and file format.

| Feature | Groundwork in the first version |
| --- | --- |
| SMD parts and solder paste layers; "flip part to the other side" command | Pad without a hole, on one layer; part on the bottom side |
| Built-in symbol and footprint editor | Library in JSON |
| Copper fills (zones, clearances, thermal reliefs), Clipper2 | "Fill zone" object (outline, layer, net, clearance) in the model |
| Rounded track corners ("sharp / rounded" switch, radius) | Arcs in tracks allowed by the file format |
| Local net labels (within one sheet) | "Global / local" attribute on labels |
| Internal cut-outs (windows) in the board | — |
| Back-annotation (board → schematic) | — |
| mm / mil unit switch | Storage in nanometres |
| System TrueType/OpenType fonts on the board (undecided whether needed) | — |
| Automated build and tests on GitHub Actions | Build script in the repository |
| Windows version: MinGW build (on GitHub Actions), `windeployqt`, zip archive, later an installer; checked under Wine (suspicious behaviour should be confirmed on real Windows — Wine has its own bugs) | Portable code |
| Hierarchical schematic sheets | — |
| Showing off-board parts in the board editor: placeholder rectangles with designator and pins outside the board edge, dashed "wires" to the pads; on screen only, not in Gerber or print | Labels on wire pads |
| Multi-unit ICs (U1A…U1D + separate power pins) | — |

## Open questions

None at the moment.
