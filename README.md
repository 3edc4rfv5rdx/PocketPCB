# PocketPCB

A small, simple editor for schematics and double-sided printed circuit boards with manual routing. The aim is a board you can order from a factory (Gerber and drill files) or make at home by toner transfer (1:1 layer printing). It is not a KiCad competitor: few features, kept simple.

Written in C++ with Qt 6. Linux first; the code is kept portable. Text is drawn in a built-in stroke font, Newstroke from KiCad, the same on any computer. The interface is in English, Ukrainian and Russian, following the system or chosen in Settings > Language (taken at the next start).

## Status

Early development. What works now:

- Projects: the project folders lie in a common PocketPCB-Projects folder beside the program (beside the AppImage file), where saving and opening start until another place is used; Settings > Projects folder chooses another. Each project lives in a folder of its own: `amp/amp.ppcb` (project), `amp.ppsch` (schematic), `amp.ppbrd` (board), all JSON. Saving keeps backup copies in `backup/`, the open project is autosaved to `autosave/` and offered back after a crash, and you are asked before unsaved changes are lost; the bottom left corner of the window shows in colour whether the project is saved, and a click on it saves. The File menu lists the recent projects. How each page was last looked at (zoom and position) is kept in a hidden `.ppview` beside the project file.
- Project properties (File > Project properties): the fields of the title block (an empty date prints the day of the last save), and the designator style, international (R, D, Q, U) or GOST (R, VD, VT, DD); a new style renames the parts as one step, numbers kept, a designator that would repeat getting the first free number; the net classes with their track width, clearance and via, added, renamed (everything giving the class follows), deleted (its nets go to the default or the power class, asked first), and which is the default class and the power class.
- Library: a built-in library and your own (`~/.local/share/PocketPCB/library`), plus the library of the open project in `amp/library/`. Parts, symbols and footprints are JSON files you can copy by hand. A project keeps copies of the parts it uses, so it opens the same even when the library changes. File > Update parts from library shows which of them differ from the library (fields, symbol, moved pins, footprints) and takes those ticked as one step; wires stay where they are, components with no rated power get the part's.
- Built-in parts: resistors, capacitors, an inductor, a trimmer, diodes and LEDs, bipolar, field-effect and MOS transistors, a thyristor, DIP chips, a crystal, a fuse, a button, a buzzer, pin headers, screw terminals, a wire pad, and panel parts (jacks, switches, a potentiometer, a speaker, a battery). Symbols follow GOST/IEC style.
- Library panel: search, parts grouped by designator prefix (international or GOST) after a group of power symbols, a drawing of the symbol and footprint; a double click on a part on the sheet shows its part and footprint there.
- Power symbols are library symbols with a net name: a bar, an arrow, a ground and a chassis are built in, and your own are added as files. Symbols with one name are one net, whatever they look like.
- Sheets: tabs over the canvas, the board last. A sheet is added with "+" or from a tab's right-click menu, renamed with a double click on its tab, moved by dragging its tab; the menu also chooses A4 or A3 paper and deletes a sheet, asking first when something is on it; View > Sheet does the same for the sheet shown.
- Schematic sheet:
  - the colour of the paper is chosen from a table of pale ones in View > Sheet background, the sheet taking the colour under the mouse as a preview; it is kept with the program, not in the project;
  - choose with a click or a rectangle (a part needs only its drawing inside, not its texts); a click on one of several chosen objects keeps it alone; what is chosen stays chosen after a change;
  - drag along the grid with the wires following (Shift tears them off), rotate and mirror with the wires following too, delete, copy, cut, paste and duplicate (pasted parts are numbered on); the wires are laid again by the cheapest way round bodies, pins and other wires (Settings > Wire routing > Around obstacles, the default) or by trying a few ways with one or two bends (Simple); Settings > Keep a history of steps writes each step, with the schematic before and after it, into `~/.cache/PocketPCB/history/` (the newest 100), to show what went wrong in one;
  - draw wires (W; Space turns the corner, / or . slants it) with junction dots placed by themselves;
  - place power symbols from the panel, or with P, which places the last one again (the ground at first); wires join them like pins, and a double click edits the net name in place;
  - place net labels with L: a click asks for the text, the next label is offered numbered on (D0, D1, …); a label on no wire and no pin shows white on a red plate, one on a wire or a pin goes with it when that moves; a click on its text chooses the text alone and a drag moves it away from the point, which stays and shows as a small hollow diamond; a double click edits its text;
  - draw buses with B, the way wires are drawn: each is named BUS1, BUS2 … at first, and a double click renames it (D[0..7]); it is dragged by its segments and ends like a wire and chosen whole with a double click, the wires entering it going along; its name is chosen and dragged alone, like a part's designator; a bus drawn to another bus ends there and branches off it, with its name and a dot at the T, going along when that bus is dragged; Shift and a click choose a bus with its branches; a bus shows its name wherever "Add name here" in its menu puts it, each place dragged or deleted alone, the last of a bus and its branches kept; a wire drawn to a bus ends on it, entering it at 45 degrees over its last grid step, and joins the others by its label;
  - put no-connect marks on pins with X: a mark goes only on a pin of a part, is red elsewhere, goes with the pin when the part moves, and is deleted with the part;
  - a click on a wire chooses its segment: a drag moves it across, Delete removes it, and taken near the wire's end it drags that end, unless the end is on a pin; wires drawn on from each other end to end become one wire, also on a pin, up to a fork or a bus, so a double click chooses that whole line, Shift and a click every wire joined to it; an end of a dragged segment or whole wire that lies on another wire slides along it;
  - a click on a part's designator or value chooses that text alone, and a drag moves it; an empty value shows as a grey VALUE mark, except on parts that need none, such as switches and connectors; a double click on the value edits it in place;
  - the mouse resting a second on a wire, a pin, a label or a power symbol lights up its whole net on the sheet, with a dot on each of its pins, and shows the net's name beside the cursor;
  - a short right click opens a menu, framed to stand out from the paper: on an object it chooses it first (what is chosen already stays chosen) and offers turning, mirroring, deleting, copying, cutting, duplicating and editing its value, net name, label text or bus name in place; on empty paper it offers pasting, wires, buses, labels, power symbols and selecting all;
  - Undo/Redo.
