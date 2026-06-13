#ifndef _XOPEN_SOURCE
#define _XOPEN_SOURCE 500
#endif

#include <stdlib.h>
#include <curses.h>
#include <locale.h>
#include <panel.h>
#include <getopt.h>
#include <time.h>
#include <string.h>
#include <wchar.h>
#include <ctype.h>
#include <unistd.h>
#include <errno.h>

#include "msaw.h"


// ==========================================================================
// TUNABLE PARAMETERS  (aesthetic / behaviour knobs — safe to retune)
// ==========================================================================

// 1-in-N chance that a trunk split spawns a bare deadwood fork
#define DEADWOOD_CHANCE 8

// v2 canopy pads: leaf walkers carry an outward sign and lean their drift away
// from the trunk so foliage pools into clouds instead of a symmetric mush.
// Neutral steps drift outward by BIAS; inward drift is capped at INWARD.
#define LEAF_PAD_BIAS   1

#define LEAF_PAD_INWARD 1

// v2 lean: trunks and shoots carry a signed bias that pulls their wander
// toward a committed side, so high multiplier produces bold structure (sweeps
// and clean forks) instead of a symmetric radial explosion. Magnitude is
// clamped to MAX so the pull (probability |lean|/LEAN_DENOM per step) stays
// organic rather than a rigid diagonal.
#define LEAN_MAX   4

#define LEAN_DENOM 4

// v2 crowding gate: a trunk won't split / a shoot won't sprout if another live
// structural branch head is closer than this (Manhattan). Self-limiting — only
// bites where heads are packed (high multiplier), so sparse low-M trees are
// untouched. Trunks want more breathing room than shoots.
#define SPLIT_MIN_DIST 10

#define SHOOT_MIN_DIST 10

// v2 shoot side-runs: shoots stay on one flank for a short run, then flip, so
// foliage groups into alternating pads instead of a strict left/right comb.
// Higher multiplier -> longer one-sided runs (bolder clusters).
#define SHOOT_RUN_MDIV 6

// v2: ceiling on shoot life, hence how far a shoot can run sideways. Shoot life
// is derived from the trunk's remaining life, but a trunk's life is a HEIGHT
// budget (see TRUNK_RISE_COST), so off a fresh high-life base the raw value is
// huge and the shoot crawls clear across the screen. Cap it to the trunk's
// height budget (totalLife / this) plus a little multiplier slack, so a shoot
// never reaches much farther than the tree is tall. Lower -> shorter shoots.
#define SHOOT_LIFE_CAP_DIV 3

// v2: ticks after a trunk split during which no new shoots sprout, so a fork
// gets a clean stretch before branching resumes
#define SPLIT_SHOOT_GRACE 6

// v2 trunk widening: thicken the centerline into a tapering body, widest at the
// base. The per-cell taper uses distance ABOVE the base (which never moves), so
// the shape is stable. Overall thickness is gated and grows with the tree:
// nothing widens until the trunk is TRUNK_MIN_HEIGHT tall (small trees stay
// slim), then the base half-width ramps up one step per TRUNK_GROW_DIV rows of
// extra height, capped at TRUNK_MAX_HALF — so the trunk fattens as it matures.
// The taper then drops the half-width by one every TRUNK_TAPER_DIV rows up.
#define TRUNK_MAX_HALF    3

#define TRUNK_TAPER_DIV   6

#define TRUNK_MIN_HEIGHT  14

#define TRUNK_GROW_DIV    12

// v2: a trunk pays this much life per row it climbs (instead of per tick), so
// its total height is ~ L / TRUNK_RISE_COST regardless of the multiplier — a
// high-M trunk just spends more time wandering sideways between rises. Raise
// it for shorter trees, lower for taller.
#define TRUNK_RISE_COST   4


// ==========================================================================
// INTERNAL CONSTANTS  (not aesthetic knobs — changing these alters output / determinism)
// ==========================================================================

#define BRANCH_HISTORY 3 // moving average for proceedural leaves

// v2+ engines draw growth and cosmetics from separate msaw streams so
// cosmetic tweaks can never change a saved tree's structure; the cosmetic
// stream is decorrelated from the growth stream by this salt
#define MSAW_COSMETIC_SALT 0x9E3779B97F4A7C15ULL

// salt for the trunk-widening animation stream (presentation only — never
// touches growth/cosmetic, so widen timing can't change a saved tree)
#define MSAW_WIDEN_SALT 0xD1B54A32D192ED03ULL

// salt for the deadwood (jin/shari) decision stream. Kept off the growth
// stream so trees that grow no dead limb stay byte-identical; a tree only
// diverges where a dead fork actually appears.
#define MSAW_DEADWOOD_SALT 0xBF58476D1CE4E5B9ULL


// ==========================================================================
// TYPES  (hoisted: every struct/enum precedes all functions)
// ==========================================================================

enum branchType {trunk, shootLeft, shootRight, dying, dead};

struct config {
	unsigned long long targetGlobalTime;
	int live;
	int infinite;
	int screensaver;
	int printTree;
	int verbosity;
	int lifeStart;
	int multiplier;
	int baseType;
	int seed;
	int leavesSize;
	int version;
	int save;
	int load;

	int proceduralMode;
	int namedTree;           // Whether this is a named tree
    time_t creationTime;     // When the tree was first created
    double secondsPerTick;   // How many real seconds per simulation tick (-1 if not named)

	int messageTimeout;  // seconds before message disappears
	time_t messageStartTime;  // when the message was first displayed

	double timeWait;
	double timeStep;

	char* message;
	char* leaves[64];
	char* saveFile;
	char* loadFile;
	int no_disp;
	int hideLeaves;          // --bare: suppress foliage rendering (v2 only)
};

struct ncursesObjects {
	WINDOW* treeWin;
	WINDOW* messageBorderWin;
	WINDOW* messageWin;

	PANEL* treePanel;
	PANEL* messageBorderPanel;
	PANEL* messagePanel;
};

struct GridCell {
	char ch[8];
	attr_t attrs;
	short color_pair;
	int occupied;
	int widenHalf;    // v2 trunk widening: current rendered half-width
	int widenTimer;   // v2 trunk widening: ticks until the next widen step
	int splitDepth;   // v2 trunk widening: how many splits deep (forks thin out)
};

struct VirtualGrid {
	struct GridCell *cells;
	int width, height;
	int anchor_x, anchor_y;
};

struct ColorResult {
	attr_t attrs;
	short color_pair;
};

struct counters {
	int trunks;
	int branches;
	int shoots;
	int shootCounter;
	int trunkSplitCooldown;
	int shootSide;          // v2: current committed shoot flank (shootLeft/shootRight)
	int shootRunRemaining;  // v2: shoots left on this flank before it flips
	unsigned long long globalTime;
};

// Structure to hold RGB values
struct ColorRGB {
	int r, g, b;
	int r_2, g_2, b_2;
};

// Define season colors
struct ColorRGB season_colors[5] = {
	{.r=350, .g=800, .b=350,	// Spring: Light green
	.r_2=500, .g_2=800, .b_2=500},
	{.r=0, .g=700, .b=0,		// Summer: Deep green
	.r_2=0, .g_2=520, .b_2=0},
    {.r=1000, .g=900, .b=80,	// Early Fall: Yellow (short period)
	.r_2=1000, .g_2=600, .b_2=80},
	{.r=900, .g=100, .b=100,	// Late Fall: Deep red (Japanese maple)
	.r_2=450, .g_2=50, .b_2=50},
	{.r=900, .g=900, .b=900,	// Winter: White
	.r_2=750, .g_2=750, .b_2=750}
};

enum Season {
    SPRING,
    SUMMER,
    EARLY_FALL,
    LATE_FALL,
    WINTER
};

struct LeafWalker {
	int x, y;
	unsigned int seed;	// v1: rand_r stream
	struct msaw rng;	// v2: per-walker msaw stream
	int outward;		// v2: canopy-pad bias sign (-1 left, +1 right, 0 none)
};

struct Branch {
	int x, y;                   // Current position
	int dx, dy;                 // Current direction
	int life;                   // Remaining life
	int age;                    // Current age
	enum branchType type;       // Branch type (trunk, shootLeft, etc)
	int shootCooldown;          // Current shoot cooldown
	int dripLeafCooldown;       // Current drip leaf cooldown
	int totalLife;              // Initial life value
	int multiplier;             // Stored multiplier
	int lean;                   // v2: signed horizontal growth bias (committed side)
	int splitDepth;             // v2: how many trunk splits deep this branch is
	int shootGrace;             // v2: ticks after a split before this trunk shoots again
	int deadwood;               // v2: jin/shari — currently bare bleached dead wood
	int diebackLife;            // v2: life at/below which a destined fork dies back (0 = never)
	unsigned int leaf_seed;		// for proceedural consistency (v1)
	struct msaw leaf_rng;		// for proceedural consistency (v2)
	int x_history[BRANCH_HISTORY];          // Circular buffer for last BRANCH_HISTORY x positions
	int y_history[BRANCH_HISTORY];          // Circular buffer for last BRANCH_HISTORY y positions
	int history_count;         // How many positions we've stored (max BRANCH_HISTORY)
	int history_index;         // Current index in circular buffer

	struct VirtualGrid *leafGrid;
	int leaf_steps_drawn;
	int leaf_cur_x, leaf_cur_y;

	struct LeafWalker *walkers;
	int walker_count;
	int walker_capacity;
};

struct BranchList {
	struct Branch* branches;    // Dynamic array of branches
	int count;                  // Current number of branches
	int capacity;              // Current capacity of array
};

/*
 * Versioned tree generation. The version tag pins the growth algorithm:
 * a saved tree must replay bit-identically under the engine it was created
 * with, so each engine owns its full RNG-consuming call chain (growth
 * deltas, branching decisions, colors, leaf glyphs, leaf walkers). Only
 * RNG-free plumbing (grids, blitting, resize/input handling, the pot) is
 * shared between engines.
 *
 * v1 is frozen: it consumes the global rand() stream seeded via srand()
 * and must never be modified. v2+ engines draw from explicit msaw streams.
 */
struct TreeEngine {
	void (*growTree)(struct config *conf, struct ncursesObjects *objects,
		struct counters *myCounters);
};


// ==========================================================================
// FORWARD DECLARATIONS
// ==========================================================================

// common / shared
struct VirtualGrid* grid_create(int w, int h, int ax, int ay);
void grid_destroy(struct VirtualGrid *g);
void grid_clear(struct VirtualGrid *g);
void grid_grow(struct VirtualGrid *g, int lx, int ly);
void grid_put(struct VirtualGrid *g, int tx, int ty, const char *str, attr_t attrs, short cpair);
static struct GridCell *grid_at(struct VirtualGrid *g, int x, int y);
void grid_blit_to_window(struct VirtualGrid *g, WINDOW *win, int ox, int oy);
void delObjects(struct ncursesObjects *objects);
void quit(struct config *conf, struct ncursesObjects *objects, int returnCode);
int saveToFile(const struct config *conf, unsigned long long globalTime);
int loadFromFile(struct config *conf);
void finish(const struct config *conf, struct counters *myCounters);
void printHelp(void);
static void grid_put_str(struct VirtualGrid *g, int tx, int ty, const char *str, attr_t attrs, short cpair);
int getBaseHeight(int baseType);
void drawBaseToGrid(struct VirtualGrid *grid, int baseType, int trunk_x, int trunk_y);
void drawPotRim(struct VirtualGrid *grid, int baseType, int trunk_x, int trunk_y,
				int span_lo, int span_hi);
void drawWins(struct ncursesObjects *objects);
static inline int mrand(struct msaw *st, int mod);
int checkKeyPress(const struct config *conf, struct counters *myCounters);
void updateScreen(float timeStep);
static inline int interpolate_color(int color1, int color2, float ratio);
enum Season get_current_season_with_blend(float *blend_ratio);
void initBranchList(struct BranchList* list);
void addBranch(struct BranchList* list, struct Branch branch, struct counters *myCounters);
void removeBranch(struct BranchList* list, int index);
void freeBranchList(struct BranchList* list);
static inline void update_position_history(struct Branch* branch);
static inline void get_average_position(struct Branch* branch, int* avg_x, int* avg_y);
static inline int isEarlyTrunk(int age, int totalLife);
static inline int isYoungTrunk(int age, int totalLife);
static inline int getBranchRollThreshold(int age, int totalLife, int multiplier);
void addSpaces(WINDOW* messageWin, int count, int *linePosition, int maxWidth);
int createMessageWindows(struct ncursesObjects *objects, char* message);
int drawMessage(struct config *conf, struct ncursesObjects *objects, char* message);
void clearMessage(struct ncursesObjects *objects);
void init(struct config *conf, struct ncursesObjects *objects);
void recalculate_offsets(int trunk_x, int trunk_y, int baseHeight, WINDOW *win, int *ox, int *oy);
void handleResize(struct config *conf, struct ncursesObjects *objects,
				  int trunk_x, int trunk_y, int baseHeight, int *off_x, int *off_y);
void blitTree(struct VirtualGrid *skeleton, struct VirtualGrid *trunkPlane, int trunk_y,
			  struct BranchList *branchList,
			  struct ncursesObjects *objects, int off_x, int off_y);
int liveStepDisplay(struct config *conf, struct ncursesObjects *objects,
					struct VirtualGrid *skeleton, struct VirtualGrid *trunkPlane,
					struct BranchList *branchList,
					struct counters *myCounters,
					int trunk_x, int trunk_y, int baseHeight,
					int *off_x, int *off_y, int maxX, int maxY, int turn);
void finalHold(struct config *conf, struct ncursesObjects *objects,
			   struct VirtualGrid *skeleton, struct VirtualGrid *trunkPlane,
			   struct BranchList *branchList,
			   int trunk_x, int trunk_y, int baseHeight, int *off_x, int *off_y);

// v1 engine (frozen)
static inline void roll(int *dice, int mod);
struct ColorResult chooseColorResult(enum branchType type);
void setDeltas(enum branchType type, int life, int totalLife, int age, int multiplier, int *returnDx, int *returnDy);
char* chooseString(const struct config *conf, enum branchType type, int life, int dx, int dy);
void updateBranch_v1(struct config *conf, struct VirtualGrid *skeleton,
				struct counters *myCounters, int branchIdx,
				struct BranchList* list);
static void leafStepWalkers(struct config *conf, struct VirtualGrid *grid,
							enum branchType type, int groundY,
							struct LeafWalker **walkers, int *count, int *capacity);
void generateLeaves_v1(struct config *conf, struct VirtualGrid *grid, enum branchType type, int x, int y, int life, unsigned int leaf_seed, int groundY);
void growTree_v1(struct config *conf, struct ncursesObjects *objects, struct counters *myCounters);

// v2 engine
struct ColorResult chooseColorResult_v2(enum branchType type, struct msaw *cosmetic);
static int applyLean(struct msaw *growth, int dx, int lean, int lo, int hi);
void setDeltas_v2(enum branchType type, int life, int totalLife, int age,
				  int multiplier, int *returnDx, int *returnDy,
				  int lean, struct msaw *growth);
char* chooseString_v2(const struct config *conf, enum branchType type, int life,
					  int dx, int dy, struct msaw *cosmetic);
static int structuralCrowded(const struct BranchList *list, int x, int y,
							 int exceptIdx, int minDist);
void updateBranch_v2(struct config *conf, struct VirtualGrid *skeleton,
				struct counters *myCounters, int branchIdx,
				struct BranchList* list,
				struct msaw *growth, struct msaw *cosmetic, struct msaw *deadRng);
static void leafStep_v2(struct config *conf, struct VirtualGrid *grid,
						enum branchType type, int groundY,
						struct LeafWalker **walkers, int *count, int *capacity);
void generateLeaves_v2(struct config *conf, struct VirtualGrid *grid, enum branchType type,
					   int x, int y, int life, const struct msaw *leafRng, int groundY,
					   int outward);
static void advanceTrunkWiden(struct VirtualGrid *tp, int trunk_y, struct msaw *rng);
static void drawWidenedTrunk(struct VirtualGrid *tp, WINDOW *win,
							 int trunk_y, int off_x, int off_y);
void growTree_v2(struct config *conf, struct ncursesObjects *objects, struct counters *myCounters);

// dispatch + entry
struct TreeEngine get_engine(int version);
void printstdscr(void);
char* createDefaultCachePath(void);


// ==========================================================================
// COMMON / SHARED  (grid, colour & season, branch list, message, init, io)
// ==========================================================================

struct VirtualGrid* grid_create(int w, int h, int ax, int ay) {
	struct VirtualGrid *g = malloc(sizeof(struct VirtualGrid));
	g->width = w;
	g->height = h;
	g->anchor_x = ax;
	g->anchor_y = ay;
	g->cells = calloc(w * h, sizeof(struct GridCell));
	return g;
}

void grid_destroy(struct VirtualGrid *g) {
	if (!g) return;
	free(g->cells);
	free(g);
}

void grid_clear(struct VirtualGrid *g) {
	memset(g->cells, 0, sizeof(struct GridCell) * g->width * g->height);
}

