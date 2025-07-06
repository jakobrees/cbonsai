cbonsai(6)

# NAME

cbonsai - grow bonsai trees in your terminal with seasonal colors and real-time growth

# SYNOPSIS

cbonsai [OPTION]...

# DESCRIPTION

cbonsai is an advanced bonsai tree generator with seasonal color changes, procedural leaf generation, and support for persistent real-time growth. Trees automatically adapt their colors based on the current season and can be configured to grow over days or weeks.

# OPTIONS

*-l*, *--live*
	live mode: show each step of growth

*-t*, *--time*=_TIME_
	in live mode, wait TIME secs between steps of growth (must be larger than 0) [default: 0.03]

*-P*, *--procedural*
	enable procedural leaf generation mode for more realistic leaf placement

*-i*, *--infinite*
	infinite mode: keep growing trees

*-w*, *--wait*=_TIME_
	in infinite mode, wait TIME between each tree generation [default: 4.00]

*-S*, *--screensaver*
	screensaver mode; equivalent to -li and quit on any keypress

*-m*, *--message*=_STR_
	attach message next to the tree

*-T*, *--msgtime*=_SECS_
	clear message after SECS seconds

*-b*, *--base*=_INT_
	ascii-art plant base to use, 0 is none

*-c*, *--leaf*=_LIST_
	list of comma-delimited strings randomly chosen for leaves

*-M*, *--multiplier*=_INT_
	branch multiplier; higher -> more branching (0-20) [default: 8]

*-N*, *--name*=_TIME_
	create a named tree that grows over real time, where TIME is life of tree in seconds. MUST be used with -W to specify a save file. (automatically enables -l and -P)

*-L*, *--life*=_INT_
	life; higher -> more growth (0-200) [default: 120]

*-p*, *--print*
	print tree to terminal when finished

*-s*, *--seed*=_INT_
	seed random number generator

*-W*, *--save*=_FILE_
	save progress to file [default: ~/.cache/cbonsai]

*-C*, *--load*=_FILE_
	load progress from file [default: ~/.cache/cbonsai]

*-v*, *--verbose*
	increase output verbosity

*-h*, *--help*
	show help

# EXAMPLES

Generate a simple tree:
	cbonsai

Watch a tree grow with live animation:
	cbonsai -l

Create a complex tree with procedural leaves:
	cbonsai -L 200 -M 15 -P -l

Create a named tree that grows over 30 days:
	cbonsai -N 2592000 -W ~/my_tree

Display a tree with a message that disappears after 10 seconds:
	cbonsai -m "Welcome!" -T 10

# FEATURES

*Seasonal Colors*
	Trees automatically display colors appropriate for the current season, transitioning through spring greens, summer depth, autumn yellows and reds, and winter whites.

*Named Trees*
	Using the -N option, trees can be created that persist and continue growing in real-time even when the program is not running.

*Procedural Generation*
	The -P option enables advanced leaf placement using branch position history for more realistic results.

*Enhanced Growth Algorithm*
	Sophisticated branching logic with age-based behavior, trunk splitting, and dynamic branch lifecycle management.

# FILES

~/.cache/cbonsai
	Default location for saved tree state

# AUTHORS

Enhanced and significantly improved by Jakob Rees, building on the original foundation by John Allbritten <me@johnallbritten.com>. 

Major enhancements include complete algorithm rewrite, seasonal color system, named tree functionality, procedural leaf generation, and advanced branch management.

For more information about cbonsai development, see the project repository.

# SEE ALSO

ncurses(3)