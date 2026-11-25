# Working rules for PocketPCB

Distilled from the author's Android projects (`../PhotoCollage/AGENTS.md` and siblings), adapted to a C++/Qt6 desktop project. The specification is `tz.ru.md` (original) / `tz.md` (translation); `tz-add.md` is a draft of additions to it.

## Language

Everything written into the project is English: code, identifiers, comments, `CHANGELOG.md`, `README.md`, commit messages, this file. The conversation with the author is in Russian. UI strings are English in the code, wrapped in `tr()`, and translated into every declared locale (en, uk, ru) — never left as an English placeholder.

## Scope and criticism

- Do what was asked and nothing beside it: no drive-by refactors, renames, "while I was in there" fixes, or reformatting of untouched lines. Anything extra is proposed in one sentence and waits for a yes.
- When the approach looks wrong, say so before writing code, with the reason. Do not invent objections: a sound idea gets one line saying so.
- Nothing is deleted except on the author's explicit command.
- A question is a question: "А если сделать X?" wants prose, not an edit. Diagnose out loud before fixing; for a UI decision, show the variants and wait for a pick.

## Committing

Only on an explicit request — "запиши", "коммит", "commit". A workflow description ("по-фично, с коммитом") is not standing permission.

- One fix, one commit; nothing unrelated from the working tree goes in.
- Commit message: one line, subject only.
- Every commit carries its `CHANGELOG.md` entry (format below), except changes to files that touch neither the app nor its build (`AGENTS.md`, `README.md`, `.gitignore`).
- `README.md` says what the app can do now, a few words per feature, and how to build it; it never repeats `help.html`. A commit that adds a feature carries its README line.
- `help.html` is the user's guide, with a table of contents: how to use every feature, short. A commit that adds or changes a feature carries its help edit too.
- Exactly the intended changes: no chmod, no side tweaks; a file mode the author changed is committed as it is.
- `build_number.txt` is the author's: never edit it by hand, but stage it when a build has left it dirty.
- The branch is `main`. After committing, stop — the author pushes.

## Building and tests

Do not build, run tests or install — not even to check that it compiles. The author builds. If unsure the code compiles, say so. A finished change comes with the one or two things the author should look at to see it working.

## Version and changelog

- `build_number.txt` is the only version: shell-style assignments, no quotes, no spaces around `=`:
  ```
  base_version=0.1
  build=1
  version=0.1.1
  build_date=2026-11-13
  ```
  `version` = `base_version.build`. CMake reads it for `project(VERSION …)`.
- The version line (`base_version`) moves once per feature: a release build raises it when `## Unreleased` holds an `N:` entry and the newest tag is still on the current line.
- `CHANGELOG.md` is parsed, so its shape is fixed:
  ```
  # CHANGELOG
  > Newest entries on top.
  > N=new feature, E=error fix, F=fine-tune, R=refactor, I=infrastructure

  ## Unreleased
  - N: One short sentence
  ## v0.1.5 (2026-11-20)
  - E: One short sentence
  ```
  `## Unreleased` exactly; entries start with `- <letter>: `, newest on top; continuation lines indented; `N:` only when the app can do something it could not before.

## Audits

`ADD/tofixN.md`: verify each finding against the code first (real / not real / cannot verify, with evidence), fix only confirmed ones, one commit each. Mark an item done by prefixing its heading with `FIXED` and the fix's short hash — `## FIXED a1b2c3d — Finding 4: …` — in a second small commit. Items in a plain todo list get a leading `+`.

## Small conventions

- Temporary debug logging starts with `>>> ` so it can be grepped out.
- No duplication, in code or in translations: a value, a function or a user-visible message lives in one place, and shared ones go to one common file. Qt groups `tr()` strings by class, so the same text written in two classes is two entries to translate; a message several classes need is one shared function instead.