void grid_grow(struct VirtualGrid *g, int lx, int ly) {
	int new_w = g->width;
	int new_h = g->height;
	int sx = 0, sy = 0;

	if (lx < 0) {
		int need = -lx;
		int grow = (int)(g->width * 0.5);
		if (grow < need) grow = need;
		new_w += grow;
		sx = grow;
	} else if (lx >= g->width) {
		int need = lx - g->width + 1;
		int grow = (int)(g->width * 0.5);
		if (grow < need) grow = need;
		new_w += grow;
	}

	if (ly < 0) {
		int need = -ly;
		int grow = (int)(g->height * 0.5);
		if (grow < need) grow = need;
		new_h += grow;
		sy = grow;
	} else if (ly >= g->height) {
		int need = ly - g->height + 1;
		int grow = (int)(g->height * 0.5);
		if (grow < need) grow = need;
		new_h += grow;
	}

	struct GridCell *nc = calloc(new_w * new_h, sizeof(struct GridCell));
	for (int y = 0; y < g->height; y++) {
		memcpy(&nc[(y + sy) * new_w + sx],
			   &g->cells[y * g->width],
			   g->width * sizeof(struct GridCell));
	}

	free(g->cells);
	g->cells = nc;
	g->anchor_x -= sx;
	g->anchor_y -= sy;
	g->width = new_w;
	g->height = new_h;
}

void grid_put(struct VirtualGrid *g, int tx, int ty, const char *str, attr_t attrs, short cpair) {
	int lx = tx - g->anchor_x;
	int ly = ty - g->anchor_y;
	if (lx < 0 || lx >= g->width || ly < 0 || ly >= g->height) {
		grid_grow(g, lx, ly);
		lx = tx - g->anchor_x;
		ly = ty - g->anchor_y;
	}
	struct GridCell *cell = &g->cells[ly * g->width + lx];
	strncpy(cell->ch, str, sizeof(cell->ch) - 1);
	cell->ch[sizeof(cell->ch) - 1] = '\0';
	cell->attrs = attrs;
	cell->color_pair = cpair;
	cell->occupied = 1;
}

// Return the cell at absolute (x, y), or NULL if outside the grid's bounds.
static struct GridCell *grid_at(struct VirtualGrid *g, int x, int y) {
	int lx = x - g->anchor_x, ly = y - g->anchor_y;
	if (lx < 0 || lx >= g->width || ly < 0 || ly >= g->height) return NULL;
	return &g->cells[ly * g->width + lx];
}

void grid_blit_to_window(struct VirtualGrid *g, WINDOW *win, int ox, int oy) {
	int wh, ww;
	getmaxyx(win, wh, ww);
	for (int gy = 0; gy < g->height; gy++) {
		int wy = (g->anchor_y + gy) + oy;
		if (wy < 0 || wy >= wh) continue;
		for (int gx = 0; gx < g->width; gx++) {
			struct GridCell *cell = &g->cells[gy * g->width + gx];
			if (!cell->occupied) continue;
			int wx = (g->anchor_x + gx) + ox;
			if (wx < 0 || wx >= ww) continue;
			wattron(win, cell->attrs | COLOR_PAIR(cell->color_pair));
			mvwprintw(win, wy, wx, "%s", cell->ch);
			wattroff(win, cell->attrs | COLOR_PAIR(cell->color_pair));
		}
	}
}

void delObjects(struct ncursesObjects *objects) {
	if (objects->treePanel) del_panel(objects->treePanel);
	if (objects->messageBorderPanel) del_panel(objects->messageBorderPanel);
	if (objects->messagePanel) del_panel(objects->messagePanel);

	if (objects->treeWin) delwin(objects->treeWin);
	if (objects->messageBorderWin) delwin(objects->messageBorderWin);
	if (objects->messageWin) delwin(objects->messageWin);

	objects->treePanel = NULL;
	objects->messageBorderPanel = NULL;
	objects->messagePanel = NULL;
	objects->treeWin = NULL;
	objects->messageBorderWin = NULL;
	objects->messageWin = NULL;
}

void quit(struct config *conf, struct ncursesObjects *objects, int returnCode) {
	delObjects(objects);
	free(conf->saveFile);
	free(conf->loadFile);
	exit(returnCode);
}

int saveToFile(const struct config *conf, unsigned long long globalTime) {
	FILE *fp = fopen(conf->saveFile, "w");

	if (!fp) {
		printf("error: file was not opened properly for writing: %s\n", conf->saveFile);
		return 1;
	}

	fprintf(fp, "v%d %d %llu %ld %.6f %d %d %d", conf->version, conf->seed,
		globalTime, conf->creationTime, conf->secondsPerTick,
		conf->lifeStart, conf->multiplier, conf->baseType);
	fclose(fp);

	return 0;
}

// load seed and counter from file
int loadFromFile(struct config *conf) {
	FILE* fp = fopen(conf->loadFile, "r");

	if (!fp) {
		printf("error: file was not opened properly for reading: %s\n", conf->loadFile);
		return 1;
	}

	int seed;
	unsigned long long globalTime;
	time_t creationTime;
	double secondsPerTick;

	char first[16] = {0};
	if (fscanf(fp, "%15s", first) != 1) {
		printf("error: save file could not be read\n");
		fclose(fp);
		return 1;
	}

	if (first[0] == 'v') {
		conf->version = atoi(first + 1);
		if (fscanf(fp, "%i %llu %ld %lf", &seed, &globalTime, &creationTime, &secondsPerTick) != 4) {
			printf("error: save file could not be read\n");
			fclose(fp);
			return 1;
		}
	} else {
		conf->version = 1;
		seed = atoi(first);
		if (fscanf(fp, "%llu %ld %lf", &globalTime, &creationTime, &secondsPerTick) != 3) {
			printf("error: save file could not be read\n");
			fclose(fp);
			return 1;
		}
	}

	// optional extended fields (newer save files); older files simply
	// lack them, in which case CLI flags / defaults stay in effect
	int lifeStart, multiplier, baseType;
	if (fscanf(fp, "%d %d %d", &lifeStart, &multiplier, &baseType) == 3) {
		conf->lifeStart = lifeStart;
		conf->multiplier = multiplier;
		conf->baseType = baseType;
	}

	conf->seed = seed;
	conf->creationTime = creationTime;
	conf->secondsPerTick = secondsPerTick;
	conf->targetGlobalTime = globalTime;

	// Calculate current target time based on elapsed real time
	if (secondsPerTick > 0) {
		time_t currentTime = time(NULL);
		double elapsedSeconds = difftime(currentTime, creationTime);
		conf->targetGlobalTime = (unsigned long long)(elapsedSeconds / secondsPerTick);
		conf->timeStep = secondsPerTick;
		conf->live = 1;
		conf->proceduralMode = 1;
		conf->printTree = 0;
		conf->infinite = 0;
		conf->load = 1;
	} else {
		conf->targetGlobalTime = globalTime;
	}

	fclose(fp);
	return 0;
}

void finish(const struct config *conf, struct counters *myCounters) {
	clear();
	refresh();
	endwin();	// delete ncurses screen
	if (conf->save)
		saveToFile(conf, myCounters->globalTime);
}

void printHelp(void) {
	printf("%s",
		"Usage: cbonsai [OPTION]...\n"
			"\n"
			"cbonsai is a beautifully random bonsai tree generator.\n"
			"\n"
			"Options:\n"
			"  -l, --live             live mode: show each step of growth\n"
			"  -t, --time=TIME        in live mode, wait TIME secs between\n"
			"                           steps of growth (must be larger than 0) [default: 0.03]\n"
			"  -P, --procedural       enable procedural leaf generation mode;\n"
			"                           leaves grow incrementally using branch\n"
			"                           position history for realistic placement\n"
			"  -i, --infinite         infinite mode: keep growing trees\n"
			"  -w, --wait=TIME        in infinite mode, wait TIME between each tree\n"
			"                           generation [default: 4.00]\n"
			"  -S, --screensaver      screensaver mode; equivalent to -li,\n"
			"                           quits on any keypress, and handles\n"
			"                           terminal resizing gracefully\n"
			"  -m, --message=STR      attach message next to the tree\n"
			"  -T, --msgtime=SECS     clear message after SECS seconds\n"
			"                           [default: no timeout, message stays]\n"
			"  -b, --base=INT         ascii-art plant base to use:\n"
			"                           0 = none, 1 = large pot, 2 = small pot\n"
			"                           [default: 1]\n"
			"  -c, --leaf=LIST        list of comma-delimited strings randomly\n"
			"                           chosen for leaves [default: "
#ifdef _WIN32
			"#,#,*,.,\n"
#else
			"█,█,█,▒,▒]\n"
#endif
			"  -M, --multiplier=INT   branch multiplier; higher -> more\n"
			"                           branching (1-20) [default: 10]\n"
			"  -N, --name=TIME        create a named tree that grows over real\n"
			"                           time, where TIME is the full lifespan\n"
			"                           of the tree in seconds. MUST be used\n"
			"                           with -W to specify a save file.\n"
			"                           Automatically enables -l and -P.\n"
			"                           Use -C to load and continue growing\n"
			"                           a previously saved named tree.\n"
			"  -L, --life=INT         life; higher -> more growth (10-500) [default: 60]\n"
			"  -p, --print            print tree to terminal when finished\n"
			"  -s, --seed=INT         seed random number generator\n"
			"      --engine=INT       tree generation engine version for\n"
			"                           new trees (1 or 2) [default: 2];\n"
			"                           loaded trees use their saved version\n"
			"      --bare             suppress foliage; draw only the woody\n"
			"                           structure (v2 engine only; same tree,\n"
			"                           leaves hidden)\n"
			"  -W, --save=FILE        save progress to file\n"
			"                           [default: $XDG_CACHE_HOME/cbonsai\n"
			"                            or $HOME/.cache/cbonsai]\n"
			"  -C, --load=FILE        load progress from file\n"
			"                           [default: $XDG_CACHE_HOME/cbonsai\n"
			"                            or $HOME/.cache/cbonsai]\n"
			"  -v, --verbose          increase output verbosity\n"
			"  -h, --help             show help\n"
	);
}

static void grid_put_str(struct VirtualGrid *g, int tx, int ty, const char *str, attr_t attrs, short cpair) {
	for (int i = 0; str[i]; i++) {
		char ch[2] = { str[i], '\0' };
		grid_put(g, tx + i, ty, ch, attrs, cpair);
	}
}

int getBaseHeight(int baseType) {
	switch(baseType) {
	case 1: return 4;
	case 2: return 3;
	default: return 0;
	}
}

// Static part of the plant base (everything below the rim row).
// The rim row itself is drawn by drawPotRim so it can track the trunk.
void drawBaseToGrid(struct VirtualGrid *grid, int baseType, int trunk_x, int trunk_y) {
	if (baseType <= 0) return;

	int by = trunk_y + 1;

	switch(baseType) {
	case 1: {
		int bx = trunk_x - 31 / 2;
		grid_put_str(grid, bx, by + 1, " \\                           / ", A_BOLD, 8);
		grid_put_str(grid, bx, by + 2, "  \\_________________________/ ", A_BOLD, 8);
		grid_put_str(grid, bx, by + 3, "  (_)                     (_)", A_BOLD, 8);
		break;
	}
	case 2: {
		int bx = trunk_x - 15 / 2;
		grid_put_str(grid, bx, by + 1, " (           ) ", 0, 8);
		grid_put_str(grid, bx, by + 2, "  (_________)  ", 0, 8);
		break;
	}
	}
}

// Top row of the pot: grass, then a ./~...~\. flare hugging the trunk's
// actual span [span_lo, span_hi] on the row above. Redrawn whenever the
// trunk's footprint at ground level changes. RNG-free.
void drawPotRim(struct VirtualGrid *grid, int baseType, int trunk_x, int trunk_y,
				int span_lo, int span_hi) {
	if (baseType <= 0) return;

	int by = trunk_y + 1;

	switch(baseType) {
	case 1: {
		int bx = trunk_x - 31 / 2;
		// keep the flare (span plus . / \ . on each side) inside the pot mouth
		int lo = span_lo, hi = span_hi;
		if (lo < bx + 3) lo = bx + 3;
		if (hi > bx + 27) hi = bx + 27;

		grid_put(grid, bx, by, ":", A_BOLD, 8);
		for (int gx = bx + 1; gx < lo - 2; gx++)
			grid_put(grid, gx, by, "_", A_BOLD, 23);
		grid_put(grid, lo - 2, by, ".", A_BOLD, 20);
		grid_put(grid, lo - 1, by, "/", A_BOLD, 20);
		for (int gx = lo; gx <= hi; gx++)
			grid_put(grid, gx, by, "~", A_BOLD, 20);
		grid_put(grid, hi + 1, by, "\\", A_BOLD, 20);
		grid_put(grid, hi + 2, by, ".", A_BOLD, 20);
		for (int gx = hi + 3; gx < bx + 30; gx++)
			grid_put(grid, gx, by, "_", A_BOLD, 23);
		grid_put(grid, bx + 30, by, ":", A_BOLD, 8);
		break;
	}
	case 2: {
		int bx = trunk_x - 15 / 2;
		int lo = span_lo, hi = span_hi;
		if (lo < bx + 3) lo = bx + 3;
		if (hi > bx + 11) hi = bx + 11;

		grid_put(grid, bx, by, "(", 0, 8);
		for (int gx = bx + 1; gx < lo - 2; gx++)
			grid_put(grid, gx, by, "-", 0, 2);
		grid_put(grid, lo - 2, by, ".", 0, 11);
		grid_put(grid, lo - 1, by, "/", 0, 11);
		for (int gx = lo; gx <= hi; gx++)
			grid_put(grid, gx, by, "~", 0, 11);
		grid_put(grid, hi + 1, by, "\\", 0, 11);
		grid_put(grid, hi + 2, by, ".", 0, 11);
		for (int gx = hi + 3; gx < bx + 14; gx++)
			grid_put(grid, gx, by, "-", 0, 2);
		grid_put(grid, bx + 14, by, ")", 0, 8);
		break;
	}
	}
}

void drawWins(struct ncursesObjects *objects) {
	int rows, cols;
	getmaxyx(stdscr, rows, cols);

	delObjects(objects);

	objects->treeWin = newwin(rows, cols, 0, 0);
	objects->treePanel = new_panel(objects->treeWin);
}

// v2 counterpart of rand() % mod, drawing from an explicit msaw stream
static inline int mrand(struct msaw *st, int mod) {
	if (mod < 1) return 0;
	return (int)msaw_below(st, (uint32_t)mod);
}

// check for key press: 0=nothing, 1=quit, 2=resize
int checkKeyPress(const struct config *conf, struct counters *myCounters) {
	int ch = wgetch(stdscr);
	if (ch == KEY_RESIZE) return 2;
	if ((conf->screensaver && ch != ERR) || ch == 'q') {
		finish(conf, myCounters);
		return 1;
	}
	return 0;
}

// display changes (also sleeps)
void updateScreen(float timeStep) {
	update_panels();
	doupdate();

	// convert given time into seconds and nanoseconds and sleep
	struct timespec ts;
	ts.tv_sec = timeStep / 1;
	ts.tv_nsec = (timeStep - ts.tv_sec) * 1000000000;
	nanosleep(&ts, NULL);	// sleep for given time
}

// Helper function to interpolate between two color values
static inline int interpolate_color(int color1, int color2, float ratio) {
	return color2 * (1.0 - ratio) + color1 * ratio;
	//return color1 + (int)((color2 - color1) * ratio);
}

enum Season get_current_season_with_blend(float *blend_ratio) {
	time_t now = time(NULL);
	struct tm *local_time = localtime(&now);

	// Get day of year (0-365)
	int day_of_year = local_time->tm_yday;

	// Define season transitions (days of year)
	const int spring_start = 20;
	const int summer_start = 100;
	const int fall_start = 220;
	const int fall_late = 260;
	const int winter_start = 320;

	// Transition period (2 weeks = 14 days)
	int blend_period = 0;

	// Calculate current season and blend ratio
	enum Season current_season;
	int days_into_transition = 0;

	if (day_of_year >= winter_start || day_of_year < spring_start) {
		current_season = WINTER;
		blend_period = 10;
		if (day_of_year >= winter_start) {
			days_into_transition = day_of_year - winter_start;
		} else {
			days_into_transition = (365 - winter_start) + day_of_year;
		}
	} else if (day_of_year >= fall_start && day_of_year < fall_late) {
		current_season = EARLY_FALL;
		blend_period = 40;
		days_into_transition = day_of_year - fall_start;
	} else if (day_of_year >= fall_late) {
		current_season = LATE_FALL;
		blend_period = 25;
		days_into_transition = day_of_year - fall_late;
	} else if (day_of_year >= summer_start) {
		current_season = SUMMER;
		blend_period = 20;
		days_into_transition = day_of_year - summer_start;
	} else {
		current_season = SPRING;
		blend_period = 10;
		days_into_transition = day_of_year - spring_start;
	}

	if (((float)days_into_transition) / blend_period < 1.0f)
		*blend_ratio = ((float) days_into_transition) / blend_period;
	else
		*blend_ratio = 1.0;
	return current_season;
}

void initBranchList(struct BranchList* list) {
	list->capacity = 16;  // Initial capacity
	list->count = 0;
	list->branches = malloc(sizeof(struct Branch) * list->capacity);
}

void addBranch(struct BranchList* list, struct Branch branch, struct counters *myCounters) {
	myCounters->branches++;
	if (list->count >= list->capacity) {
		list->capacity *= 2;
		struct Branch *tmp = realloc(list->branches, sizeof(struct Branch) * list->capacity);
		if (!tmp) {
			list->capacity /= 2;
			return;
		}
		list->branches = tmp;
	}
	list->branches[list->count++] = branch;
}

