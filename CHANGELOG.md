# CHANGELOG
> Newest entries on top.
> N=new feature, E=error fix, F=fine-tune, R=refactor, I=infrastructure

## Unreleased
- I: 00-Make.sh copies a build that passed its tests to OUT/
- N: Main window with a canvas: grid as lines or dots, wheel zoom, panning with the right or middle button, grid cursor styles, grid step in the status bar
- I: 00-Make.sh configures, builds and tests the project in one command
- I: CMake skeleton with the core library (nanometre units, grid steps and snapping) and its Qt Test suite
