# cbonsai

<img src="https://i.imgur.com/rnqJx3P.gif" align="right" width="400px">

`cbonsai` is an advanced bonsai tree generator, written in `C` using `ncurses`. It intelligently creates, colors, and positions bonsai trees with seasonal color changes, real-time growth, and advanced procedural generation. The trees are entirely configurable via CLI options and support multiple modes of operation including static display, live growth animation, and persistent real-time trees.

This version includes major enhancements and new features developed by Jakob Rees, building on the original foundation by John Allbritten.

<br>
<br>
<br>
<br>

## Key Features

<img src="assets/cbonsai-demo.gif"  align="right" width="400px">

- **Seasonal Colors**: Trees automatically change colors based on the current date, transitioning through spring greens, summer depth, autumn yellows and reds, and winter whites
- **Named Trees**: Create persistent trees that grow in real-time over days or weeks using the `-N` option
- **Procedural Leaf Generation**: Advanced `-P` mode generates realistic leaf distributions using position history
- **Message System**: Display custom messages with optional timeouts
- **Enhanced Growth Algorithm**: Sophisticated branching logic with age-based behavior and trunk splitting
- **Live Growth Animation**: Watch your tree grow step by step with customizable timing
- **Save/Load System**: Persistent tree state with timestamp support for real-time growth

## Installation

### Quick Build (macOS with Homebrew)

```bash
gcc -Wall -Wextra -Wshadow -Wpointer-arith -Wcast-qual -pedantic \
    -I$(brew --prefix)/opt/ncurses/include \
    -L$(brew --prefix)/opt/ncurses/lib \
    cbonsai.c -o cbonsai \
    $(brew --prefix)/opt/ncurses/lib/libncurses.a \
    $(brew --prefix)/opt/ncurses/lib/libpanel.a
```

### Manual Installation

You'll need to have a working `ncursesw`/`ncurses` library.

#### Debian-based

```bash
sudo apt install libncursesw5-dev
git clone [your-repo-url]
cd cbonsai
make install PREFIX=~/.local
```

#### Fedora

```bash
sudo dnf install ncurses-devel
git clone [your-repo-url]
cd cbonsai
make install PREFIX=~/.local
```

#### macOS

```bash
brew install ncurses
git clone [your-repo-url]
cd cbonsai
# Follow the Quick Build command above
```

## Usage

```
Usage: cbonsai [OPTION]...

cbonsai is a beautifully random bonsai tree generator with seasonal colors and real-time growth.

Options:
  -l, --live             live mode: show each step of growth
  -t, --time=TIME        in live mode, wait TIME secs between
                           steps of growth (must be larger than 0) [default: 0.03]
  -P, --procedural       enable procedural leaf generation mode
  -i, --infinite         infinite mode: keep growing trees
  -w, --wait=TIME        in infinite mode, wait TIME between each tree
                           generation [default: 4.00]
  -S, --screensaver      screensaver mode; equivalent to -li and
                           quit on any keypress
  -m, --message=STR      attach message next to the tree
  -T, --msgtime=SECS     clear message after SECS seconds
  -b, --base=INT         ascii-art plant base to use, 0 is none
  -c, --leaf=LIST        list of comma-delimited strings randomly chosen
                           for leaves
  -M, --multiplier=INT   branch multiplier; higher -> more
                           branching (0-20) [default: 8]
  -N, --name=TIME        create a named tree that grows over real time,
                           where TIME is life of tree in seconds.
                           MUST be used with -W to specify a save file.
                           (automatically enables -l and -P)
  -L, --life=INT         life; higher -> more growth (0-200) [default: 120]
  -p, --print            print tree to terminal when finished
  -s, --seed=INT         seed random number generator
  -W, --save=FILE        save progress to file [default: ~/.cache/cbonsai]
  -C, --load=FILE        load progress from file [default: ~/.cache/cbonsai]
  -v, --verbose          increase output verbosity
  -h, --help             show help
```

## Examples

### Basic Usage

```bash
# Generate a simple tree
cbonsai

# Watch it grow with live animation
cbonsai -l

# Create a bigger, more complex tree
cbonsai -L 200 -M 15 -l

# Create a tree with a custom message
cbonsai -m "Happy Birthday!" -T 10
```

### Named Trees (Real-time Growth)

```bash
# Create a tree that grows over 30 days (2,592,000 seconds)
cbonsai -N 2592000 -W ~/my_tree

# Load and continue growing your tree
cbonsai -C ~/my_tree
```

### Screensaver Mode

```bash
# Perfect for screensavers - saves/loads automatically
cbonsai -S
```

## Advanced Features

### Seasonal Colors
Trees automatically display appropriate colors for the current season:
- **Spring**: Light green growth
- **Summer**: Deep, rich greens  
- **Autumn**: Yellows transitioning to deep reds
- **Winter**: Bare branches with white highlights

### Procedural Leaf Generation
When using `-P`, the algorithm tracks branch movement history to create more realistic leaf placement patterns.

### Named Trees
Named trees with `-N` create persistent trees that continue growing even when the program isn't running. Perfect for long-term displays or screensavers.

## Tips

### Long-term Growth
```bash
# Start a tree that will grow over a month
cbonsai -N 2592000 -L 200 -M 10 -W ~/garden/oak_tree -v

# Check on it periodically
cbonsai -C ~/garden/oak_tree
```

### Add to Shell Profile
For a new bonsai tree every time you open a terminal:
```bash
echo "cbonsai -p" >> ~/.bashrc
```

## How it Works

This enhanced version uses a sophisticated iterative growth algorithm instead of simple recursion (yes, potentially ruining the source  code's elegance; commpensating with visual intuitiveness). Key improvements include:

- **Dynamic Branch Management**: Branches are managed in a dynamic list with individual lifecycles
- **Position History Tracking**: Each branch maintains a history of positions for realistic leaf placement
- **Age-based Growth Patterns**: Different growth behaviors based on branch age and type
- **Seasonal Color System**: Real-time color calculation based on current date with smooth transitions
- **Persistent State**: Complex save/load system supporting real-time growth scenarios

The algorithm carefully balances realistic growth patterns with aesthetic appeal, using probability distributions that change based on branch age, type, and environmental factors.

## Development

### Major Enhancements by Jakob Rees:
- Complete rewrite of the growth algorithm from recursive to iterative
- Seasonal color system with date-based transitions
- Named tree functionality with real-time growth
- Procedural leaf generation with position history
- Advanced branch lifecycle management
- Enhanced save/load system with timestamps
- Message timeout system
- Improved color management and terminal compatibility

### Original Foundation:
Based on the original `cbonsai` by John Allbritten, which was itself inspired by earlier bonsai generators.

## Contributing

This project welcomes contributions! Feel free to open issues or submit pull requests.

## License

GNU GENERAL PUBLIC LICENSE

## Authors

**Jakob Rees** - Major enhancements and algorithm improvements  
**John Allbritten** - Original foundation and core concepts