void removeBranch(struct BranchList* list, int index) {
	if (index >= list->count) return;
	memmove(&list->branches[index], &list->branches[index + 1], 
			sizeof(struct Branch) * (list->count - index - 1));
	list->count--;
}

void freeBranchList(struct BranchList* list) {
	for (int i = 0; i < list->count; i++) {
		grid_destroy(list->branches[i].leafGrid);
		free(list->branches[i].walkers);
	}
	free(list->branches);
	list->branches = NULL;
	list->count = 0;
	list->capacity = 0;
}

static inline void update_position_history(struct Branch* branch) {
	// Add new position to history
	branch->x_history[branch->history_index] = branch->x;
	branch->y_history[branch->history_index] = branch->y;
	
	// Update count and index
	if (branch->history_count < BRANCH_HISTORY) {
		branch->history_count++;
	}
	branch->history_index = (branch->history_index + 1) % BRANCH_HISTORY;
}

static inline void get_average_position(struct Branch* branch, int* avg_x, int* avg_y) {
	if (branch->history_count == 0) {

		*avg_x = branch->x;
		*avg_y = branch->y;
		return;
	}
	int sum_x = 0, sum_y = 0;
	for (int i = 0; i < branch->history_count; i++) {
		sum_x += branch->x_history[i];
		sum_y += branch->y_history[i];
	}
	*avg_x = sum_x / branch->history_count;
	*avg_y = sum_y / branch->history_count;
}

// Check if we're in the early trunk phase (first 30% of life)
static inline int isEarlyTrunk(int age, int totalLife) {
	return age < (totalLife * 14 / 20);  // 70% of total life
}

// Check if we're in the young trunk phase (first 15% of life)
static inline int isYoungTrunk(int age, int totalLife) {
	return age < (totalLife * 3 / 20);  // 15% of total life
}

// Calculate branching roll threshold based on age (higher number = less likely to branch)
static inline int getBranchRollThreshold(int age, int totalLife, int multiplier) {
	// Base dice of 12, lower number = more branching
	int dice = 3;  // Base roll threshold (1/3 chance)

	if (isYoungTrunk(age, totalLife)) {
		dice = 12 - (multiplier/10);  // Only 1/12 chance during young phase
	} else {
		// We want LESS chance to branch as we go up
		int remainingLife = totalLife - age;



		if (remainingLife < totalLife / 4) {  // Near the top
			dice = 15 - (multiplier/4);  // Base 1/12 chance
		} else if (remainingLife < totalLife / 2) {  // Upper half
			dice = 10 - (multiplier/6);  // Base 1/8 chance
		} else {
			dice = 5 - (multiplier/10);  // Base 1/4 chance near bottom
		}
	}
	
	return dice;
}

void addSpaces(WINDOW* messageWin, int count, int *linePosition, int maxWidth) {
	// add spaces if there's enough space
	if (*linePosition < (maxWidth - count)) {
		/* if (verbosity) mvwprintw(treeWin, 12, 5, "inserting a space: linePosition: %02d", *linePosition); */

		// add spaces up to width
		for (int j = 0; j < count; j++) {
			wprintw(messageWin, " ");
			(*linePosition)++;
		}
	}
}

// create ncurses windows to contain message and message box
// returns 0 on success, 1 if terminal too small
int createMessageWindows(struct ncursesObjects *objects, char* message) {
	int maxY, maxX;
	getmaxyx(stdscr, maxY, maxX);

	if (maxX < 10 || maxY < 4) {
		objects->messageBorderWin = NULL;
		objects->messageWin = NULL;
		objects->messageBorderPanel = NULL;
		objects->messagePanel = NULL;
		return 1;
	}

	int boxWidth = 0;
	int boxHeight = 0;

	if (strlen(message) + 3 <= (0.25 * maxX)) {
		boxWidth = strlen(message) + 1;
		boxHeight = 1;
	} else {
		boxWidth = 0.25 * maxX;
		if (boxWidth < 4) boxWidth = 4;
		boxHeight = (strlen(message) / boxWidth) + (strlen(message) / boxWidth);
	}

	int borderW = boxWidth + 4;
	int borderH = boxHeight + 2;
	int borderY = (int)(maxY * 0.3) - 1;
	int borderX = (int)(maxX * 0.7) - 2;

	if (borderY < 0) borderY = 0;
	if (borderX < 0) borderX = 0;
	if (borderX + borderW > maxX) borderW = maxX - borderX;
	if (borderY + borderH > maxY) borderH = maxY - borderY;
	if (borderW < 3 || borderH < 3) {
		objects->messageBorderWin = NULL;
		objects->messageWin = NULL;
		objects->messageBorderPanel = NULL;
		objects->messagePanel = NULL;
		return 1;
	}

	int msgW = borderW - 3;
	int msgH = borderH - 2;
	int msgY = borderY + 1;
	int msgX = borderX + 2;

	objects->messageBorderWin = newwin(borderH, borderW, borderY, borderX);
	objects->messageWin = newwin(msgH, msgW, msgY, msgX);

	wattron(objects->messageBorderWin, COLOR_PAIR(8) | A_BOLD);
	wborder(objects->messageBorderWin, '|', '|', '-', '-', '+', '+', '+', '+');

	objects->messageBorderPanel = new_panel(objects->messageBorderWin);
	objects->messagePanel = new_panel(objects->messageWin);
	return 0;
}

int drawMessage(struct config *conf, struct ncursesObjects *objects, char* message) {
	if (!message) return 1;

	conf->messageStartTime = time(NULL);
	if (createMessageWindows(objects, message) != 0) return 1;

	int maxWidth = getmaxx(objects->messageWin) - 2;

	// word wrap message as it is written
	unsigned int i = 0;
	int linePosition = 0;
	int wordLength = 0;
	char wordBuffer[512] = {'\0'};
	char thisChar;
	while (true) {
		thisChar = message[i];
		if (conf->verbosity) {
			mvwprintw(objects->treeWin, 9, 5, "index: %03d", i);
			mvwprintw(objects->treeWin, 10, 5, "linePosition: %02d", linePosition);
		}

		// append this character to word buffer,
		// if it's not space or NULL and it can fit
		if (!(isspace(thisChar) || thisChar == '\0') && wordLength < (int) (sizeof(wordBuffer) / sizeof(wordBuffer[0]))) {
			strncat(wordBuffer, &thisChar, 1);
			wordLength++;
			linePosition++;
		}

		// if char is space or null char
		else if (isspace(thisChar) || thisChar == '\0') {

			// if current line can fit word, add word to current line
			if (linePosition <= maxWidth) {
				wprintw(objects->messageWin, "%s", wordBuffer);	// print word
				wordLength = 0;		// reset word length
				wordBuffer[0] = '\0';	// clear word buffer

				switch (thisChar) {
				case ' ':
					addSpaces(objects->messageWin, 1, &linePosition, maxWidth);
					break;
				case '\t':
					addSpaces(objects->messageWin, 1, &linePosition, maxWidth);
					break;
				case '\n':
					waddch(objects->messageWin, thisChar);
					linePosition = 0;
					break;
				}

			}

			// if word can't fit within a single line, just print it
			else if (wordLength > maxWidth) {
				wprintw(objects->messageWin, "%s ", wordBuffer);	// print word
				wordLength = 0;		// reset word length
				wordBuffer[0] = '\0';	// clear word buffer

				// our line position on this new line is the x coordinate
				int y;
				(void) y;
				getyx(objects->messageWin, y, linePosition);
			}

			// if current line can't fit word, go to next line
			else {
				if (conf->verbosity) mvwprintw(objects->treeWin, (i / 24) + 28, 5, "couldn't fit word. linePosition: %02d, wordLength: %02d", linePosition, wordLength);
				wprintw(objects->messageWin, "\n%s ", wordBuffer); // print newline, then word
				linePosition = wordLength;	// reset line position
				wordLength = 0;		// reset word length
				wordBuffer[0] = '\0';	// clear word buffer
			}
		}
		else {
			printf("%s", "Error while parsing message");
			return 1;
		}

		if (conf->verbosity >= 2) {
			updateScreen(1);
			mvwprintw(objects->treeWin, 11, 5, "word buffer: |%15s|", wordBuffer);
		}
		if (thisChar == '\0') break;	// quit when we reach the end of the message
		i++;
	}
	return 0;
}

void clearMessage(struct ncursesObjects *objects) {
	if (objects->messagePanel) {
		del_panel(objects->messagePanel);
		objects->messagePanel = NULL;
	}
	if (objects->messageBorderPanel) {
		del_panel(objects->messageBorderPanel);
		objects->messageBorderPanel = NULL;
	}
	if (objects->messageWin) {
		delwin(objects->messageWin);
		objects->messageWin = NULL;
	}
	if (objects->messageBorderWin) {
		delwin(objects->messageBorderWin);
		objects->messageBorderWin = NULL;
	}

	// Update display
	update_panels();
	doupdate();
}

void init(struct config *conf, struct ncursesObjects *objects) {
	savetty();	// save terminal settings
	initscr();	// init ncurses screen
	noecho();	// don't echo input to screen
	curs_set(0);	// make cursor invisible
	cbreak();	// don't wait for new line to grab user input
	nodelay(stdscr, TRUE);	// force getch to be a non-blocking call

	// use native background color when possible
	int bg = COLOR_BLACK;
	if (use_default_colors() != ERR) bg = -1;

	// if terminal has color capabilities, use them
	if (has_colors()) {
		start_color();

		// define color pairs 0-15
		for(int i=0; i<16; i++){
			init_pair(i, i, bg);
		}

		// restrict color palette in non-256color terminals (e.g. screen, cygwin, linux console)
		if (COLORS < 256) {
			init_pair(8, 7, bg);	// gray will look white
			init_pair(9, 1, bg);
			init_pair(10, 2, bg);
			init_pair(11, 3, bg);
			init_pair(12, 4, bg);
			init_pair(13, 5, bg);
			init_pair(14, 6, bg);
			init_pair(15, 7, bg);
		}

		if (can_change_color() && COLORS >= 256) {
			// Full 256-color terminal: define custom seasonal RGB colors
			float blend_ratio;
			enum Season season = get_current_season_with_blend(&blend_ratio);

			struct ColorRGB current_colors = season_colors[season];
			struct ColorRGB prev_colors = season_colors[(season + 4) % 5];

			int r   = interpolate_color(current_colors.r,   prev_colors.r,   blend_ratio);
			int g   = interpolate_color(current_colors.g,   prev_colors.g,   blend_ratio);
			int b   = interpolate_color(current_colors.b,   prev_colors.b,   blend_ratio);
			int r_2 = interpolate_color(current_colors.r_2, prev_colors.r_2, blend_ratio);
			int g_2 = interpolate_color(current_colors.g_2, prev_colors.g_2, blend_ratio);
			int b_2 = interpolate_color(current_colors.b_2, prev_colors.b_2, blend_ratio);

			init_color(16, 540, 270, 0);     // Lighter brown for trunk
			init_color(17, 280, 140, 0);     // Darker brown for branches
			init_color(18, r,   g,   b);     // Main leaf color
			init_color(19, r_2, g_2, b_2);  // Darker leaf variant
			init_color(25, 560, 510, 420);  // Weathered deadwood (jin/shari) — muted driftwood

			init_pair(20, 16, bg);  // Trunk color
			init_pair(21, 17, bg);  // Branch color
			init_pair(22, 18, bg);  // Leaf color 1
			init_pair(23, 19, bg);  // Leaf color 2
			init_pair(24, 25, bg);  // Deadwood (bleached)
		} else {
			// Limited terminal (cygwin, screen, linux console, ssh with basic TERM):
			// fall back to the 8 standard colors, season-aware
			float blend_ratio;
			enum Season season = get_current_season_with_blend(&blend_ratio);

			short trunk_color, leaf_color_1, leaf_color_2;
			switch (season) {
				case EARLY_FALL:
					trunk_color  = COLOR_YELLOW;
					leaf_color_1 = COLOR_YELLOW;
					leaf_color_2 = COLOR_YELLOW;
					break;
				case LATE_FALL:
					trunk_color  = COLOR_RED;
					leaf_color_1 = COLOR_RED;
					leaf_color_2 = COLOR_YELLOW;
					break;
				case WINTER:
					trunk_color  = COLOR_WHITE;
					leaf_color_1 = COLOR_WHITE;
					leaf_color_2 = COLOR_WHITE;
					break;
				case SPRING:
				case SUMMER:
				default:
					trunk_color  = COLOR_YELLOW;
					leaf_color_1 = COLOR_GREEN;
					leaf_color_2 = COLOR_GREEN;
					break;
			}
			init_pair(20, trunk_color,  bg);
			init_pair(21, trunk_color,  bg);
			init_pair(22, leaf_color_1, bg);
			init_pair(23, leaf_color_2, bg);
			init_pair(24, COLOR_WHITE,  bg);  // Deadwood (bleached -> white)
		}
	}
	// else: no color support at all — pairs remain at defaults, tree renders in terminal's default color

	// define and draw windows, then create panels
	drawWins(objects);
	drawMessage(conf, objects, conf->message);
}

void recalculate_offsets(int trunk_x, int trunk_y, int baseHeight, WINDOW *win, int *ox, int *oy) {
	int h, w;
	getmaxyx(win, h, w);
	*ox = (w / 2) - trunk_x;
	*oy = (h - 1 - baseHeight) - trunk_y;
}

void handleResize(struct config *conf, struct ncursesObjects *objects,
				  int trunk_x, int trunk_y, int baseHeight, int *off_x, int *off_y) {
	endwin();
	refresh();
	drawWins(objects);
	if (conf->message) {
		time_t saved = conf->messageStartTime;
		drawMessage(conf, objects, conf->message);
		conf->messageStartTime = saved;
	}
	recalculate_offsets(trunk_x, trunk_y, baseHeight, objects->treeWin, off_x, off_y);
}

void blitTree(struct VirtualGrid *skeleton, struct VirtualGrid *trunkPlane, int trunk_y,
			  struct BranchList *branchList,
			  struct ncursesObjects *objects, int off_x, int off_y) {
	werase(objects->treeWin);
	if (trunkPlane)
		drawWidenedTrunk(trunkPlane, objects->treeWin, trunk_y, off_x, off_y);
	grid_blit_to_window(skeleton, objects->treeWin, off_x, off_y);
	for (int i = 0; i < branchList->count; i++) {
		if (branchList->branches[i].leafGrid)
			grid_blit_to_window(branchList->branches[i].leafGrid, objects->treeWin, off_x, off_y);
	}
}

// One live-mode display step: blit, verbose output, screen update, then
// sleep conf->timeStep while handling message timeout, quit and resize.
// RNG-free: shared by all engines. Returns 1 if the user quit (caller
// must free its state and exit), 0 otherwise.
int liveStepDisplay(struct config *conf, struct ncursesObjects *objects,
					struct VirtualGrid *skeleton, struct VirtualGrid *trunkPlane,
					struct BranchList *branchList,
					struct counters *myCounters,
					int trunk_x, int trunk_y, int baseHeight,
					int *off_x, int *off_y, int maxX, int maxY, int turn) {
	if (conf->no_disp) return 0;

	blitTree(skeleton, trunkPlane, trunk_y, branchList, objects, *off_x, *off_y);
	if (conf->verbosity > 0) {
		struct Branch *db = &branchList->branches[turn > 0 ? turn - 1 : 0];
		mvwprintw(objects->treeWin, 2, 5, "maxX: %03d, maxY: %03d", maxX, maxY);
		mvwprintw(objects->treeWin, 5, 5, "dx: %02d", db->dx);
		mvwprintw(objects->treeWin, 6, 5, "dy: %02d", db->dy);
		mvwprintw(objects->treeWin, 7, 5, "type: %d", db->type);
		mvwprintw(objects->treeWin, 8, 5, "shootCooldown: % 3d", db->shootCooldown);
		mvwprintw(objects->treeWin, 9, 5, "globalTime: %llu", myCounters->globalTime);
		mvwprintw(objects->treeWin, 10, 5, "seed: %u", conf->seed);
	}
	update_panels();
	doupdate();

	float remaining = conf->timeStep;
	while (remaining > 0) {
		float sleepTime = (remaining > 0.2f) ? 0.2f : remaining;

		if (conf->messageTimeout > 0 && conf->message != NULL && objects->messagePanel != NULL) {
			if (time(NULL) - conf->messageStartTime >= conf->messageTimeout) {
				clearMessage(objects);
				conf->message = NULL;
			}
		}

		struct timespec ts;
		ts.tv_sec = (time_t)(sleepTime);
		ts.tv_nsec = (long)((sleepTime - ts.tv_sec) * 1000000000);
		nanosleep(&ts, NULL);

		int key = checkKeyPress(conf, myCounters);
		if (key == 1) return 1;
		if (key == 2) {
			handleResize(conf, objects, trunk_x, trunk_y, baseHeight, off_x, off_y);
			blitTree(skeleton, trunkPlane, trunk_y, branchList, objects, *off_x, *off_y);
			update_panels();
			doupdate();
		}

		remaining -= sleepTime;
	}
	return 0;
}

