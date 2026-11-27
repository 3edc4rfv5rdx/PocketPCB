# Writing a part

A part is three JSON files, each in its own folder and named by its file name
without `.json`:

- `symbols/<name>.json` — the drawing on the schematic and its pins;
- `footprints/<name>.json` — the pads on the board and the outline on the silkscreen;
- `parts/<name>.json` — what the library panel shows: which symbol, which footprints,
  the designator prefixes, the group and the name in every language.

Several parts may share a symbol or a footprint. The built-in library is this folder;
your own goes into `~/.PocketPCB/library` (File > Open library folder) in
the same three folders, and a file there with the name of a built-in one takes its place.
The program reads the folders again by the ⟳ button of the library panel.

All sizes are in millimetres. The origin of a symbol and of a footprint is its
middle; Y grows downwards.

## Symbol

```json
{
  "type": "pocketpcb-symbol",
  "format": 1,
  "graphics": [
    {"kind": "rect", "from": [-2.54, -1.016], "to": [2.54, 1.016]},
    {"kind": "line", "from": [-1.27, 0], "to": [1.27, 0], "width": 0.3},
    {"kind": "circle", "center": [0, 0], "radius": 1.27},
    {"kind": "arc", "center": [0, 0], "radius": 1.27, "start": 0, "span": 180},
    {"kind": "polygon", "points": [[0, 0], [1, 0.5], [0, 1]], "filled": true},
    {"kind": "text", "at": [-1.905, -1.524], "text": "+", "height": 1.27}
  ],
  "pins": [
    {"number": "1", "name": "A", "at": [-5.08, 0], "side": "left"},
    {"number": "2", "name": "K", "at": [5.08, 0], "side": "right", "length": 2.54}
  ],
  "refAt": [0, -2.54],
  "valueAt": [0, 2.54],
  "showPinNumbers": false,
  "showPinNames": false
}
```

- `graphics`: `line` (`from`, `to`), `rect` (two corners), `circle` (`center`,
  `radius`), `arc` (`start` and `span` in whole degrees, 0 pointing right, growing
  clockwise on the screen), `polygon` (three corners or more, `filled` optional),
  `text` (`at`, `text`, `height`, `align`: `left`, `center` or `right`), `oval` (two
  corners, the shorter sides round). Every kind
  but text takes `width` (0.2 by default) and `dashed`.
- `pins`: `number` is unique and must match a pad number of the footprints; `name`
  is optional. `at` is the end a wire joins and must lie on the 2.54 grid; `side`
  is the side of the body the pin sticks out of (`left`, `right`, `up`, `down`),
  so the pin's line runs from `at` towards the body for `length` (2.54 by default).
- `refAt`, `valueAt`: where the designator and the value are first put.
- `showPinNumbers`, `showPinNames`: both on when absent; off for parts like a
  resistor, where they say nothing.
- A power symbol has one pin at the origin and `"power": "GND"`, the net name it
  places; `"hidePowerName": true` leaves the name off the sheet, and `"name"` gives
  it a name in the panel as for a part.

Keep to the look of the symbols already here: bodies on whole or half steps of
2.54 where it suits, text 1.27 high, the leads of two-pin parts 2.54 long.

## Footprint

```json
{
  "type": "pocketpcb-footprint",
  "format": 1,
  "pads": [
    {"number": "1", "at": [-3.81, 0], "shape": "rect", "size": [1.8, 1.8], "hole": 0.8},
    {"number": "2", "at": [3.81, 0], "shape": "circle", "size": [1.8, 1.8], "hole": 0.8}
  ],
  "graphics": [
    {"kind": "rect", "from": [-2.5, -1.2], "to": [2.5, 1.2]}
  ],
  "refAt": [0, -2.4],
  "valueAt": [0, 2.4],
  "body": {"shape": "lying", "size": [6.5, 2.5], "height": 2.5, "raised": 0.5, "color": "#d9c3a0"}
}
```

- `pads`: `shape` is `circle` (width and height equal), `rect` or `oval`; `size` is
  width and height; `hole` the drill, smaller than the pad; `layers` is `both`
  (the default for a pad with a hole), `front` or `back` (a pad with no hole).
  Pad 1 is usually square. The pitch is usually 2.54 or a multiple.
- `graphics`: the outline of the body on the silkscreen, as in a symbol, kept
  clear of the pads; `oval` too (two corners, its short sides round).
- `body` (optional): the part over the board for the picture in three dimensions.
  `shape` is `box`, `upright` (a cylinder standing up, as wide as the smaller side
  of `size`) or `lying` (a cylinder along the longer side, as thick as the shorter);
  `size` across and down about the middle of the part, left out for the drawing of
  the silkscreen; `height` (not used by `lying`); `raised` how far above the board,
  0 by default; `color` as `#rrggbb`.

## Part

```json
{
  "type": "pocketpcb-part",
  "format": 1,
  "symbol": "diode",
  "footprints": ["diode-7.62", "diode-10.16"],
  "prefix": "D",
  "prefixGost": "VD",
  "value": "1N4148",
  "connector": false,
  "noValue": false,
  "powerMw": 125,
  "group": "diodes",
  "name": {"en": "Diode", "uk": "Діод", "ru": "Диод"}
}
```

- `footprints`: the first one is offered first; none for a part only drawn.
- `prefix`, `prefixGost`: the international and the GOST designator prefix
  (R/R, D/VD, LED/HL, Q/VT, U/DD or DA, J/X, SW/SA, F/FU …).
- `value`: put in when the part is placed; empty asks for one. `noValue`: the
  part has none (switches, connectors), so ERC does not ask.
- `connector`: true for connectors and wire pads, which ERC treats as the ends of
  the board.
- `powerMw`: the rated power of a resistor, in milliwatts; leave it out for others.
- `group`: where the library panel and the BOM put the part. The program's groups
  are listed, in their order, in `PART_GROUPS` in `src/core/part.cpp`; a key it does
  not know becomes a group of its own named by the key.
- `name`: the name in the panel in English, Ukrainian and Russian.

## Checklist for a built-in part

1. The symbol, the footprint and the part, with pin numbers matching pad numbers.
2. The name in all three languages, both prefixes, the group.
3. Placed on a sheet: it looks like its neighbours, the designator and the value
   stand clear of the body, and the footprint shows the right pads.
4. A `CHANGELOG.md` entry.
