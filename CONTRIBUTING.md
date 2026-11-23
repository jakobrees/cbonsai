# Contributing

The best way to contribute is to **open an issue**. Bug reports are useful, and
you're welcome to include a suggested fix: a diff, a link to a branch in your
fork, or just a description of what you think is wrong.

This is a spare-time project. I may look at a suggestion or I may not, and I may
fix the problem a different way. An issue without a reply isn't a rejection.
Please open an issue rather than a pull request.

A good report includes:

- what you ran (the exact command) and what happened
- your OS, terminal emulator, and `cbonsai --version`
- for color or drawing bugs: `$TERM`, and whether you're inside
  tmux/screen/Neovim

The rest of this file explains how the code works, so that a suggested fix has
a better chance of fitting.

## Building

See [README.md](README.md#installation) for dependencies. Once you have
`ncursesw`:

```bash
make            # build ./cbonsai
make cbonsai.6  # build the man page (needs scdoc)
make clean
```

On macOS the Homebrew `ncurses` paths matter; the README has the explicit `gcc`
invocation that works there.

## The determinism contract

This is the one rule every change has to respect.

**A saved tree must replay bit-identically under the engine it was created
with.** Named trees (`-N`) and save files (`-W`/`-C`) persist across days or
weeks, and a tree someone has been growing for a month must never change shape
because they updated the program.

The mechanism is a version tag on the growth algorithm. From `src/cbonsai.c`:

> The version tag pins the growth algorithm: a saved tree must replay
> bit-identically under the engine it was created with, so each engine owns its
> full RNG-consuming call chain (growth deltas, branching decisions, colors,
> leaf glyphs, leaf walkers). Only RNG-free plumbing (grids, blitting,
> resize/input handling, the pot) is shared between engines.

Two consequences:

- **v1 is frozen.** It consumes the global `rand()` stream seeded by `srand()`.
  Do not modify `growTree_v1` or anything it calls, ever — not even to fix a
  bug. If v1 has a bug, that bug is now part of what v1 trees look like.
- **v2+ engines draw from explicit `msaw` streams** (`src/msaw.h`), never from
  global `rand()`. Each engine owns its `_v2`-suffixed call chain.

Within an engine, growth and cosmetics are drawn from *separate* streams,
decorrelated by salt constants (`MSAW_COSMETIC_SALT`, `MSAW_WIDEN_SALT`). That
separation is deliberate and load-bearing: it's what lets colors and animation
be tweaked without reshaping saved trees.

### Changes that are always safe

- Anything RNG-free: grids, blitting, resize handling, input, the pot,
  windowing, output.
- Palette values and color selection, as long as you draw from the cosmetic
  stream and don't change how many draws you take.
- Presentation-only timing, e.g. the trunk-widening animation stream.
- New flags that only affect display (`--bare` and `--season` are the model:
  same tree, drawn differently).

### Changes that need a new engine

If your change alters **the order or number of RNG draws** in the growth call
chain, it is not a v2 edit — it's v3. That includes:

- changing growth deltas, lean, branching probability, fork conditions
- adding or removing a `mrand`/`msaw_next`/`msaw_below` call on the growth path
- changing when leaf walkers spawn or how many draws they consume
- reordering existing draws, even if the distribution is unchanged

It doesn't matter whether the new behavior is better. If existing save files
would replay differently, it needs its own version.

### Adding an engine

1. Copy the `_v2` call chain to `_v3` and change it there.
2. Register it in `get_engine()` (`src/cbonsai.c`).
3. Bump the default `.version` in the config initializer so new trees use it.
4. Leave v1 and v2 untouched.
5. Update the `--engine` help text, the README, and `cbonsai.scd`.

Yes, this duplicates code. That's the intended trade: correctness for old saves
over DRY in the engine.

## Save file format

Save files are a single line written by `saveToFile()`:

```
v<version> <seed> <globalTime> <creationTime> <secondsPerTick> <lifeStart> <multiplier> <baseType>
```

The leading `v<version>` is the engine tag, and it's what `loadFromFile()`
dispatches on. If you need to persist more state, **append** fields and keep the
reader tolerant of older, shorter lines — someone out there has a tree they've
been growing for months.

## Terminal compatibility

A terminal can claim (through terminfo) that its palette can be redefined and
then ignore `init_color()`; Konsole does this by default. `can_change_color()`
returns true there, so it can't be used to detect the problem.

Instead, `init()` puts every color in the xterm-256 slot whose stock color is
already close. If the redefinition works you get the exact shade; if it's
ignored you get a near match. Keep that property when changing colors: never
redefine a slot whose default would look wrong.

## Where code goes

- `src/` — the program. Keep it OS-agnostic. cbonsai should not know what
  distribution it's running on.
- `packaging/`, `flake.nix` — package recipes, maintained here.
- `legacy/` — historical snapshots kept for reference. Not built, not
  maintained; don't send fixes for them.

## If you add a CLI flag, update all four

A flag isn't done until it exists in all of these:

1. `printHelp()` and the `getopt_long` table in `src/cbonsai.c`
2. the usage block in `README.md`
3. `cbonsai.scd` (the man page source)
4. `completions/bash/cbonsai.bash`

## Style

Match the surrounding code rather than any external style guide:

- tabs for indentation
- `camelCase` for functions and variables, `SCREAMING_CASE` for constants
- engine-owned functions carry their version suffix (`updateBranch_v2`)
- comments explain *why*, especially where something looks arbitrary — if a
  constant affects determinism, say so, as the `INTERNAL CONSTANTS` block does

Build cleanly under the flags the Makefile uses (`-Wall -Wextra -Wshadow
-Wpointer-arith -Wcast-qual -pedantic`). No new warnings.

## License

This project is GPL-3.0. Anything you suggest in an issue is accepted under the
same license.

cbonsai is a fork of [cbonsai by John Allbritten](https://gitlab.com/jallbrit/cbonsai).
If your fix also applies upstream, please consider sending it there too.