// Hold the finished tree on screen until a real keypress; terminal
// resizes just re-center and redraw it. RNG-free: shared by all engines.
void finalHold(struct config *conf, struct ncursesObjects *objects,
			   struct VirtualGrid *skeleton, struct VirtualGrid *trunkPlane,
			   struct BranchList *branchList,
			   int trunk_x, int trunk_y, int baseHeight, int *off_x, int *off_y) {
	if (conf->no_disp || conf->infinite) return;

	nodelay(stdscr, FALSE);
	while (wgetch(stdscr) == KEY_RESIZE) {
		handleResize(conf, objects, trunk_x, trunk_y, baseHeight, off_x, off_y);
		blitTree(skeleton, trunkPlane, trunk_y, branchList, objects, *off_x, *off_y);
		update_panels();
		doupdate();
	}
	nodelay(stdscr, TRUE);
}


// ==========================================================================
// V1 ENGINE  (FROZEN — original global-rand growth; do not modify)
// ==========================================================================

// roll (randomize) a given die
static inline void roll(int *dice, int mod) { *dice = rand() % mod; }

struct ColorResult chooseColorResult(enum branchType type) {
	struct ColorResult cr = {0, 0};
	int r;
	switch(type) {
	case trunk:
		r = rand() % 6;
		if (r < 3) { cr.attrs = A_BOLD; cr.color_pair = 20; }
		else if (r < 5) { cr.color_pair = 20; }
		else { cr.color_pair = 21; }
		break;

	case shootLeft:
	case shootRight:
		r = rand() % 10;
		if (r < 2) { cr.attrs = A_BOLD; cr.color_pair = 20; }
		else if (r < 6) { cr.attrs = A_BOLD; cr.color_pair = 21; }
		else { cr.color_pair = 21; }
		break;

	case dying:
		r = rand() % 6;
		if (r < 3) { cr.color_pair = 22; }
		else if (r < 5) { cr.attrs = A_BOLD; cr.color_pair = 22; }
		else { cr.color_pair = 23; }
		break;

	case dead:
		r = rand() % 18;
		if (r < 2) { cr.attrs = A_BOLD; cr.color_pair = 23; }
		else if (r < 8) { cr.attrs = A_BOLD; cr.color_pair = 22; }
		else { cr.color_pair = 22; }
		break;
	}
	return cr;
}

// determine change in X and Y coordinates of a given branch
void setDeltas(enum branchType type, int life, int totalLife, int age, int multiplier, int *returnDx, int *returnDy) {
	int dx = 0;
	int dy = 0;
	int dice;
	switch (type) {
	case trunk: // trunk

		// new or dead trunk
		if (age <= 2 || life < 4) {
			dy = 0;
			dx = (rand() % 3) - 1;
		}
		// young trunk should grow wide]
		else if (isYoungTrunk(age, totalLife)) {
			// every (multiplier * 0.6) steps, raise tree to next level
			int step = (int)(multiplier * 0.6);
			if (step < 1) step = 1;
			if (age % step == 0) dy = -1;
			else dy = 0;

			roll(&dice, 10);
			if (dice >= 0 && dice <=0) dx = -2;			// 10%
			else if (dice >= 1 && dice <= 3) dx = -1;	// 30%
			else if (dice >= 4 && dice <= 5) dx = 0;	// 20%
			else if (dice >= 6 && dice <= 8) dx = 1;	// 30%
			else if (dice >= 9 && dice <= 9) dx = 2;	// 10%
		}
		else if (isEarlyTrunk(age, totalLife)) {
			// every (multiplier * 0.3) steps, raise tree to next level
			int step = (int)(multiplier * 0.3);
			if (step < 1) step = 1;
			if (age % step == 0) dy = -1;
			else dy = 0;

			roll(&dice, 10);
			if (dice >= 0 && dice <=0) dx = -2;			// 10%
			else if (dice >= 1 && dice <= 3) dx = -1;	// 30%
			else if (dice >= 4 && dice <= 5) dx = 0;	// 20%
			else if (dice >= 6 && dice <= 8) dx = 1;	// 30%
			else if (dice >= 9 && dice <= 9) dx = 2;	// 10%
		}
		// old-aged trunk
		else {
			roll(&dice, 10);
			if (dice > 4) dy = -1;
			else dy = 0;
			
			roll(&dice, 20);
			if (dice >= 0 && dice <=0) dx = -2;			// 10%
			else if (dice >= 1 && dice <= 7) dx = -1;	// 30%
			else if (dice >= 8 && dice <= 12) dx = 0;	// 20%
			else if (dice >= 13 && dice <= 18) dx = 1;	// 30%
			else if (dice >= 19 && dice <= 19) dx = 2;	// 10%
		}
		break;

	case 1: // left shoot: trend left and little vertical movement
		roll(&dice, 10);
		if (dice >= 0 && dice <= 2) dy = -1;
		else if (dice >= 3 && dice <= 7) dy = 0;
		else if (dice >= 8 && dice <= 9) dy = 1;

		roll(&dice, 10);
		if (dice >= 0 && dice <=1) dx = -2;
		else if (dice >= 2 && dice <= 5) dx = -1;
		else if (dice >= 6 && dice <= 8) dx = 0;
		else if (dice >= 9 && dice <= 9) dx = 1;
		break;

	case 2: // right shoot: trend right and little vertical movement
		roll(&dice, 10);
		if (dice >= 0 && dice <= 2) dy = -1;
		else if (dice >= 3 && dice <= 7) dy = 0;
		else if (dice >= 8 && dice <= 9) dy = 1;

		roll(&dice, 10);
		if (dice >= 0 && dice <=1) dx = 2;
		else if (dice >= 2 && dice <= 5) dx = 1;
		else if (dice >= 6 && dice <= 8) dx = 0;
		else if (dice >= 9 && dice <= 9) dx = -1;
		break;

	case 3: // dying: discourage vertical growth(?); trend left/right (-3,3)
		roll(&dice, 10);
		if (dice >= 0 && dice <=0) dy = -1;
		else if (dice >= 1 && dice <=8) dy = 0;
		else if (dice >= 9 && dice <=9) dy = 1;

		roll(&dice, 15);
		if (dice >= 0 && dice <=0) dx = -3;
		else if (dice >= 1 && dice <= 2) dx = -2;
		else if (dice >= 3 && dice <= 5) dx = -1;
		else if (dice >= 6 && dice <= 8) dx = 0;
		else if (dice >= 9 && dice <= 11) dx = 1;
		else if (dice >= 12 && dice <= 13) dx = 2;
		else if (dice >= 14 && dice <= 14) dx = 3;
		break;

	case 4: // dead: fill in surrounding area
		roll(&dice, 12);
		if (dice >= 0 && dice <= 1) dy = -1;
		else if (dice >= 2 && dice <= 8) dy = 0;
		else if (dice >= 9 && dice <= 11) dy = 1;
		
		roll(&dice, 15);
		if (dice >= 0 && dice <=1) dx = -3;
		else if (dice >= 2 && dice <= 3) dx = -2;
		else if (dice >= 4 && dice <= 5) dx = -1;
		else if (dice >= 6 && dice <= 8) dx = 0;
		else if (dice >= 9 && dice <= 10) dx = 1;
		else if (dice >= 11 && dice <= 12) dx = 2;
		else if (dice >= 13 && dice <= 14) dx = 3;
		break;
	}

	*returnDx = dx;
	*returnDy = dy;
}

char* chooseString(const struct config *conf, enum branchType type, int life, int dx, int dy) {
	char* branchStr;

	const unsigned int maxStrLen = 32;

	branchStr = malloc(maxStrLen);
	strcpy(branchStr, "?");	// fallback character

	if (life < 4) type = dying;

	switch(type) {
	case trunk:
		if (dy == 0) strcpy(branchStr, "/~");
		else if (dx < 0) strcpy(branchStr, "\\|");
		else if (dx == 0) strcpy(branchStr, "/|\\");
		else if (dx > 0) strcpy(branchStr, "|/");
		break;
	case shootLeft:
		if (dy > 0) strcpy(branchStr, "\\");
		else if (dy == 0) strcpy(branchStr, "\\_");
		else if (dx < 0) strcpy(branchStr, "\\|");
		else if (dx == 0) strcpy(branchStr, "/|");
		else if (dx > 0) strcpy(branchStr, "/");
		break;
	case shootRight:
		if (dy > 0) strcpy(branchStr, "/");
		else if (dy == 0) strcpy(branchStr, "_/");
		else if (dx < 0) strcpy(branchStr, "\\|");
		else if (dx == 0) strcpy(branchStr, "/|");
		else if (dx > 0) strcpy(branchStr, "/");
		break;
	case dying:
	case dead:
		strncpy(branchStr, conf->leaves[rand() % conf->leavesSize], maxStrLen - 1);
		branchStr[maxStrLen - 1] = '\0';
	}

	return branchStr;
}

void updateBranch_v1(struct config *conf, struct VirtualGrid *skeleton,
				struct counters *myCounters, int branchIdx,
				struct BranchList* list) {

	struct Branch *branch = &list->branches[branchIdx];
	branch->life--;     // decrement remaining life counter

	// Random die-off check - more likely on shoots
	if (branch->type == trunk) {
		if (rand() % 66 == 0) {   // 2% chance for trunk
			branch->life -= (branch->life/2);  // Lose 1/4 of life
		}
	} else if (branch->type == shootLeft || branch->type == shootRight) {
		if (rand() % 20 == 0) {    // 5% chance for shoots
			branch->life /= 2;      // Lose half of life
		}
	}

	branch->age++;

	setDeltas(branch->type, branch->life, branch->totalLife,
			  branch->age, branch->multiplier, &branch->dx, &branch->dy);

	int groundY = skeleton->anchor_y + skeleton->height - getBaseHeight(conf->baseType);
	if (branch->dy > 0 && branch->y > (groundY - 6))
		branch->dy--;

	// near-dead branch should branch into a lot of leaves
	if (branch->life < 6) {
		struct Branch newBranch = {
			.x = branch->x,
			.y = branch->y,
			.type = dead,
			.life = branch->life,
			.age = 0,
			.totalLife = branch->life,
			.multiplier = branch->multiplier,
			.shootCooldown = conf->multiplier,
			.dripLeafCooldown = branch->life / 4,
			.leaf_seed = rand(),
			.history_count = 0,
			.history_index = 0,
			.x_history[0] = branch->x,
			.y_history[0] = branch->y
		};
		addBranch(list, newBranch, myCounters);
		branch = &list->branches[branchIdx];
	}
	else if (branch->type == shootLeft || branch->type == shootRight) {
		if (branch->life < 7 + (branch->multiplier /5)) {
			struct Branch newBranch = {
				.x = branch->x,
				.y = branch->y,
				.type = dying,
				.life = branch->life + 1,
				.age = 0,
				.totalLife = branch->life + 1,
				.multiplier = branch->multiplier,
				.shootCooldown = conf->multiplier,
				.dripLeafCooldown = (branch->life + 1) / 4,
				.leaf_seed = rand(),
				.history_count = 0,
				.history_index = 0,
				.x_history[0] = branch->x,
				.y_history[0] = branch->y
			};
			addBranch(list, newBranch, myCounters);
			branch = &list->branches[branchIdx];
		}
		else if (branch->dripLeafCooldown <= 0 && (rand() % 3) == 0) {
			struct Branch newBranch = {
				.x = branch->x,
				.y = branch->y,
				.type = dying,
				.life = 5,
				.age = 0,
				.totalLife = 5,
				.multiplier = branch->multiplier,
				.shootCooldown = conf->multiplier,
				.dripLeafCooldown = (branch->multiplier*2)/3,
				.leaf_seed = rand(),
				.history_count = 0,
				.history_index = 0,
				.x_history[0] = branch->x,
				.y_history[0] = branch->y
			};
			addBranch(list, newBranch, myCounters);
			branch = &list->branches[branchIdx];
			branch->dripLeafCooldown = 7+ (25 + branch->multiplier);
		}
	}
	// dying trunk should branch into a lot of leaves
	else if (branch->type == trunk && branch->life < (branch->multiplier + 2)) {
		struct Branch newBranch = {
			.x = branch->x,
			.y = branch->y,
			.type = dying,
			.life = branch->life,
			.age = 0,
			.totalLife = branch->life,
			.multiplier = branch->multiplier,
			.shootCooldown = conf->multiplier,
			.dripLeafCooldown = branch->life / 4,
			.leaf_seed = rand(),
			.history_count = 0,
			.history_index = 0,
			.x_history[0] = branch->x,
			.y_history[0] = branch->y
		};
		addBranch(list, newBranch, myCounters);
		branch = &list->branches[branchIdx];
	}
	// dying shoot should branch into a lot of leaves
	else if ((branch->type == shootLeft || branch->type == shootRight) && 
			 branch->life < (branch->multiplier + 2)) {
		struct Branch newBranch = {
			.x = branch->x,
			.y = branch->y,
			.type = dying,
			.life = branch->life,
			.age = 0,
			.totalLife = branch->life,
			.multiplier = branch->multiplier,
			.shootCooldown = conf->multiplier,
			.dripLeafCooldown = branch->life / 4,
			.leaf_seed = rand(),
			.history_count = 0,
			.history_index = 0,
			.x_history[0] = branch->x,
			.y_history[0] = branch->y
		};
		addBranch(list, newBranch, myCounters);
		branch = &list->branches[branchIdx];
	}
	else if (branch->type == trunk) {
		branch->life--;
		// First check for trunk splits - only in early phase and with enough life
		if (!isYoungTrunk(branch->age, branch->totalLife)) {
			int splitThreshold = (24 - branch->multiplier) + (2 * myCounters->trunks);
			double ageRatio = (double)branch->age / branch->totalLife;

			if (ageRatio < 0.1) {      // Bottom 10%
				splitThreshold = (splitThreshold * 2)/7;
			} else if (ageRatio < 0.4) {
				splitThreshold = (splitThreshold * 3)/7;
			} else {
				splitThreshold = (splitThreshold * 5)/7;
			}

			if (myCounters->trunkSplitCooldown < 0 && rand() % splitThreshold == 0) {
				myCounters->trunkSplitCooldown = 2 + ((22 - conf->multiplier)*3)/4 +
					(int)(5 * ((double)branch->totalLife - branch->age)/branch->totalLife);
				myCounters->trunks++;
				branch->shootCooldown = (25 - branch->multiplier)/4;
				struct Branch newBranch = {
					.x = branch->x,
					.y = branch->y,
					.type = trunk,
					.life = branch->life - (rand() % 6),
					.age = 0,
					.totalLife = branch->life - (rand() % 6),
					.multiplier = branch->multiplier,
					.shootCooldown = conf->multiplier,
					.dripLeafCooldown = branch->life / 4,
					.leaf_seed = rand(),
					.history_count = 0,
					.history_index = 0,
					.x_history[0] = branch->x,
					.y_history[0] = branch->y
				};
				addBranch(list, newBranch, myCounters);
				branch = &list->branches[branchIdx];
				branch->life -= rand() % 1+ (int)(5 * ((double)branch->totalLife - branch->age)/branch->totalLife); // cost of splitting
			}
		}

		// Then check for regular branch shoots
		int branchDice = getBranchRollThreshold(branch->age, branch->totalLife, branch->multiplier);
		if (branch->shootCooldown <= 0 && (rand() % branchDice == 0)) {
			branch->shootCooldown = myCounters->trunks + (25 - branch->multiplier)/6;
			int shootLife = ((branch->life * 3)/4 + (rand() % branch->multiplier) - 2);
			
			myCounters->shoots++;
			myCounters->shootCounter++;
			(void)0; // verbose shoot count displayed during render
			
			struct Branch newBranch = {
				.x = branch->x,
				.y = branch->y,
				.type = (enum branchType)((myCounters->shootCounter % 2) + 1),
				.life = shootLife,
				.age = 0,
				.totalLife = shootLife,
				.multiplier = branch->multiplier,
				.shootCooldown = conf->multiplier,
				.dripLeafCooldown = shootLife / 4,
				.leaf_seed = rand(),
				.history_count = 0,
				.history_index = 0,
				.x_history[0] = branch->x,
				.y_history[0] = branch->y
			};
			addBranch(list, newBranch, myCounters);
			branch = &list->branches[branchIdx];

			branch->life -= rand() % 3; // cost of sprouting
		}
	}
	myCounters->trunkSplitCooldown--;
	branch->shootCooldown--;
	branch->dripLeafCooldown--;

	// move in x and y directions
	branch->x += branch->dx;
	branch->y += branch->dy;
	if(conf->proceduralMode && branch->type != dying && branch->type != dead)
		update_position_history(branch);

	enum branchType displayType = (branch->life < 4) ? dying : branch->type;
	struct ColorResult cr = chooseColorResult(displayType);

	// choose string to use for this branch
	char *branchStr = chooseString(conf, displayType, branch->life, branch->dx, branch->dy);

	// grab wide character from branchStr
	wchar_t wc = 0;
	mbstate_t ps = {0};
	mbrtowc(&wc, branchStr, 32, &ps);

	// write to grid, but ensure wide characters don't overlap
	int w = wcwidth(wc);
	if (w <= 0) w = 1;
	if(branch->x % w == 0) {
		grid_put(skeleton, branch->x, branch->y, branchStr, cr.attrs, cr.color_pair);
	}

	free(branchStr);
}

