# CHANGELOG
> Newest entries on top.
> N=new feature, E=error fix, F=fine-tune, R=refactor, I=infrastructure

## Unreleased
- I: Library gets pin headers 1×1 to 1×10, screw terminals for 2 and 3 wires and a wire pad
- F: Electrolytic capacitor footprints have their pads on the 2.54 grid
- F: Library panel keeps the groups the user opened, the chosen part and the scroll when the library is read again
- I: Library gets a diode, an LED, NPN and PNP transistors, JFETs and MOSFETs with TO-92 and TO-220 footprints
- I: Library gets a capacitor, an electrolytic capacitor, a trimmer resistor and an inductor
- N: Library panel on the right: parts of the library and of the open project grouped by prefix, a search field and a drawing of the chosen part
- I: Library gets the resistor symbol and part, and a test that every part has its symbol and footprints
- N: The built-in and the user's library are loaded on start, the user's library folder is made and opened from the File menu
- R: The library types are named for any library folder, not for the project's alone
- R: Folders are made by one shared function with one message
- N: The open project is autosaved every five minutes into its autosave folder, and the changes are offered back when it is opened after a crash
- N: Saving keeps the three last versions of each project file in the backup folder of the project
- N: Closing the window, a new project and opening another one ask whether to save unsaved changes
- I: Project file names its default net class and the class of power nets
## v0.1.14 (2026-11-14)
- E: 22-RelUpload.sh no longer refuses the build it was asked to upload
- I: 00-Make.sh moves the version line when a feature waits in CHANGELOG since the last release
- I: Scripts to push the branch, tag a release and upload it to GitHub
- F: A project keeps the parts, symbols and footprints it uses as library files in its library folder, and draws a red question mark for one that is missing
- F: Save as asks for a name and a place and puts the project into a folder of its own, refusing one that is not empty
- I: Library part gets a name in every interface language for the library panel
- N: The window opens, creates and saves projects and shows the schematic sheets and the board on tabs over the canvas
- I: Library gets two resistor footprints: with 10.16 mm between the pads and standing upright
- I: Core gets the open project with Undo and Redo over the schematic and the board, and saves only the files that changed
- I: Core gets the project (title block, net classes, routing rules, grid) with the .ppcb file, and reads and writes the three files of a project together
- I: Core gets the board model (outline, placed footprints, tracks, vias, mounting holes, silkscreen drawing, zones) and the .ppbrd file
- I: Schematic model gets buses, and graphics get a dashed line
- I: Core gets the schematic model (sheets, components, wires, labels, power symbols) and the .ppsch file
- F: Settings and data folders are named PocketPCB instead of pocketpcb
- I: 00-Make.sh takes the next build number in build_number.txt on every run
- N: Open library file shows a symbol or footprint JSON on the canvas and redraws it whenever the file is saved
- I: Core gets the footprint with its pads and the library part, read from and written to JSON
- I: Symbol graphics get a polygon with an optional fill
- I: Core gets the schematic symbol with its graphics and pins, read from and written to JSON
- I: Core gets points, object identifiers and JSON reading and writing with error messages that name the place in the file
- N: The interface is translated into Ukrainian and Russian and follows the system language
- I: 00-Make.sh copies a build that passed its tests to OUT/
- N: Main window with a canvas: grid as lines or dots, wheel zoom, panning with the right or middle button, grid cursor styles, grid step in the status bar
- I: 00-Make.sh configures, builds and tests the project in one command
- I: CMake skeleton with the core library (nanometre units, grid steps and snapping) and its Qt Test suite