- Properties panel: the designator, value, footprint (one the part allows), off-board flag, comment (not drawn on the sheet) and, for resistors, rated power of the chosen components; with GOST designators the power is marked inside the resistor's body (/// // / — | || and Roman numerals from 5 W), with international ones it is written after the value unless most resistors have it; it is also chosen from the menu of the sheet; with several chosen, what is set goes to them all as one step.
- Nets panel: every net with its name, class and pins, and a filter by name or pin. A net's class is chosen from a list; a net with no label gets a lasting name (NN1, NN2 …) kept by its pins, and two nets with classes joined by a wire take the stronger class. A row chosen lights its net, opening a sheet it is on; Esc on the sheet drops it.
- BOM tab, after the board's, shown by View > BOM and closed by the button on it: the parts of the schematic, those alike (part, value, footprint, rated power and comment) in one row, with their designators and how many, under a heading for each group of parts (resistors, capacitors and so on; parts without a group last, under Other); the rated power most resistors have is said once below the list, another one follows the value; the value and the comment are typed in the table for the whole row, a double click on the designators shows those parts, and the buttons write the list as `out/<project>-bom.txt`, `.csv` or `.pdf`.
- ERC panel: the schematic is checked after every change; each problem is listed under its sheet and ringed in red there: a missing value, a repeated designator, a pin joined to nothing, a no-connect mark on no pin or on a joined one, a label or a wire end joining nothing, a wire lying along another, wires closing a loop, a two-pin part with both pins on one net, a net with two names, a name of the nets table with no pins left or given to another net. The rings show while the panel is open. A problem chosen shows its sheet, centred on it, and an object chosen on the sheet chooses its problem in the list; a missing value opens for typing at once.

Planned next: nets, ERC; then the board editor with ratsnest, tracks and DRC; Gerber and Excellon export; 1:1 printing for toner transfer; schematic printing with a title block and a BOM; an AppImage.

The full specification is in [`tz.md`](tz.md) (English) and [`tz.ru.md`](tz.ru.md) (Russian original).

## Building

Needs CMake 3.22+, a C++17 compiler and Qt 6.4+ (Core, Gui, Widgets, LinguistTools, Test). On Ubuntu 24.04:

```
sudo apt install build-essential cmake ninja-build qt6-base-dev qt6-tools-dev qt6-l10n-tools
./00-Make.sh
```

`00-Make.sh` configures, builds and runs the tests; a build that passed them is copied to `OUT/pocketpcb`. Open `examples/demo/demo.ppcb` to see a small project.

## License

GPL-3.0-or-later, see [`LICENSE`](LICENSE). The Newstroke font (`src/core/strokefont_data.cpp`) is taken from KiCad, GPL-2.0-or-later, by Vladimir Uryvaev and the KiCad developers, after the Hershey fonts.