static void leafStepWalkers(struct config *conf, struct VirtualGrid *grid,
							enum branchType type, int groundY,
							struct LeafWalker **walkers, int *count, int *capacity) {
	int prev_count = *count;
	for (int w = 0; w < prev_count; w++) {
		struct LeafWalker *wk = &(*walkers)[w];

		int dx = 0, dy = 0, dice;
		switch (type) {
		case dying:
			dice = rand_r(&wk->seed) % 10;
			if (dice >= 0 && dice <= 0) dy = -1;
			else if (dice >= 1 && dice <= 8) dy = 0;
			else if (dice >= 9 && dice <= 9) dy = 1;

			dice = rand_r(&wk->seed) % 15;
			if (dice >= 0 && dice <= 0) dx = -3;
			else if (dice >= 1 && dice <= 2) dx = -2;
			else if (dice >= 3 && dice <= 5) dx = -1;
			else if (dice >= 6 && dice <= 8) dx = 0;
			else if (dice >= 9 && dice <= 11) dx = 1;
			else if (dice >= 12 && dice <= 13) dx = 2;
			else if (dice >= 14 && dice <= 14) dx = 3;
			break;
		case dead:
			dice = rand_r(&wk->seed) % 12;
			if (dice >= 0 && dice <= 1) dy = -1;
			else if (dice >= 2 && dice <= 8) dy = 0;
			else if (dice >= 9 && dice <= 11) dy = 1;

			dice = rand_r(&wk->seed) % 15;
			if (dice >= 0 && dice <= 1) dx = -3;
			else if (dice >= 2 && dice <= 3) dx = -2;
			else if (dice >= 4 && dice <= 5) dx = -1;
			else if (dice >= 6 && dice <= 8) dx = 0;
			else if (dice >= 9 && dice <= 10) dx = 1;
			else if (dice >= 11 && dice <= 12) dx = 2;
			else if (dice >= 13 && dice <= 14) dx = 3;
			break;
		default:
			break;
		}

		if (dy > 0 && wk->y > (groundY - 2))
			dy--;

		unsigned int child_seed = rand_r(&wk->seed);
		if (*count < 4096) {
			if (*count >= *capacity) {
				*capacity *= 2;
				*walkers = realloc(*walkers, sizeof(struct LeafWalker) * (size_t)*capacity);
				wk = &(*walkers)[w];
			}
			(*walkers)[(*count)++] = (struct LeafWalker){.x = wk->x, .y = wk->y, .seed = child_seed};
		}

		wk->x += dx;
		wk->y += dy;

		if (wk->y >= 0 && wk->y < groundY) {
			attr_t la = 0;
			short lc = 0;
			switch (type) {
			case dying:
				if (rand_r(&wk->seed) % 6 == 0) { lc = 22; }
				else if (rand_r(&wk->seed) % 2 == 0) { la = A_BOLD; lc = 23; }
				else { lc = 23; }
				break;
			case dead:
				if (rand_r(&wk->seed) % 7 == 0) { la = A_BOLD; lc = 22; }
				else if (rand_r(&wk->seed) % 2 == 0) { la = A_BOLD; lc = 23; }
				else { lc = 23; }
				break;
			default:
				break;
			}

			grid_put(grid, wk->x, wk->y, conf->leaves[rand_r(&wk->seed) % conf->leavesSize], la, lc);
		}
	}
}

void generateLeaves_v1(struct config *conf, struct VirtualGrid *grid, enum branchType type, int x, int y, int life, unsigned int leaf_seed, int groundY) {
	int capacity = 16;
	int count = 1;
	struct LeafWalker *walkers = malloc(sizeof(struct LeafWalker) * (size_t)capacity);
	walkers[0] = (struct LeafWalker){.x = x, .y = y, .seed = leaf_seed};

	for (int step = 0; step < life; step++) {
		leafStepWalkers(conf, grid, type, groundY, &walkers, &count, &capacity);
	}

	free(walkers);
}

// v1 engine: frozen. Consumes the global rand() stream seeded by srand();
// any change to its rand() call sequence breaks replay of saved v1 trees.
void growTree_v1(struct config *conf, struct ncursesObjects *objects, struct counters *myCounters) {
	int maxY, maxX;
	getmaxyx(objects->treeWin, maxY, maxX);

	int baseHeight = getBaseHeight(conf->baseType);
	struct VirtualGrid *skeleton = grid_create(maxX, maxY + baseHeight, 0, 0);
	struct VirtualGrid *trunkPlane = grid_create(maxX, maxY + baseHeight, 0, 0);
	struct VirtualGrid *renderPlane = NULL;  // v1: never render the widened trunk (frozen look)
	int trunk_x = maxX / 2;
	int trunk_y = maxY - 1 - baseHeight;
	int off_x = 0, off_y = 0;
	int rimLo = trunk_x, rimHi = trunk_x;

	drawBaseToGrid(skeleton, conf->baseType, trunk_x, trunk_y);
	drawPotRim(skeleton, conf->baseType, trunk_x, trunk_y, rimLo, rimHi);

	struct BranchList branchList;
	initBranchList(&branchList);

	myCounters->trunks = 0;
	myCounters->shoots = 0;
	myCounters->branches = 0;
	myCounters->shootCounter = 5;
	myCounters->globalTime = 0;
	myCounters->trunkSplitCooldown = 0;

	struct Branch initialBranch = {
		.x = trunk_x,
		.y = trunk_y,
		.life = conf->lifeStart,
		.age = 0,
		.type = trunk,
		.shootCooldown = conf->multiplier,
		.dripLeafCooldown = conf->multiplier + conf->lifeStart / 4,
		.totalLife = conf->lifeStart,
		.multiplier = conf->multiplier
	};
	addBranch(&branchList, initialBranch, myCounters);

	int turn = 0;
	while (branchList.count > 0) {
		myCounters->globalTime++;

		if (branchList.branches[turn].life <= 0) {
			struct Branch* b = &branchList.branches[turn];
			if (conf->proceduralMode &&
				b->type != dying && b->type != dead &&
				b->totalLife > 0) {
				double lifeRatio = ((double)b->age) / b->totalLife;
				unsigned int leaf_seed = b->leaf_seed;

				int avg_x, avg_y;
				get_average_position(b, &avg_x, &avg_y);

				int log_factor = 0, dummy = b->age;
				while(dummy > 0) {
					log_factor++;
					dummy >>= 1;
				}

				int leafLife = log_factor + lifeRatio * ((b->type == trunk) ? 4 : 3);
				enum branchType newType = (b->type == trunk) ? dead : dying;

				generateLeaves_v1(conf, skeleton, newType, avg_x, avg_y, leafLife, leaf_seed, trunk_y + 1);
			}

			free(b->walkers);
			b->walkers = NULL;
			b->walker_count = 0;
			b->walker_capacity = 0;
			grid_destroy(b->leafGrid);
			b->leafGrid = NULL;

			removeBranch(&branchList, turn);
			if (turn >= branchList.count) {
				turn = 0;
			}
			continue;
		}

		updateBranch_v1(conf, skeleton, myCounters, turn, &branchList);

		// record trunk cells in the trunk plane (tracking only, never
		// blitted) and keep the pot rim hugging the trunk's footprint
		{
			struct Branch *ub = &branchList.branches[turn];
			if (ub->type == trunk) {
				grid_put(trunkPlane, ub->x, ub->y, "#", 0, 0);
				if (ub->y == trunk_y && (ub->x < rimLo || ub->x > rimHi)) {
					if (ub->x < rimLo) rimLo = ub->x;
					if (ub->x > rimHi) rimHi = ub->x;
					drawPotRim(skeleton, conf->baseType, trunk_x, trunk_y, rimLo, rimHi);
				}
			}
		}

		if (conf->live && conf->proceduralMode) {
			for (int i = 0; i < branchList.count; i++) {
				struct Branch* b = &branchList.branches[i];

				if (b->type != trunk && b->type != shootLeft && b->type != shootRight)
					continue;
				if (b->totalLife <= 0)
					continue;

				int avg_x, avg_y;
				get_average_position(b, &avg_x, &avg_y);

				int log_factor = 0, dummy = b->age;
				while(dummy > 0) {
					log_factor++;
					dummy >>= 1;
				}

				double lifeRatio = ((double)b->age) / b->totalLife;
				int targetLeafLife = log_factor + lifeRatio * ((b->type == trunk) ? 4 : 3);

				if (!b->walkers) {
					b->walker_capacity = 16;
					b->walker_count = 1;
					b->walkers = malloc(sizeof(struct LeafWalker) * (size_t)b->walker_capacity);
					b->walkers[0] = (struct LeafWalker){.x = avg_x, .y = avg_y, .seed = b->leaf_seed};
					b->leaf_steps_drawn = 0;
					b->leaf_cur_x = avg_x;
					b->leaf_cur_y = avg_y;
					b->leafGrid = grid_create(40, 40, avg_x - 20, avg_y - 20);
				}

				if (avg_x != b->leaf_cur_x || avg_y != b->leaf_cur_y) {
					int delta_x = avg_x - b->leaf_cur_x;
					int delta_y = avg_y - b->leaf_cur_y;
					for (int w = 0; w < b->walker_count; w++) {
						b->walkers[w].x += delta_x;
						b->walkers[w].y += delta_y;
					}
					b->leafGrid->anchor_x += delta_x;
					b->leafGrid->anchor_y += delta_y;
					b->leaf_cur_x = avg_x;
					b->leaf_cur_y = avg_y;
				}

				if (b->leaf_steps_drawn < targetLeafLife) {
					enum branchType leafType = (b->type == trunk) ? dead : dying;
					leafStepWalkers(conf, b->leafGrid, leafType, trunk_y + 1,
									&b->walkers, &b->walker_count, &b->walker_capacity);
					b->leaf_steps_drawn++;
				}
			}
		}

		turn = (turn + 1) % branchList.count;

		if (conf->live && !(conf->load && myCounters->globalTime < conf->targetGlobalTime)) {
			if (liveStepDisplay(conf, objects, skeleton, renderPlane, &branchList, myCounters,
								trunk_x, trunk_y, baseHeight, &off_x, &off_y,
								maxX, maxY, turn)) {
				freeBranchList(&branchList);
				grid_destroy(skeleton);
				grid_destroy(trunkPlane);
				quit(conf, objects, 0);
			}
		}
	}

	if (!conf->no_disp) {
		blitTree(skeleton, renderPlane, trunk_y, &branchList, objects, off_x, off_y);
		update_panels();
		doupdate();
	}

	finalHold(conf, objects, skeleton, renderPlane, &branchList, trunk_x, trunk_y, baseHeight, &off_x, &off_y);

	freeBranchList(&branchList);

	grid_destroy(skeleton);
	grid_destroy(trunkPlane);
}


// ==========================================================================
// V2 ENGINE  (msaw streams: pads, lean, widening, deadwood)
// ==========================================================================

struct ColorResult chooseColorResult_v2(enum branchType type, struct msaw *cosmetic) {
	struct ColorResult cr = {0, 0};
	int r;
	switch(type) {
	case trunk:
		r = mrand(cosmetic, 4);
		if (r < 2) { cr.attrs = A_BOLD; cr.color_pair = 20; }
		else if (r == 2) { cr.color_pair = 20; }
		else { cr.color_pair = 21; }
		break;

	case shootLeft:
	case shootRight:
		r = mrand(cosmetic, 10);
		if (r < 2) { cr.attrs = A_BOLD; cr.color_pair = 20; }
		else if (r < 6) { cr.attrs = A_BOLD; cr.color_pair = 21; }
		else { cr.color_pair = 21; }
		break;

	case dying:
		r = mrand(cosmetic, 6);
		if (r < 3) { cr.color_pair = 22; }
		else if (r < 5) { cr.attrs = A_BOLD; cr.color_pair = 22; }
		else { cr.color_pair = 23; }
		break;

	case dead:
		r = mrand(cosmetic, 18);
		if (r < 2) { cr.attrs = A_BOLD; cr.color_pair = 23; }
		else if (r < 8) { cr.attrs = A_BOLD; cr.color_pair = 22; }
		else { cr.color_pair = 22; }
		break;
	}
	return cr;
}

// v2: nudge a freshly-rolled dx toward the branch's committed lean. Higher
// |lean| -> more consistent pull; dx is held within [lo, hi] so the lean bends
// the wander rather than overriding it. Draws one value from the growth stream.
static int applyLean(struct msaw *growth, int dx, int lean, int lo, int hi) {
	if (lean == 0) return dx;
	int s = (lean > 0) ? 1 : -1;
	int strength = (lean > 0) ? lean : -lean;
	if (strength > LEAN_MAX) strength = LEAN_MAX;
	if (mrand(growth, LEAN_DENOM) < strength) {
		dx += s;
		if (dx < lo) dx = lo;
		if (dx > hi) dx = hi;
	}
	return dx;
}

// v2: ported from setDeltas onto an explicit msaw growth stream, plus a lean
// pull on the trunk and shoot wander (see applyLean). dying/dead drift is left
// symmetric here — foliage shaping is handled by the canopy-pad walker bias.
void setDeltas_v2(enum branchType type, int life, int totalLife, int age,
				  int multiplier, int *returnDx, int *returnDy,
				  int lean, struct msaw *growth) {
	int dx = 0;
	int dy = 0;
	int dice;
	switch (type) {
	case trunk: // trunk

		// new or dead trunk
		if (age <= 2 || life < 4) {
			dy = 0;
			dx = mrand(growth, 3) - 1;
		}
		// young trunk should grow wide]
		else if (isYoungTrunk(age, totalLife)) {
			// every (multiplier * 0.4) steps, raise tree to next level
			int step = (int)(multiplier * 0.6);
			if (step < 1) step = 1;
			if (age % step == 0) dy = -1;
			else dy = 0;

			dice = mrand(growth, 10);
			if (dice >= 0 && dice <=0) dx = -2;			// 10%
			else if (dice >= 1 && dice <= 3) dx = -1;	// 30%
			else if (dice >= 4 && dice <= 5) dx = 0;	// 20%
			else if (dice >= 6 && dice <= 8) dx = 1;	// 30%
			else if (dice >= 9 && dice <= 9) dx = 2;	// 10%
		}
		else if (isEarlyTrunk(age, totalLife)) {
			// every (multiplier * 0.3) steps, raise tree to next level
			int step = (int)(multiplier * 0.3);
			if (step < 1) step = 1;
			if (age % step == 0) dy = -1;
			else dy = 0;

			dice = mrand(growth, 10);
			if (dice >= 0 && dice <=0) dx = -2;			// 10%
			else if (dice >= 1 && dice <= 3) dx = -1;	// 30%
			else if (dice >= 4 && dice <= 5) dx = 0;	// 20%
			else if (dice >= 6 && dice <= 8) dx = 1;	// 30%
			else if (dice >= 9 && dice <= 9) dx = 2;	// 10%
		}
		// old-aged trunk
		else {
			dice = mrand(growth, 10);
			if (dice > 3) dy = -1;
			else dy = 0;

			dice = mrand(growth, 20);
			if (dice >= 0 && dice <=0) dx = -2;			// 10%
			else if (dice >= 1 && dice <= 7) dx = -1;	// 30%
			else if (dice >= 8 && dice <= 12) dx = 0;	// 20%
			else if (dice >= 13 && dice <= 18) dx = 1;	// 30%
			else if (dice >= 19 && dice <= 19) dx = 2;	// 10%
		}
		break;

	case 1: // left shoot: trend left and little vertical movement
		dice = mrand(growth, 10);
		if (dice >= 0 && dice <= 2) dy = -1;
		else if (dice >= 3 && dice <= 7) dy = 0;
		else if (dice >= 8 && dice <= 9) dy = 1;

		dice = mrand(growth, 10);
		if (dice >= 0 && dice <=1) dx = -2;
		else if (dice >= 2 && dice <= 5) dx = -1;
		else if (dice >= 6 && dice <= 8) dx = 0;
		else if (dice >= 9 && dice <= 9) dx = 1;
		break;

	case 2: // right shoot: trend right and little vertical movement
		dice = mrand(growth, 10);
		if (dice >= 0 && dice <= 2) dy = -1;
		else if (dice >= 3 && dice <= 7) dy = 0;
		else if (dice >= 8 && dice <= 9) dy = 1;

		dice = mrand(growth, 10);
		if (dice >= 0 && dice <=1) dx = 2;
		else if (dice >= 2 && dice <= 5) dx = 1;
		else if (dice >= 6 && dice <= 8) dx = 0;
		else if (dice >= 9 && dice <= 9) dx = -1;
		break;

	case 3: // dying: discourage vertical growth(?); trend left/right (-3,3)
		dice = mrand(growth, 10);
		if (dice >= 0 && dice <=0) dy = -1;
		else if (dice >= 1 && dice <=8) dy = 0;
		else if (dice >= 9 && dice <=9) dy = 1;

		dice = mrand(growth, 15);
		if (dice >= 0 && dice <=0) dx = -3;
		else if (dice >= 1 && dice <= 2) dx = -2;
		else if (dice >= 3 && dice <= 5) dx = -1;
		else if (dice >= 6 && dice <= 8) dx = 0;
		else if (dice >= 9 && dice <= 11) dx = 1;
		else if (dice >= 12 && dice <= 13) dx = 2;
		else if (dice >= 14 && dice <= 14) dx = 3;
		break;

	case 4: // dead: fill in surrounding area
		dice = mrand(growth, 12);
		if (dice >= 0 && dice <= 1) dy = -1;
		else if (dice >= 2 && dice <= 8) dy = 0;
		else if (dice >= 9 && dice <= 11) dy = 1;

		dice = mrand(growth, 15);
		if (dice >= 0 && dice <=1) dx = -3;
		else if (dice >= 2 && dice <= 3) dx = -2;
		else if (dice >= 4 && dice <= 5) dx = -1;
		else if (dice >= 6 && dice <= 8) dx = 0;
		else if (dice >= 9 && dice <= 10) dx = 1;
		else if (dice >= 11 && dice <= 12) dx = 2;
		else if (dice >= 13 && dice <= 14) dx = 3;
		break;
	}

	// commit the woody wander toward the branch's lean (foliage is shaped by
	// the canopy-pad bias instead, so dying/dead are left alone)
	if (type == trunk || type == shootLeft || type == shootRight)
		dx = applyLean(growth, dx, lean, -2, 2);

	*returnDx = dx;
	*returnDy = dy;
}

