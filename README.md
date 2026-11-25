# PocketPCB

A small, simple editor for schematics and single- or double-sided printed circuit boards, with an autorouter. The aim is a board you can order from a factory (Gerber and drill files) or make at home by toner transfer (1:1 layer printing). It is not a KiCad competitor: few features, kept simple.

Written in C++ with Qt 6. Linux first; the code is kept portable. Text is drawn in a built-in stroke font, Newstroke from KiCad, the same on any computer. The interface is in English, Ukrainian and Russian, following the system or chosen in Settings > Language (taken at the next start).

## Status

Early development. What works now (the user guide, Help > User guide or [`help.html`](help.html), tells how):

- Projects: a folder each, JSON files, backups, autosave, recent projects.
- Versions: numbered zips in `versions/`, a Versions panel, restore.
- Project properties: title block, designator style, copper sides, net classes, rules.
- Library: built-in, your own and the project's copy; Update parts from library.
- Schematic: sheets, parts, wires, buses, labels, power symbols, no-connect marks, lock.
- Panels: Library, Properties, Nets, ERC, DRC, BOM, Versions.
- Board: outline, parts, holes, tracks, vias, wire links, ratsnest, lock.
- Update board from schematic (F8).
- Autorouter, also for one-sided boards with wire links.
- Printing: schematic to printer or PDF, board 1:1 for toner transfer.
- Gerber and Excellon export with a zip for the factory.

Planned next: open a version as a read-only copy.

The full specification is in [`tz.md`](tz.md) (English) and [`tz.ru.md`](tz.ru.md) (Russian original).

## Building

Needs CMake 3.22+, a C++17 compiler, Qt 6.4+ (Core, Gui, Widgets, LinguistTools, Test) and zlib. On Ubuntu 24.04:

```
sudo apt install build-essential cmake ninja-build qt6-base-dev qt6-tools-dev qt6-l10n-tools qt6-translations-l10n zlib1g-dev
./00-Make.sh
```

`00-Make.sh` configures, builds and runs the tests; a build that passed them is copied to `OUT/pocketpcb`. Open `examples/demo/demo.ppcb` to see a small project.

`14-MakeAppImage.sh` then packs that build into `OUT/pocketpcb-<version>-x86_64.AppImage`, one file with Qt, the built-in library and the user guide inside, keeping the last three. It needs `linuxdeploy` and `linuxdeploy-plugin-qt` in `tools/` (kept out of git) and says how to get them when they are missing.

## License

GPL-3.0-or-later, see [`LICENSE`](LICENSE). The Newstroke font (`src/core/strokefont_data.cpp`) is taken from KiCad, GPL-2.0-or-later, by Vladimir Uryvaev and the KiCad developers, after the Hershey fonts.
