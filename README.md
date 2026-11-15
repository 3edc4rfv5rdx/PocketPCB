# PocketPCB

A small, simple editor for schematics and double-sided printed circuit boards with manual routing. The aim is a board you can order from a factory (Gerber and drill files) or make at home by toner transfer (1:1 layer printing). It is not a KiCad competitor: few features, kept simple.

Written in C++ with Qt 6. Linux first; the code is kept portable. The interface is in English, Ukrainian and Russian.

## Status

Early development. What works now:

- Projects: each project lives in a folder of its own: `amp/amp.ppcb` (project), `amp.ppsch` (schematic), `amp.ppbrd` (board), all JSON. Saving keeps backup copies in `backup/`, the open project is autosaved to `autosave/` and offered back after a crash, and you are asked before unsaved changes are lost; the bottom left corner of the window shows in colour whether the project is saved, and a click on it saves. The File menu lists the recent projects.
- Library: a built-in library and your own (`~/.local/share/PocketPCB/library`), plus the library of the open project in `amp/library/`. Parts, symbols and footprints are JSON files you can copy by hand. A project keeps copies of the parts it uses, so it opens the same even when the library changes.
- Built-in parts: resistors, capacitors, an inductor, a trimmer, diodes and LEDs, bipolar, field-effect and MOS transistors, a thyristor, DIP chips, a crystal, a fuse, a button, a buzzer, pin headers, screw terminals, a wire pad, and panel parts (jacks, switches, a potentiometer, a speaker, a battery). Symbols follow GOST/IEC style.
- Properties panel: the designator, value, footprint (one the part allows) and off-board flag of the chosen components; with several chosen, what is set goes to them all as one step.
- Library panel: search, parts grouped by designator prefix (international or GOST), a drawing of the symbol and footprint; a double click on a part on the sheet shows its part and footprint there.
- Schematic sheet: choose with a click or a rectangle, drag along the grid with the wires following (Shift tears them off), choose a wire's segment with a click (a drag moves it across, Delete removes it, and taken near the wire's end it drags that end, unless the end is on a pin) or the whole wire with a double click, or every wire joined to it with Shift and a click, choose and drag a part's designator or value alone (an empty value shows as a grey VALUE mark, except on parts that need none, such as switches and connectors) and edit the value in place with a double click, rotate and mirror with the wires following too, delete, copy, cut, paste and duplicate (pasted parts are numbered on), draw wires (W; Space turns the corner, / slants it) with junction dots placed by themselves, Undo/Redo.

Planned next: labels and power symbols, nets, ERC; then the board editor with ratsnest, tracks and DRC; Gerber and Excellon export; 1:1 printing for toner transfer; schematic printing with a title block and a BOM; an AppImage.

The full specification is in [`tz.md`](tz.md) (English) and [`tz.ru.md`](tz.ru.md) (Russian original).

## Building

Needs CMake 3.22+, a C++17 compiler and Qt 6.4+ (Core, Gui, Widgets, LinguistTools, Test). On Ubuntu 24.04:

```
sudo apt install build-essential cmake ninja-build qt6-base-dev qt6-tools-dev qt6-l10n-tools
./00-Make.sh
```

`00-Make.sh` configures, builds and runs the tests; a build that passed them is copied to `OUT/pocketpcb`. Open `examples/demo/demo.ppcb` to see a small project.

## License

GPL-3.0-or-later, see [`LICENSE`](LICENSE).