// v2: faithful port of chooseString; leaf glyph choice draws from the
// cosmetic stream so it can be retuned without invalidating saved trees
char* chooseString_v2(const struct config *conf, enum branchType type, int life,
					  int dx, int dy, struct msaw *cosmetic) {
	char* branchStr;

	const unsigned int maxStrLen = 32;

	branchStr = malloc(maxStrLen);
	strcpy(branchStr, "?");	// fallback character

	if (life < 4) type = dying;

	switch(type) {
	case trunk:
		if (dy == 0) strcpy(branchStr, "/~");
		else if (dx < 0) strcpy(branchStr, "\\|");
		else if (dx == 0) strcpy(branchStr, "/|\\");
		else if (dx > 0) strcpy(branchStr, "|/");
		break;
	case shootLeft:
		if (dy > 0) strcpy(branchStr, "\\");
		else if (dy == 0) strcpy(branchStr, "\\_");
		else if (dx < 0) strcpy(branchStr, "\\|");
		else if (dx == 0) strcpy(branchStr, "/|");
		else if (dx > 0) strcpy(branchStr, "/");
		break;
	case shootRight:
		if (dy > 0) strcpy(branchStr, "/");
		else if (dy == 0) strcpy(branchStr, "_/");
		else if (dx < 0) strcpy(branchStr, "\\|");
		else if (dx == 0) strcpy(branchStr, "/|");
		else if (dx > 0) strcpy(branchStr, "/");
		break;
	case dying:
	case dead:
		strncpy(branchStr, conf->leaves[mrand(cosmetic, conf->leavesSize)], maxStrLen - 1);
		branchStr[maxStrLen - 1] = '\0';
	}

	return branchStr;
}

// v2: is a live structural branch head (trunk/shoot, excluding `exceptIdx`)
// within `minDist` Manhattan of (x, y)? Used to throttle splits and shoots in
// crowded regions. Pure spatial test (no RNG of its own), though gating a
// spawn does skip that spawn's draws, so changing the distances reshapes the
// tree. Deterministic for a given build + seed.
static int structuralCrowded(const struct BranchList *list, int x, int y,
							 int exceptIdx, int minDist) {
	for (int i = 0; i < list->count; i++) {
		if (i == exceptIdx) continue;
		const struct Branch *b = &list->branches[i];
		if (b->type != trunk && b->type != shootLeft && b->type != shootRight)
			continue;
		if (abs(b->x - x) + abs(b->y - y) < minDist)
			return 1;
	}
	return 0;
}

// v2: ported from updateBranch_v1 onto explicit msaw streams.
// Growth decisions draw from `growth`, colors/glyphs from `cosmetic`,
// and each spawned branch gets its own leaf stream via msaw_split.
// Diverges from v1 in the terminal phase: dead branches no longer re-trigger
// the near-dead spawn rule (v1 cascaded exponentially there), dying branches
// and dying shoots emit children at half rate, and shoots get a life floor so
// ones spawned near the top act like branches instead of leaf fountains.
// Splits and shoots are additionally suppressed where heads are crowded
// (structuralCrowded), which keeps high multiplier from tangling.
void updateBranch_v2(struct config *conf, struct VirtualGrid *skeleton,
				struct counters *myCounters, int branchIdx,
				struct BranchList* list,
				struct msaw *growth, struct msaw *cosmetic, struct msaw *deadRng) {

	struct Branch *branch = &list->branches[branchIdx];
	// non-trunk branches age one life per tick; a trunk instead pays life per
	// row it climbs (charged after setDeltas) so its height tracks L, not M
	if (branch->type != trunk)
		branch->life--;

	// Random die-off check - more likely on shoots
	if (branch->type == trunk) {
		if (mrand(growth, 66) == 0) {   // 2% chance for trunk
			branch->life -= (branch->life/2);  // Lose 1/4 of life
		}
	} else if (branch->type == shootLeft || branch->type == shootRight) {
		if (mrand(growth, 20) == 0) {    // 5% chance for shoots
			branch->life /= 2;      // Lose half of life
		}
	}

	branch->age++;

	// jin/shari: a fork destined for deadwood grows alive and leafy, then dies
	// back — once its life falls to diebackLife it becomes bare bleached wood
	// for the rest of its (now short) life. Life-based so it triggers reliably
	// as the trunk spends its height budget.
	if (branch->diebackLife > 0 && !branch->deadwood && branch->life <= branch->diebackLife)
		branch->deadwood = 1;

	setDeltas_v2(branch->type, branch->life, branch->totalLife,
			  branch->age, branch->multiplier, &branch->dx, &branch->dy,
			  branch->lean, growth);

	int groundY = skeleton->anchor_y + skeleton->height - getBaseHeight(conf->baseType);
	if (branch->dy > 0 && branch->y > (groundY - 6))
		branch->dy--;

	// trunk life is a height budget: pay per climbed row so total height is
	// ~ L / TRUNK_RISE_COST whether it rises often (low M) or rarely (high M).
	// A stalled low-life tip still drains 1/tick so it can't hang forever.
	if (branch->type == trunk) {
		if (branch->dy < 0) branch->life -= TRUNK_RISE_COST;
		else if (branch->life < 4) branch->life--;
	}

	// near-dead branch should branch into a lot of leaves; dead branches
	// don't re-trigger this (would cascade exponentially), dying ones only
	// emit at half rate so clusters don't snowball, and deadwood stays bare
	if (branch->life < 6 && branch->type != dead && !branch->deadwood) {
		if (branch->type != dying || mrand(growth, 2) == 0) {
			struct Branch newBranch = {
				.x = branch->x,
				.y = branch->y,
				.type = dead,
				.life = branch->life,
				.age = 0,
				.totalLife = branch->life,
				.multiplier = branch->multiplier,
				.shootCooldown = conf->multiplier,
				.dripLeafCooldown = branch->life / 4,
				.history_count = 0,
				.history_index = 0,
				.x_history[0] = branch->x,
				.y_history[0] = branch->y
			};
			msaw_split(growth, &newBranch.leaf_rng);
			addBranch(list, newBranch, myCounters);
			branch = &list->branches[branchIdx];
		}
	}
	else if (branch->type == shootLeft || branch->type == shootRight) {
		// dying shoot emits at half rate (v1 spawned every tick)
		if (branch->life < 7 + (branch->multiplier /5)) {
			if (mrand(growth, 2) == 0) {
				struct Branch newBranch = {
					.x = branch->x,
					.y = branch->y,
					.type = dying,
					.life = branch->life + 1,
					.age = 0,
					.totalLife = branch->life + 1,
					.multiplier = branch->multiplier,
					.shootCooldown = conf->multiplier,
					.dripLeafCooldown = (branch->life + 1) / 4,
					.history_count = 0,
					.history_index = 0,
					.x_history[0] = branch->x,
					.y_history[0] = branch->y
				};
				msaw_split(growth, &newBranch.leaf_rng);
				addBranch(list, newBranch, myCounters);
				branch = &list->branches[branchIdx];
			}
		}
		else if (branch->dripLeafCooldown <= 0 && mrand(growth, 3) == 0) {
			struct Branch newBranch = {
				.x = branch->x,
				.y = branch->y,
				.type = dying,
				.life = 5,
				.age = 0,
				.totalLife = 5,
				.multiplier = branch->multiplier,
				.shootCooldown = conf->multiplier,
				.dripLeafCooldown = (branch->multiplier*2)/3,
				.history_count = 0,
				.history_index = 0,
				.x_history[0] = branch->x,
				.y_history[0] = branch->y
			};
			msaw_split(growth, &newBranch.leaf_rng);
			addBranch(list, newBranch, myCounters);
			branch = &list->branches[branchIdx];
			// higher multiplier -> shorter gap between drip leaves (v1 had a
			// sign slip here, 25 + M, which made the drip fire ~once)
			branch->dripLeafCooldown = 7 + (25 - branch->multiplier);
		}
	}
	// dying trunk should branch into a lot of leaves (deadwood dies bare)
	else if (branch->type == trunk && branch->life < (branch->multiplier + 2) && !branch->deadwood) {
		struct Branch newBranch = {
			.x = branch->x,
			.y = branch->y,
			.type = dying,
			.life = branch->life,
			.age = 0,
			.totalLife = branch->life,
			.multiplier = branch->multiplier,
			.shootCooldown = conf->multiplier,
			.dripLeafCooldown = branch->life / 4,
			.history_count = 0,
			.history_index = 0,
			.x_history[0] = branch->x,
			.y_history[0] = branch->y
		};
		msaw_split(growth, &newBranch.leaf_rng);
		addBranch(list, newBranch, myCounters);
		branch = &list->branches[branchIdx];
	}
	else if (branch->type == trunk) {
		// (trunk life is now charged per climbed row above, not per tick)
		// First check for trunk splits - only in early phase and with enough life
		if (!isYoungTrunk(branch->age, branch->totalLife)) {
			int splitThreshold = (24 - branch->multiplier) + (2 * myCounters->trunks);
			double ageRatio = (double)branch->age / branch->totalLife;

			if (ageRatio < 0.1) {      // Bottom 10%
				splitThreshold = (splitThreshold * 2)/7;
			} else if (ageRatio < 0.4) {
				splitThreshold = (splitThreshold * 3)/7;
			} else {
				splitThreshold = (splitThreshold * 5)/7;
			}

			// never a guaranteed split: at high multiplier the threshold can
			// otherwise reach 1 (mrand(.,1)==0 always), turning every cooldown
			// into a split and exploding the trunk count
			if (splitThreshold < 2) splitThreshold = 2;

			if (myCounters->trunkSplitCooldown < 0 && !branch->deadwood && mrand(growth, splitThreshold) == 0
				&& !structuralCrowded(list, branch->x, branch->y, branchIdx, SPLIT_MIN_DIST)) {
				myCounters->trunkSplitCooldown = 2 + ((22 - conf->multiplier)*3)/4 +
					(int)(5 * ((double)branch->totalLife - branch->age)/branch->totalLife);
				myCounters->trunks++;
				branch->shootGrace = SPLIT_SHOOT_GRACE;  // parent gets a clean stretch after the split
				branch->shootCooldown = (25 - branch->multiplier)/4;
				// sequence the two draws explicitly (v1 left this order
				// unspecified inside the initializer list)
				int splitLife = branch->life - mrand(growth, 6);
				int splitTotalLife = branch->life - mrand(growth, 6);
				// fork divergence: child and parent commit to opposite sides so
				// a split reads as a real fork. Divergence grows with the
				// multiplier, so high M makes bold forks instead of a tangle.
				int divStrength = 1 + branch->multiplier / 7;   // M8->2, M20->3
				if (divStrength > LEAN_MAX) divStrength = LEAN_MAX;
				int side = (mrand(growth, 2) == 0) ? 1 : -1;
				int childLean = branch->lean + side * divStrength;
				int parentLean = branch->lean - side * divStrength;
				if (childLean > LEAN_MAX) childLean = LEAN_MAX;
				else if (childLean < -LEAN_MAX) childLean = -LEAN_MAX;
				if (parentLean > LEAN_MAX) parentLean = LEAN_MAX;
				else if (parentLean < -LEAN_MAX) parentLean = -LEAN_MAX;
				// jin/shari: occasionally this fork is destined to die back — it
				// grows alive and leafy, then turns to bare wood once its life
				// drops to ~a third remaining. Decided on its own stream so
				// trees with no dead limb are byte-identical to before.
				int childDieback = 0;
				if (mrand(deadRng, DEADWOOD_CHANCE) == 0)
					childDieback = splitTotalLife / 3 + mrand(deadRng, splitTotalLife / 6 + 1);
				struct Branch newBranch = {
					.x = branch->x,
					.y = branch->y,
					.type = trunk,
					.life = splitLife,
					.age = 0,
					.totalLife = splitTotalLife,
					.multiplier = branch->multiplier,
					.lean = childLean,
					.splitDepth = branch->splitDepth + 1,
					.shootGrace = SPLIT_SHOOT_GRACE,   // child also starts with a clean stretch
					.diebackLife = childDieback,
					.shootCooldown = conf->multiplier,
					.dripLeafCooldown = branch->life / 4,
					.history_count = 0,
					.history_index = 0,
					.x_history[0] = branch->x,
					.y_history[0] = branch->y
				};
				msaw_split(growth, &newBranch.leaf_rng);
				addBranch(list, newBranch, myCounters);
				branch = &list->branches[branchIdx];
				branch->lean = parentLean;
				// cost of splitting — smaller at higher multiplier, since high M
				// splits far more often (keeps total split drain ~M-independent)
				int splitCoef = (32 - conf->multiplier) / 5;   // M7->5, M14->3, M20->2
				if (splitCoef < 1) splitCoef = 1;
				branch->life -= mrand(growth, 1) + (int)(splitCoef * ((double)branch->totalLife - branch->age)/branch->totalLife);
			}
		}

		// Then check for regular branch shoots (deadwood limbs stay bare)
		int branchDice = getBranchRollThreshold(branch->age, branch->totalLife, branch->multiplier);
		if (branch->shootCooldown <= 0 && !branch->deadwood && branch->shootGrace <= 0
			&& mrand(growth, branchDice) == 0
			&& !structuralCrowded(list, branch->x, branch->y, branchIdx, SHOOT_MIN_DIST)) {
			branch->shootCooldown = myCounters->trunks + (25 - branch->multiplier)/6;
			int shootLife = ((branch->life * 3)/4 + mrand(growth, branch->multiplier) - 2);
			// ceiling: shoot life (and thus sideways reach) tracks the trunk's
			// height budget, so a shoot off a fresh high-life base can't run
			// clear across the screen. Applied before the floor so the floor
			// always wins as the lower bound on tiny trees.
			int shootCap = branch->totalLife / SHOOT_LIFE_CAP_DIV + branch->multiplier/3;
			if (shootLife > shootCap)
				shootLife = shootCap;
			// floor keeps shoots near the top of the trunk acting like
			// branches for a few steps before their dying phase, instead
			// of being born straight into it
			if (shootLife < 8 + branch->multiplier/3)
				shootLife = 8 + branch->multiplier/3;

			myCounters->shoots++;
			myCounters->shootCounter++;

			// side-runs: commit to one flank for a short run, then flip, so
			// shoots group into alternating clusters (feeds the canopy pads)
			if (myCounters->shootRunRemaining <= 0) {
				myCounters->shootSide = (myCounters->shootSide == shootLeft) ? shootRight : shootLeft;
				myCounters->shootRunRemaining = 1 + mrand(growth, 1 + branch->multiplier / SHOOT_RUN_MDIV);
			}
			enum branchType shootType = (enum branchType)myCounters->shootSide;
			myCounters->shootRunRemaining--;

			struct Branch newBranch = {
				.x = branch->x,
				.y = branch->y,
				.type = shootType,
				.life = shootLife,
				.age = 0,
				.totalLife = shootLife,
				.multiplier = branch->multiplier,
				.lean = branch->lean,   // shoots sweep with the trunk they grow from
				.shootCooldown = conf->multiplier,
				.dripLeafCooldown = shootLife / 4,
				.history_count = 0,
				.history_index = 0,
				.x_history[0] = branch->x,
				.y_history[0] = branch->y
			};
			msaw_split(growth, &newBranch.leaf_rng);
			addBranch(list, newBranch, myCounters);
			branch = &list->branches[branchIdx];

			branch->life -= mrand(growth, 3); // cost of sprouting
		}
	}
	myCounters->trunkSplitCooldown--;
	branch->shootGrace--;
	branch->shootCooldown--;
	branch->dripLeafCooldown--;

	// move in x and y directions
	branch->x += branch->dx;
	branch->y += branch->dy;
	if(conf->proceduralMode && branch->type != dying && branch->type != dead)
		update_position_history(branch);

	enum branchType displayType = (branch->life < 4) ? dying : branch->type;
	struct ColorResult cr = chooseColorResult_v2(displayType, cosmetic);

	// choose string to use for this branch
	char *branchStr = chooseString_v2(conf, displayType, branch->life, branch->dx, branch->dy, cosmetic);

	// deadwood (jin/shari): bare bleached wood. Force a trunk glyph (otherwise
	// chooseString_v2 emits leaf glyphs once life<4) and a mostly-bleached
	// colour, occasionally dark, for a weathered look.
	if (branch->deadwood) {
		const char *tg = (branch->dy == 0) ? "/~"
					   : (branch->dx < 0)  ? "\\|"
					   : (branch->dx == 0) ? "/|\\"
					   :                     "|/";
		strcpy(branchStr, tg);
		cr.attrs = 0;   // no bold: keeps the dead wood muted rather than bright
		cr.color_pair = (mrand(cosmetic, 4) == 0) ? 21 : 24;
	}

	// grab wide character from branchStr
	wchar_t wc = 0;
	mbstate_t ps = {0};
	mbrtowc(&wc, branchStr, 32, &ps);

	// write to grid, but ensure wide characters don't overlap.
	// --bare suppresses leaf glyphs only (dying/dead); the RNG was already
	// drawn above, so the woody structure stays byte-identical with/without it.
	int w = wcwidth(wc);
	if (w <= 0) w = 1;
	int isLeafGlyph = (displayType == dying || displayType == dead);
	if(branch->x % w == 0 && !(conf->hideLeaves && isLeafGlyph)) {
		grid_put(skeleton, branch->x, branch->y, branchStr, cr.attrs, cr.color_pair);
	}

	free(branchStr);
}

// v2: faithful port of leafStepWalkers; each walker advances its own msaw
// stream (replacing rand_r) and children fork via msaw_split
static void leafStep_v2(struct config *conf, struct VirtualGrid *grid,
						enum branchType type, int groundY,
						struct LeafWalker **walkers, int *count, int *capacity) {
	int prev_count = *count;
	for (int w = 0; w < prev_count; w++) {
		struct LeafWalker *wk = &(*walkers)[w];

		int dx = 0, dy = 0, dice;
		switch (type) {
		case dying:
			dice = mrand(&wk->rng, 10);
			if (dice >= 0 && dice <= 0) dy = -1;
			else if (dice >= 1 && dice <= 8) dy = 0;
			else if (dice >= 9 && dice <= 9) dy = 1;

			dice = mrand(&wk->rng, 15);
			if (dice >= 0 && dice <= 0) dx = -3;
			else if (dice >= 1 && dice <= 2) dx = -2;
			else if (dice >= 3 && dice <= 5) dx = -1;
			else if (dice >= 6 && dice <= 8) dx = 0;
			else if (dice >= 9 && dice <= 11) dx = 1;
			else if (dice >= 12 && dice <= 13) dx = 2;
			else if (dice >= 14 && dice <= 14) dx = 3;
			break;
		case dead:
			dice = mrand(&wk->rng, 12);
			if (dice >= 0 && dice <= 1) dy = -1;
			else if (dice >= 2 && dice <= 8) dy = 0;
			else if (dice >= 9 && dice <= 11) dy = 1;

			dice = mrand(&wk->rng, 15);
			if (dice >= 0 && dice <= 1) dx = -3;
			else if (dice >= 2 && dice <= 3) dx = -2;
			else if (dice >= 4 && dice <= 5) dx = -1;
			else if (dice >= 6 && dice <= 8) dx = 0;
			else if (dice >= 9 && dice <= 10) dx = 1;
			else if (dice >= 11 && dice <= 12) dx = 2;
			else if (dice >= 13 && dice <= 14) dx = 3;
			break;
		default:
			break;
		}

		// canopy pads: lean the walk outward from the trunk. This remaps dx
		// without drawing extra RNG, so the walker stream is unchanged.
		if (wk->outward > 0) {
			if (dx == 0) dx = LEAF_PAD_BIAS;
			else if (dx < -LEAF_PAD_INWARD) dx = -LEAF_PAD_INWARD;
		} else if (wk->outward < 0) {
			if (dx == 0) dx = -LEAF_PAD_BIAS;
			else if (dx > LEAF_PAD_INWARD) dx = LEAF_PAD_INWARD;
		}

		if (dy > 0 && wk->y > (groundY - 2))
			dy--;

		struct msaw child_rng;
		msaw_split(&wk->rng, &child_rng);
		if (*count < 4096) {
			if (*count >= *capacity) {
				*capacity *= 2;
				*walkers = realloc(*walkers, sizeof(struct LeafWalker) * (size_t)*capacity);
				wk = &(*walkers)[w];
			}
			struct LeafWalker child = {.x = wk->x, .y = wk->y, .seed = 0,
									   .outward = wk->outward};
			child.rng = child_rng;
			(*walkers)[(*count)++] = child;
		}

		wk->x += dx;
		wk->y += dy;

		if (wk->y >= 0 && wk->y < groundY) {
			attr_t la = 0;
			short lc = 0;
			switch (type) {
			case dying:
				if (mrand(&wk->rng, 6) == 0) { lc = 22; }
				else if (mrand(&wk->rng, 2) == 0) { la = A_BOLD; lc = 23; }
				else { lc = 23; }
				break;
			case dead:
				if (mrand(&wk->rng, 7) == 0) { la = A_BOLD; lc = 22; }
				else if (mrand(&wk->rng, 2) == 0) { la = A_BOLD; lc = 23; }
				else { lc = 23; }
				break;
			default:
				break;
			}

			// draw the leaf glyph (still consume the RNG when --bare so the
			// hidden-foliage tree is identical to the shown one)
			char *leafStr = conf->leaves[mrand(&wk->rng, conf->leavesSize)];
			if (!conf->hideLeaves)
				grid_put(grid, wk->x, wk->y, leafStr, la, lc);
		}
	}
}

void generateLeaves_v2(struct config *conf, struct VirtualGrid *grid, enum branchType type,
					   int x, int y, int life, const struct msaw *leafRng, int groundY,
					   int outward) {
	int capacity = 16;
	int count = 1;
	struct LeafWalker *walkers = malloc(sizeof(struct LeafWalker) * (size_t)capacity);
	walkers[0] = (struct LeafWalker){.x = x, .y = y, .seed = 0, .outward = outward};
	walkers[0].rng = *leafRng;

	for (int step = 0; step < life; step++) {
		leafStep_v2(conf, grid, type, groundY, &walkers, &count, &capacity);
	}

	free(walkers);
}

// v2: advance the trunk-widening animation by one tick. Each trunk cell grows
// its rendered half-width toward a target — distance-from-base taper scaled by
// the tree's maturity (gated by TRUNK_MIN_HEIGHT) — one step at a time, with a
// random [0,7]-tick gap between steps, so the lower trunk fills out gradually
// rather than all at once. Pure presentation: draws from `rng` only.
static void advanceTrunkWiden(struct VirtualGrid *tp, int trunk_y, struct msaw *rng) {
	int apexY = trunk_y;
	for (int gy = 0; gy < tp->height; gy++) {
		int found = 0;
		for (int gx = 0; gx < tp->width; gx++)
			if (tp->cells[gy * tp->width + gx].occupied) { found = 1; break; }
		if (found) { apexY = tp->anchor_y + gy; break; }
	}
	int height = trunk_y - apexY;
	int baseHalf = 0;
	if (height >= TRUNK_MIN_HEIGHT) {
		baseHalf = 1 + (height - TRUNK_MIN_HEIGHT) / TRUNK_GROW_DIV;
		if (baseHalf > TRUNK_MAX_HALF) baseHalf = TRUNK_MAX_HALF;
	}

	for (int gy = 0; gy < tp->height; gy++) {
		for (int gx = 0; gx < tp->width; gx++) {
			struct GridCell *c = &tp->cells[gy * tp->width + gx];
			if (!c->occupied) continue;
			int cy = tp->anchor_y + gy;
			// fork-thinning: each split deep scales the base width by 4/5
			int sh = baseHalf;
			for (int d = 0; d < c->splitDepth; d++) sh = (sh * 4) / 5;
			int target = sh - (trunk_y - cy) / TRUNK_TAPER_DIV;
			if (target < 0) target = 0;
			if (c->widenHalf < target) {
				if (c->widenTimer > 0) c->widenTimer--;
				else { c->widenHalf++; c->widenTimer = mrand(rng, 8); }  // 0..7
			}
		}
	}
}

// v2 trunk widening: render the body under the skeleton using each cell's
// animated half-width (advanceTrunkWiden owns the timing). "Widen from the
// inside" — the stored centerline glyph's outer chars become the sloped edges
// (lean baked in), its middle becomes the fill. The left/right reach are
// computed independently so the trunk can bulge to the outside of a lean and
// stop short of a neighbouring arm (no welding into a slab).
static void drawWidenedTrunk(struct VirtualGrid *tp, WINDOW *win,
							 int trunk_y, int off_x, int off_y) {
	(void)trunk_y;  // taper baked into each cell's widenHalf
	int wh, ww;
	getmaxyx(win, wh, ww);

	for (int gy = 0; gy < tp->height; gy++) {
		struct GridCell *row = &tp->cells[gy * tp->width];
		for (int gx = 0; gx < tp->width; gx++) {
			struct GridCell *cell = &row[gx];
			if (!cell->occupied) continue;
			int cx = tp->anchor_x + gx;
			int cy = tp->anchor_y + gy;

			int half = cell->widenHalf;

			// look at the connected centerline in the rows above/below (nearest
			// occupied cell within reach) so the body can follow the trunk's bends
			int aboveOff = 99, belowOff = 99, aboveHalf = 0;
			if (gy > 0) {
				struct GridCell *up = &tp->cells[(gy - 1) * tp->width];
				for (int o = -2; o <= 2; o++) {
					int nx = gx + o;
					if (nx >= 0 && nx < tp->width && up[nx].occupied && abs(o) < abs(aboveOff)) {
						aboveOff = o; aboveHalf = up[nx].widenHalf;
					}
				}
			}
			if (gy + 1 < tp->height) {
				struct GridCell *dn = &tp->cells[(gy + 1) * tp->width];
				for (int o = -2; o <= 2; o++) {
					int nx = gx + o;
					if (nx >= 0 && nx < tp->width && dn[nx].occupied && abs(o) < abs(belowOff))
						belowOff = o;
				}
			}

			// bottom-most layer (nothing below): grow out to match the trunk
			// just above it instead of being width-limited, so the base doesn't
			// pinch in. It stays symmetric (the bias below is skipped for it),
			// so it ends up ~1 wider than the biased row above — the natural
			// off-by-one flare.
			int isBase = (belowOff == 99);
			if (isBase && aboveHalf > half) half = aboveHalf;

			if (half < 1) continue;   // not yet widened: centerline only

			const char *g = cell->ch;
			int len = (int)strlen(g);
			char ledge[2] = { g[0], 0 };
			char redge[2] = { len > 0 ? g[len - 1] : '|', 0 };
			char fill[2]  = { (len == 3) ? g[1] : '|', 0 };

			int lh = half, rh = half;

			// bias toward the side(s) where the trunk continues above/below,
			// so the body follows the centerline's bends rather than growing
			// straight out (thin the side with no trunk neighbour). Skipped for
			// the base, which stays symmetric and full.
			if (!isBase) {
				int wantRight = (aboveOff != 99 && aboveOff > 0) || (belowOff != 99 && belowOff > 0);
				int wantLeft  = (aboveOff != 99 && aboveOff < 0) || (belowOff != 99 && belowOff < 0);
				if (wantRight && !wantLeft && lh > 0) lh--;
				else if (wantLeft && !wantRight && rh > 0) rh--;
			}

			// anti-weld: a flank only fills empty cells and stops short of a
			// neighbouring arm's centerline, splitting the gap so two arms
			// never merge into a slab
			int e = 0;
			while (e < rh && (gx + 1 + e >= tp->width || !row[gx + 1 + e].occupied)) e++;
			if (gx + 1 + e < tp->width && row[gx + 1 + e].occupied) { rh = (e - 1) / 2; if (rh < 0) rh = 0; }
			e = 0;
			while (e < lh && (gx - 1 - e < 0 || !row[gx - 1 - e].occupied)) e++;
			if (gx - 1 - e >= 0 && row[gx - 1 - e].occupied) { lh = (e - 1) / 2; if (lh < 0) lh = 0; }

			if (lh < 1 && rh < 1) continue;

			for (int d = -lh; d <= rh; d++) {
				int wx = cx + d + off_x;
				int wy = cy + off_y;
				if (wx < 0 || wx >= ww || wy < 0 || wy >= wh) continue;
				if (d == 0) continue;                 // centerline drawn by skeleton
				const char *ch; short pair; attr_t at = 0;
				if (d == -lh)      { ch = ledge; pair = 21; at = A_BOLD; }
				else if (d == rh)  { ch = redge; pair = 21; at = A_BOLD; }
				else               { ch = fill;  pair = 20; }
				wattron(win, at | COLOR_PAIR(pair));
				mvwaddstr(win, wy, wx, ch);
				wattroff(win, at | COLOR_PAIR(pair));
			}
		}
	}
}

// v2 engine: structurally a faithful port of v1, but fully self-seeded
// from explicit msaw streams — it never touches the global rand() stream,
// so growth, cosmetics and leaf walkers are independently deterministic.
void growTree_v2(struct config *conf, struct ncursesObjects *objects, struct counters *myCounters) {
	int maxY, maxX;
	getmaxyx(objects->treeWin, maxY, maxX);

	struct msaw growth, cosmetic, widenRng, deadRng;
	msaw_seed(&growth, (uint64_t)conf->seed);
	msaw_seed(&cosmetic, (uint64_t)conf->seed ^ MSAW_COSMETIC_SALT);
	msaw_seed(&widenRng, (uint64_t)conf->seed ^ MSAW_WIDEN_SALT);
	msaw_seed(&deadRng, (uint64_t)conf->seed ^ MSAW_DEADWOOD_SALT);

	int baseHeight = getBaseHeight(conf->baseType);
	struct VirtualGrid *skeleton = grid_create(maxX, maxY + baseHeight, 0, 0);
	struct VirtualGrid *trunkPlane = grid_create(maxX, maxY + baseHeight, 0, 0);
	struct VirtualGrid *renderPlane = trunkPlane;  // v2: render the widened trunk under the skeleton
	int trunk_x = maxX / 2;
	int trunk_y = maxY - 1 - baseHeight;
	int off_x = 0, off_y = 0;
	int rimLo = trunk_x, rimHi = trunk_x;

	drawBaseToGrid(skeleton, conf->baseType, trunk_x, trunk_y);
	drawPotRim(skeleton, conf->baseType, trunk_x, trunk_y, rimLo, rimHi);

	struct BranchList branchList;
	initBranchList(&branchList);

	myCounters->trunks = 0;
	myCounters->shoots = 0;
	myCounters->branches = 0;
	myCounters->shootCounter = 5;
	myCounters->globalTime = 0;
	myCounters->trunkSplitCooldown = 0;
	// random starting flank; runs flip from here (see shoot side-runs below)
	myCounters->shootSide = (mrand(&growth, 2) == 0) ? shootLeft : shootRight;
	myCounters->shootRunRemaining = 0;

	// gentle random whole-tree lean (windswept variety); the dramatic shaping
	// comes from fork divergence, so the base tilt stays mild
	int baseLean = mrand(&growth, 3) - 1;   // -1, 0, +1
	struct Branch initialBranch = {
		.x = trunk_x,
		.y = trunk_y,
		.life = conf->lifeStart,
		.age = 0,
		.type = trunk,
		.shootCooldown = conf->multiplier,
		.dripLeafCooldown = conf->multiplier + conf->lifeStart / 4,
		.totalLife = conf->lifeStart,
		.multiplier = conf->multiplier,
		.lean = baseLean
	};
	msaw_split(&growth, &initialBranch.leaf_rng);
	addBranch(&branchList, initialBranch, myCounters);

	int turn = 0;
	while (branchList.count > 0) {
		myCounters->globalTime++;

		if (branchList.branches[turn].life <= 0) {
			struct Branch* b = &branchList.branches[turn];
			if (conf->proceduralMode &&
				b->type != dying && b->type != dead &&
				!b->deadwood &&                  // deadwood dies bare, no leaf burst
				b->totalLife > 0) {
				double lifeRatio = ((double)b->age) / b->totalLife;

				int avg_x, avg_y;
				get_average_position(b, &avg_x, &avg_y);

				int log_factor = 0, dummy = b->age;
				while(dummy > 0) {
					log_factor++;
					dummy >>= 1;
				}

				int leafLife = log_factor + lifeRatio * ((b->type == trunk) ? 4 : 3);
				enum branchType newType = (b->type == trunk) ? dead : dying;

				// canopy pads: clusters lean away from the trunk centerline
				int leafOutward = (avg_x < trunk_x) ? -1 : (avg_x > trunk_x) ? 1 : 0;
				generateLeaves_v2(conf, skeleton, newType, avg_x, avg_y, leafLife, &b->leaf_rng, trunk_y + 1, leafOutward);
			}

			free(b->walkers);
			b->walkers = NULL;
			b->walker_count = 0;
			b->walker_capacity = 0;
			grid_destroy(b->leafGrid);
			b->leafGrid = NULL;

			removeBranch(&branchList, turn);
			if (turn >= branchList.count) {
				turn = 0;
			}
			continue;
		}

		updateBranch_v2(conf, skeleton, myCounters, turn, &branchList, &growth, &cosmetic, &deadRng);

		// record trunk cells in the trunk plane, storing the centerline glyph
		// (its outer chars become the widened edges) and keeping the pot rim
		// hugging the trunk's footprint
		{
			struct Branch *ub = &branchList.branches[turn];
			if (ub->type == trunk && !ub->deadwood) {  // deadwood stays a thin bare spar
				// same centerline glyph chooseString_v2 draws for a trunk;
				// drawWidenedTrunk relocates its edge chars outward
				const char *tg = (ub->dy == 0) ? "/~"
							   : (ub->dx < 0)  ? "\\|"
							   : (ub->dx == 0) ? "/|\\"
							   :                 "|/";
				grid_put(trunkPlane, ub->x, ub->y, tg, 0, 0);
				struct GridCell *tc = grid_at(trunkPlane, ub->x, ub->y);
				if (tc) {
					tc->splitDepth = ub->splitDepth;   // deeper forks widen less
					// random initial delay so the lower trunk widens
					// cell-by-cell (staggered) rather than in lockstep
					if (tc->widenHalf == 0)
						tc->widenTimer = mrand(&widenRng, 8);
				}
			}
		}

		// advance the trunk-widening animation one tick
		advanceTrunkWiden(trunkPlane, trunk_y, &widenRng);

		// keep the pot rim hugging the widened trunk base (which thickens over
		// time), not just the thin centerline
		{
			int ly = trunk_y - trunkPlane->anchor_y;
			if (ly >= 0 && ly < trunkPlane->height) {
				int lo = trunk_x, hi = trunk_x;
				for (int gx = 0; gx < trunkPlane->width; gx++) {
					struct GridCell *c = &trunkPlane->cells[ly * trunkPlane->width + gx];
					if (!c->occupied) continue;
					int cx = trunkPlane->anchor_x + gx;
					if (cx - c->widenHalf < lo) lo = cx - c->widenHalf;
					if (cx + c->widenHalf > hi) hi = cx + c->widenHalf;
				}
				if (lo != rimLo || hi != rimHi) {
					rimLo = lo; rimHi = hi;
					drawPotRim(skeleton, conf->baseType, trunk_x, trunk_y, rimLo, rimHi);
				}
			}
		}

		if (conf->live && conf->proceduralMode) {
			for (int i = 0; i < branchList.count; i++) {
				struct Branch* b = &branchList.branches[i];

				if (b->type != trunk && b->type != shootLeft && b->type != shootRight)
					continue;
				if (b->deadwood)   // bare limb: no live foliage
					continue;
				if (b->totalLife <= 0)
					continue;

				int avg_x, avg_y;
				get_average_position(b, &avg_x, &avg_y);

				int log_factor = 0, dummy = b->age;
				while(dummy > 0) {
					log_factor++;
					dummy >>= 1;
				}

				double lifeRatio = ((double)b->age) / b->totalLife;
				int targetLeafLife = log_factor + lifeRatio * ((b->type == trunk) ? 4 : 3);

				if (!b->walkers) {
					b->walker_capacity = 16;
					b->walker_count = 1;
					b->walkers = malloc(sizeof(struct LeafWalker) * (size_t)b->walker_capacity);
					int leafOutward = (avg_x < trunk_x) ? -1 : (avg_x > trunk_x) ? 1 : 0;
					b->walkers[0] = (struct LeafWalker){.x = avg_x, .y = avg_y, .seed = 0,
														.outward = leafOutward};
					b->walkers[0].rng = b->leaf_rng;
					b->leaf_steps_drawn = 0;
					b->leaf_cur_x = avg_x;
					b->leaf_cur_y = avg_y;
					b->leafGrid = grid_create(40, 40, avg_x - 20, avg_y - 20);
				}

				if (avg_x != b->leaf_cur_x || avg_y != b->leaf_cur_y) {
					int delta_x = avg_x - b->leaf_cur_x;
					int delta_y = avg_y - b->leaf_cur_y;
					for (int w = 0; w < b->walker_count; w++) {
						b->walkers[w].x += delta_x;
						b->walkers[w].y += delta_y;
					}
					b->leafGrid->anchor_x += delta_x;
					b->leafGrid->anchor_y += delta_y;
					b->leaf_cur_x = avg_x;
					b->leaf_cur_y = avg_y;
				}

				if (b->leaf_steps_drawn < targetLeafLife) {
					enum branchType leafType = (b->type == trunk) ? dead : dying;
					leafStep_v2(conf, b->leafGrid, leafType, trunk_y + 1,
								&b->walkers, &b->walker_count, &b->walker_capacity);
					b->leaf_steps_drawn++;
				}
			}
		}

		turn = (turn + 1) % branchList.count;

		if (conf->live && !(conf->load && myCounters->globalTime < conf->targetGlobalTime)) {
			if (liveStepDisplay(conf, objects, skeleton, renderPlane, &branchList, myCounters,
								trunk_x, trunk_y, baseHeight, &off_x, &off_y,
								maxX, maxY, turn)) {
				freeBranchList(&branchList);
				grid_destroy(skeleton);
				grid_destroy(trunkPlane);
				quit(conf, objects, 0);
			}
		}
	}

	if (!conf->no_disp) {
		blitTree(skeleton, renderPlane, trunk_y, &branchList, objects, off_x, off_y);
		update_panels();
		doupdate();
	}

	finalHold(conf, objects, skeleton, renderPlane, &branchList, trunk_x, trunk_y, baseHeight, &off_x, &off_y);

	freeBranchList(&branchList);

	grid_destroy(skeleton);
	grid_destroy(trunkPlane);
}


// ==========================================================================
// DISPATCH + ENTRY POINT
// ==========================================================================

// long-only option codes (no short form)
#define OPT_ENGINE 1000

#define OPT_BARE 1001

struct TreeEngine get_engine(int version) {
	struct TreeEngine engine;
	switch (version) {
	case 1:
		engine.growTree = growTree_v1;
		break;
	case 2:
	default:
		engine.growTree = growTree_v2;
		break;
	}
	return engine;
}

// print stdscr to terminal window
void printstdscr(void) {
	int maxY, maxX;
	getmaxyx(stdscr, maxY, maxX);

	// loop through each character on stdscr
	for (int y = 0; y < maxY; y++) {
		for (int x = 0; x < maxX; x++) {
			// grab cchar_t from stdscr
			cchar_t c;
			mvwin_wch(stdscr, y, x, &c);

			// grab wchar_t from cchar_t
			wchar_t wch[128] = {0};
			attr_t attrs;
			short color_pair;
			getcchar(&c, wch, &attrs, &color_pair, 0);

			short fg;
			short bg;
			pair_content(color_pair, &fg, &bg);

			// enable bold if needed
			if(attrs & A_BOLD) printf("\033[1m");
			else printf("\033[0m");

			// enable correct color
			if (fg == 0) printf("\033[0m");
			else if (fg <= 7) printf("\033[3%him", fg);
			else if (fg >= 8) printf("\033[9%him", fg - 8);

			printf("%ls", wch);

			short clen = wcslen(wch);
			short cwidth = 0;
			for (int i = 0; i < clen; ++i)
				cwidth += wcwidth(wch[i]);

			if (cwidth > 1)
				x += cwidth - 1;
		}
	}

	printf("\033[0m\n");
}

char* createDefaultCachePath(void) {
	char* result;
	size_t envlen;
	char* toAppend;

	// follow XDG Base Directory Specification for default cache file path
	const char* env_XDG_CACHE_HOME = getenv("XDG_CACHE_HOME");
	if (env_XDG_CACHE_HOME && (envlen = strlen(env_XDG_CACHE_HOME))) {
		toAppend = "/cbonsai";

		// create result buffer
		result = malloc(envlen + strlen(toAppend) + 1);
		strncpy(result, env_XDG_CACHE_HOME, envlen);
		strcpy(result + envlen, toAppend);
		return result;
	}

	// if we don't have $XDG_CACHE_HOME, try $HOME
	const char* env_HOME = getenv("HOME");
	if (env_HOME && (envlen = strlen(env_HOME))) {
		toAppend = "/.cache/cbonsai";

		// create result buffer
		result = malloc(envlen + strlen(toAppend) + 1);
		strncpy(result, env_HOME, envlen);
		strcpy(result + envlen, toAppend);
		return result;
	}

	// if we also don't have $HOME, just use ./cbonsai
	toAppend = "cbonsai";
	result = malloc(strlen(toAppend) + 1);
	strcpy(result, toAppend);
	return result;
}

int main(int argc, char* argv[]) {
	setlocale(LC_ALL, "");

	struct config conf = {	// defaults
		.live = 0,
		.infinite = 0,
		.screensaver = 0,
		.printTree = 0,
		.verbosity = 0,
		.lifeStart = 60,
		.multiplier = 10,
		.baseType = 1,
		.seed = 0,
		.leavesSize = 0,
		.version = 2,	// new trees use the latest engine; loaded saves use their tag
		.save = 0,
		.load = 0,
		.targetGlobalTime = 0,
		.proceduralMode = 0,

		.timeWait = 4,
		.timeStep = 0.03,

		.messageStartTime = 0,
		.messageTimeout = -1,

		.message = NULL,
		.leaves = {0},
		.saveFile = createDefaultCachePath(),
		.loadFile = createDefaultCachePath(),
		.no_disp = 0,
		.hideLeaves = 0,
	};

	struct option long_options[] = {
		{"live", no_argument, NULL, 'l'},
		{"time", required_argument, NULL, 't'},
		{"infinite", no_argument, NULL, 'i'},
		{"wait", required_argument, NULL, 'w'},
		{"screensaver", no_argument, NULL, 'S'},
		{"message", required_argument, NULL, 'm'},
		{"msgtime", required_argument, NULL, 'T'},
		{"base", required_argument, NULL, 'b'},
		{"leaf", required_argument, NULL, 'c'},
		{"multiplier", required_argument, NULL, 'M'},
		{"life", required_argument, NULL, 'L'},
		{"print", required_argument, NULL, 'p'},
		{"seed", required_argument, NULL, 's'},
		{"save", required_argument, NULL, 'W'},
		{"load", required_argument, NULL, 'C'},
		{"procedural", no_argument, NULL, 'P'},
		{"verbose", no_argument, NULL, 'v'},
		{"help", no_argument, NULL, 'h'},
		{"name", required_argument, NULL, 'N'},
		{"engine", required_argument, NULL, OPT_ENGINE},
		{"bare", no_argument, NULL, OPT_BARE},
		{0, 0, 0, 0}
	};

	struct ncursesObjects objects = {0};

	char leavesInput[128] = "█,█,█,▒,▒";

	// parse arguments
	int option_index = 0;
	int c;
	int real_save = 0;
	while ((c = getopt_long(argc, argv, ":lt:iw:Sm:b:c:M:L:ps:C:W:vhPN:T:", long_options, &option_index)) != -1) {
		switch (c) {
		case 'l':
			conf.live = 1;
			break;
		case 't':
			if (strtold(optarg, NULL) != 0) conf.timeStep = strtod(optarg, NULL);
			else {
				printf("error: invalid step time: '%s'\n", optarg);
				quit(&conf, &objects, 1);
			}
			if (conf.timeStep < 0) {
				printf("error: invalid step time: '%s'\n", optarg);
				quit(&conf, &objects, 1);
			}
			break;
		case 'T':
			if (strtold(optarg, NULL) != 0) conf.messageTimeout = strtod(optarg, NULL);
			else {
				printf("error: invalid message timeout: '%s'\n", optarg);
				quit(&conf, &objects, 1);
			}
			if (conf.messageTimeout < 0) {
				printf("error: invalid message timeout: '%s'\n", optarg);
				quit(&conf, &objects, 1);
			}
			break;
		case 'i':
			conf.infinite = 1;
			break;
		case 'w':
			if (strtold(optarg, NULL) != 0) conf.timeWait = strtod(optarg, NULL);
			else {
				printf("error: invalid wait time: '%s'\n", optarg);
				quit(&conf, &objects, 1);
			}
			if (conf.timeWait < 0) {
				printf("error: invalid wait time: '%s'\n", optarg);
				quit(&conf, &objects, 1);
			}
			break;
		case 'S':
			conf.live = 1;
			conf.infinite = 1;

			conf.save = 1;
			conf.load = 1;

			conf.screensaver = 1;
			break;
		case 'm':
			conf.message = optarg;
			break;
		case 'b':
			/* 0 can legitimately be returned, so we cannot check whether
			   strtold(optarg, NULL) != 0.  We need to set errno to zero
			   before the conversion attempt, and check if it changed
			   afterwards. */
			errno = 0;
			strtold(optarg, NULL);
			if (!errno)
				conf.baseType = strtod(optarg, NULL);
			else {
				printf("error: invalid base index: '%s'\n", optarg);
				quit(&conf, &objects, 1);
			}
			break;
		case 'c':
			strncpy(leavesInput, optarg, sizeof(leavesInput) - 1);
			leavesInput[sizeof(leavesInput) - 1] = '\0';
			break;
		case 'M':
			if (strtold(optarg, NULL) != 0) conf.multiplier = strtod(optarg, NULL);
			else {
				printf("error: invalid multiplier: '%s'\n", optarg);
				quit(&conf, &objects, 1);
			}
			// clamp to the supported range [1, 20]
			if (conf.multiplier < 1) conf.multiplier = 1;
			if (conf.multiplier > 20) conf.multiplier = 20;
			break;
		case 'L':
			if (strtold(optarg, NULL) != 0) conf.lifeStart = strtod(optarg, NULL);
			else {
				printf("error: invalid initial life: '%s'\n", optarg);
				quit(&conf, &objects, 1);
			}
			// clamp to the supported range [10, 500]
			if (conf.lifeStart < 10) conf.lifeStart = 10;
			if (conf.lifeStart > 500) conf.lifeStart = 500;
			break;
		case 'p':
			conf.printTree = 1;
			break;
		case 'P':
			conf.proceduralMode = 1;
			break;
		case 's':
			if (strtold(optarg, NULL) != 0) conf.seed = strtod(optarg, NULL);
			else {
				printf("error: invalid seed: '%s'\n", optarg);
				quit(&conf, &objects, 1);
			}
			if (conf.seed < 0) {
				printf("error: invalid seed: '%s'\n", optarg);
				quit(&conf, &objects, 1);
			}
			break;
		case 'W':
			// skip argument if it's actually an option
			if (optarg[0] == '-') optind -= 1;
			else {
				free(conf.saveFile);
				size_t bufsize = strlen(optarg) + 1;
				conf.saveFile = malloc(bufsize);
				strncpy(conf.saveFile, optarg, bufsize - 1);
				conf.saveFile[bufsize - 1] = '\0';
			}

			real_save = 1;
			conf.save = 1;
			break;
		case 'C':
			// skip argument if it's actually an option
			if (optarg[0] == '-') optind -= 1;
			else {
				free(conf.loadFile);
				size_t bufsize = strlen(optarg) + 1;
				conf.loadFile = malloc(bufsize);
				strncpy(conf.loadFile, optarg, bufsize - 1);
				conf.loadFile[bufsize - 1] = '\0';
			}

			conf.load = 1;
			break;
		case 'N':
			conf.namedTree = 1;
			conf.live = 1;               // Named trees are always live
			conf.proceduralMode = 1;     // Named trees are always procedural
			conf.save = 1;

			// Parse the time argument
			if (strtold(optarg, NULL) != 0) {
				conf.secondsPerTick = strtod(optarg, NULL);												// not quite, rather simulate, later
			} else {
				printf("error: invalid seconds per tick: '%s'\n", optarg);
				quit(&conf, &objects, 1);
			}
			if (conf.secondsPerTick <= 0) {
				printf("error: seconds per tick must be positive: '%s'\n", optarg);
				quit(&conf, &objects, 1);
			}
			break;

		case 'v':
			conf.verbosity++;
			break;

		case OPT_ENGINE:
			conf.version = atoi(optarg);
			if (conf.version < 1 || conf.version > 2) {
				printf("error: invalid engine version: '%s'\n", optarg);
				quit(&conf, &objects, 1);
			}
			break;

		case OPT_BARE:
			conf.hideLeaves = 1;
			break;

		// option has required argument, but it was not given
		case ':':
			switch (optopt) {
			case 'W':
				conf.save = 1;
				break;
			case 'C':
				conf.load = 1;
				break;
			default:
				printf("error: option requires an argument -- '%c'\n", optopt);
				printHelp();
				return 0;
				break;
			}
			break;

		// invalid option was given
		case '?':
			printf("error: invalid option -- '%c'\n", optopt);
			printHelp();
			return 0;
			break;

		case 'h':
			printHelp();
			return 0;
			break;
		}
	}

	// delimit leaves on "," and add each token to the leaves[] list
	char *token = strtok(leavesInput, ",");
	while (token != NULL) {
		if (conf.leavesSize < 100) conf.leaves[conf.leavesSize] = token;
		token = strtok(NULL, ",");
		conf.leavesSize++;
	}

	if (conf.load)
		loadFromFile(&conf);

	// seed random number generator
	if (conf.seed == 0) conf.seed = time(NULL);
	srand(conf.seed);

	struct counters myCounters;

	if (conf.namedTree) {
		if (!real_save) {
			printf("error: named trees require specifying a save file with -W\n");
			quit(&conf, &objects, 1);
		}

		// simulate actual time taken
		double targetSec = conf.secondsPerTick;

		init(&conf, &objects);
		conf.timeStep = 0;
		conf.no_disp = 1;
		get_engine(conf.version).growTree(&conf, &objects, &myCounters);
		conf.no_disp = 0;

		conf.secondsPerTick = targetSec / myCounters.globalTime;
		conf.timeStep = conf.secondsPerTick;
		conf.creationTime = time(NULL);
		srand(conf.seed);
	}

	do {
		init(&conf, &objects);
		get_engine(conf.version).growTree(&conf, &objects, &myCounters);
		if (conf.load) conf.targetGlobalTime = 0;
		if (conf.infinite) {
			timeout(conf.timeWait * 1000);
			int key = checkKeyPress(&conf, &myCounters);
			if (key == 1)
				quit(&conf, &objects, 0);
			// key == 2 (resize): next iteration calls init() which rebuilds windows

			conf.seed = time(NULL);
			srand(conf.seed);
		}
	} while (conf.infinite);

	finish(&conf, &myCounters);

	quit(&conf, &objects, 0);
}