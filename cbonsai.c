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

#define BRANCH_HISTORY 3 // moving average for proceedural leaves

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
};

struct ncursesObjects {
	WINDOW* baseWin;
	WINDOW* treeWin;
	WINDOW* messageBorderWin;
	WINDOW* messageWin;

	PANEL* basePanel;
	PANEL* treePanel;
	PANEL* messageBorderPanel;
	PANEL* messagePanel;
};

struct counters {
	int trunks;
	int branches;
	int shoots;
	int shootCounter;
	int trunkSplitCooldown;
	unsigned long long globalTime;
};

void delObjects(struct ncursesObjects *objects) {
	// delete panels
	del_panel(objects->basePanel);
	del_panel(objects->treePanel);
	del_panel(objects->messageBorderPanel);
	del_panel(objects->messagePanel);

	// delete windows
	delwin(objects->baseWin);
	delwin(objects->treeWin);
	delwin(objects->messageBorderWin);
	delwin(objects->messageWin);
}

void quit(struct config *conf, struct ncursesObjects *objects, int returnCode) {
	if (conf->proceduralMode) {
		// Restore original tree panel if we're in procedural mode (prevent error)
		if (!objects->treePanel) {
			objects->treePanel = new_panel(objects->treeWin);
		}
	}
	delObjects(objects);
	free(conf->saveFile);
	free(conf->loadFile);
	exit(returnCode);
}

int saveToFile(char* fname, int seed, unsigned long long globalTime, time_t creationTime, double secondsPerTick) {
	FILE *fp = fopen(fname, "w");

	if (!fp) {
		printf("error: file was not opened properly for writing: %s\n", fname);
		return 1;
	}

	fprintf(fp, "%d %llu %ld %.6f", seed, globalTime, creationTime, secondsPerTick);
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

	if (fscanf(fp, "%i %llu %ld %lf", &seed, &globalTime, &creationTime, &secondsPerTick) != 4) {
		printf("error: save file could not be read\n");
		return 1;
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
		saveToFile(conf->saveFile, conf->seed, myCounters->globalTime, 
				conf->creationTime, conf->secondsPerTick);
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
			"  -P, --procedural       enable procedural leaf generation mode\n"
			"  -i, --infinite         infinite mode: keep growing trees\n"
			"  -w, --wait=TIME        in infinite mode, wait TIME between each tree\n"
			"                           generation [default: 4.00]\n"
			"  -S, --screensaver      screensaver mode; equivalent to -li and\n"
			"                           quit on any keypress\n"
			"  -m, --message=STR      attach message next to the tree\n"
			"  -T, --msgtime=SECS     clear message after SECS seconds\n"
			"  -b, --base=INT         ascii-art plant base to use, 0 is none\n"
			"  -c, --leaf=LIST        list of comma-delimited strings randomly chosen\n"
			"                           for leaves\n"
			"  -M, --multiplier=INT   branch multiplier; higher -> more\n"
			"                           branching (0-20) [default: 8]\n"
			"  -N, --name=TIME        create a named tree that grows over real time,\n"
			"                           where TIME is life of tree in seconds.\n"
			"                           MUST be used with -C to specify a save file.\n"
			"                           (automatically enables -l and -P)\n"
			"  -L, --life=INT         life; higher -> more growth (0-200) [default: 120]\n"
			"  -p, --print            print tree to terminal when finished\n"
			"  -s, --seed=INT         seed random number generator\n"
			"  -W, --save=FILE        save progress to file [default: $XDG_CACHE_HOME/cbonsai or $HOME/.cache/cbonsai]\n"
			"  -C, --load=FILE        load progress from file [default: $XDG_CACHE_HOME/cbonsai]\n"
			"  -v, --verbose          increase output verbosity\n"
			"  -h, --help             show help\n"
	);
}

void drawBase(WINDOW* baseWin, int baseType) {
	// draw base art
	switch(baseType) {
	case 1:
		wattron(baseWin, A_BOLD | COLOR_PAIR(8));
		wprintw(baseWin, "%s", ":");
		wattron(baseWin, COLOR_PAIR(23));
		wprintw(baseWin, "%s", "__________");
		wattron(baseWin, COLOR_PAIR(20));
		wprintw(baseWin, "%s", "./~~~~\\.");
		wattron(baseWin, COLOR_PAIR(23));
		wprintw(baseWin, "%s", "___________");
		wattron(baseWin, COLOR_PAIR(8));
		wprintw(baseWin, "%s", ":");

		mvwprintw(baseWin, 1, 0, "%s", " \\                           / ");
		mvwprintw(baseWin, 2, 0, "%s", "  \\_________________________/ ");
		mvwprintw(baseWin, 3, 0, "%s", "  (_)                     (_)");

		wattroff(baseWin, A_BOLD);
		break;
	case 2:
		wattron(baseWin, COLOR_PAIR(8));
		wprintw(baseWin, "%s", "(");
		wattron(baseWin, COLOR_PAIR(2));
		wprintw(baseWin, "%s", "---");
		wattron(baseWin, COLOR_PAIR(11));
		wprintw(baseWin, "%s", "./~~~~\\.");
		wattron(baseWin, COLOR_PAIR(2));
		wprintw(baseWin, "%s", "--");
		wattron(baseWin, COLOR_PAIR(8));
		wprintw(baseWin, "%s", ")");

		mvwprintw(baseWin, 1, 0, "%s", " (           ) ");
		mvwprintw(baseWin, 2, 0, "%s", "  (_________)  ");
		break;
	}
}

void drawWins(int baseType, struct ncursesObjects *objects) {
	int baseWidth = 0;
	int baseHeight = 0;
	int rows, cols;

	switch(baseType) {
	case 1:
		baseWidth = 31;
		baseHeight = 4;
		break;
	case 2:
		baseWidth = 15;
		baseHeight = 3;
		break;
	}

	// calculate where base should go
	getmaxyx(stdscr, rows, cols);
	int baseOriginY = (rows - baseHeight);
	int baseOriginX = (cols / 2) - (baseWidth / 2);

	// clean up old objects
	delObjects(objects);

	// create windows
	objects->baseWin = newwin(baseHeight, baseWidth, baseOriginY, baseOriginX);
	objects->treeWin = newwin(rows - baseHeight, cols, 0, 0);

	// create tree and base panels
	objects->basePanel = new_panel(objects->baseWin);
	objects->treePanel = new_panel(objects->treeWin);

	drawBase(objects->baseWin, baseType);
}

// roll (randomize) a given die
static inline void roll(int *dice, int mod) { *dice = rand() % mod; }

// check for key press
int checkKeyPress(const struct config *conf, struct counters *myCounters) {
	int ch = wgetch(stdscr);
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
    else *blend_ratio = 1.0;
    return current_season;
}

// based on type of tree, determine what color a branch should be
void chooseColor(enum branchType type, WINDOW* treeWin) {
	int r;
	switch(type) {
	case trunk:
		r = rand() % 4;  // Get a number 0-3	
		if (r < 2) wattron(treeWin, A_BOLD | COLOR_PAIR(20));      // 0,1 (50% chance)
		else if (r == 2) wattron(treeWin, COLOR_PAIR(20));         // 2 (25% chance)
		else wattron(treeWin, COLOR_PAIR(21));                     // 3 (25% chance)
		break; 

	case shootLeft:
	case shootRight:
		r = rand() % 10;  // Get a number 0-9
		if (r < 2) wattron(treeWin, A_BOLD | COLOR_PAIR(20));
		else if (r < 6) wattron(treeWin, A_BOLD | COLOR_PAIR(21));
		else wattron(treeWin, COLOR_PAIR(21));
		break;

	case dying:
		r = rand() % 6;  // Get a number 0-5
		if (r < 3) wattron(treeWin, COLOR_PAIR(22));
		else if (r < 5) wattron(treeWin, A_BOLD | COLOR_PAIR(22));
		else wattron(treeWin, COLOR_PAIR(23));
		break;

	case dead:
		r = rand() % 18;  // Get a number 0-17
		if (r < 2) wattron(treeWin, A_BOLD | COLOR_PAIR(23));
		else if (r < 8) wattron(treeWin, A_BOLD | COLOR_PAIR(22));
		else wattron(treeWin, COLOR_PAIR(22));
		break;
	}
}

// New structures to add
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
	unsigned int leaf_seed;		// for proceedural consistency
	int x_history[BRANCH_HISTORY];          // Circular buffer for last BRANCH_HISTORY x positions
	int y_history[BRANCH_HISTORY];          // Circular buffer for last BRANCH_HISTORY y positions
	int history_count;         // How many positions we've stored (max BRANCH_HISTORY)
	int history_index;         // Current index in circular buffer
};

struct BranchList {
	struct Branch* branches;    // Dynamic array of branches
	int count;                  // Current number of branches
	int capacity;              // Current capacity of array
};

void initBranchList(struct BranchList* list) {
	list->capacity = 16;  // Initial capacity
	list->count = 0;
	list->branches = malloc(sizeof(struct Branch) * list->capacity);
}

void addBranch(struct BranchList* list, struct Branch branch, struct counters *myCounters) {
	myCounters->branches++;
	if (list->count >= list->capacity) {
		list->capacity *= 2;
		list->branches = realloc(list->branches, sizeof(struct Branch) * list->capacity);
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
	int sum_x = 0, sum_y = 0;
	
	// Use all available history points
	for (int i = 0; i < branch->history_count; i++) {
		sum_x += branch->x_history[i];
		sum_y += branch->y_history[i];
	}
	
	// Calculate averages
	*avg_x = sum_x / (branch->history_count > 0 ? branch->history_count : 1);
	*avg_y = sum_y / (branch->history_count > 0 ? branch->history_count : 1);
}

// Check if we're in the early trunk phase (first 30% of life)
static inline int isEarlyTrunk(int age, int totalLife) {
	return age < (totalLife * 14 / 20);  // 70% of total life
}

// Check if we're in the young trunk phase (first 15% of life)
static inline int isYoungTrunk(int age, int totalLife) {
	return age < (totalLife * 3 / 20);  // 15% of total life
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
			if (age % (int) (multiplier * 0.6) == 0) dy = -1;
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
			if (age % (int) (multiplier * 0.3) == 0) dy = -1;
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

void updateBranch(struct config *conf, struct ncursesObjects *objects, 
				struct counters *myCounters, struct Branch* branch, 
				struct BranchList* list) {

	// Simulate one step of branch growth
	if (checkKeyPress(conf, myCounters) == 1)
		quit(conf, objects, 0);

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

	int maxY = getmaxy(objects->treeWin);
	if (branch->dy > 0 && branch->y > (maxY - 2)) 
		branch->dy--; // reduce dy if too close to the ground

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
			if (conf->verbosity)
				mvwprintw(objects->treeWin, 4, 5, "shoots: %02d", myCounters->shoots);
			
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

			branch->life -= rand() % 3; // cost of sprouting
		}
	}
	myCounters->trunkSplitCooldown--;
	branch->shootCooldown--;
	branch->dripLeafCooldown--;

	if (conf->verbosity > 0) {
		mvwprintw(objects->treeWin, 5, 5, "dx: %02d", branch->dx);
		mvwprintw(objects->treeWin, 6, 5, "dy: %02d", branch->dy);
		mvwprintw(objects->treeWin, 7, 5, "type: %d", branch->type);
		mvwprintw(objects->treeWin, 8, 5, "shootCooldown: % 3d", branch->shootCooldown);
		mvwprintw(objects->treeWin, 9, 5, "globalTime: %llu", myCounters->globalTime);
		mvwprintw(objects->treeWin, 10, 5, "seed: %u", conf->seed);
		mvwprintw(objects->treeWin, 11, 5, "targetGlobalTime: %llu", conf->targetGlobalTime);
		mvwprintw(objects->treeWin, 12, 5, "secondsPerTick: %.6f", conf->secondsPerTick);
		mvwprintw(objects->treeWin, 13, 5, "timeStep: %.6f", conf->timeStep);
		mvwprintw(objects->treeWin, 14, 5, "loadState: %d", conf->load ? 1 : 0);
	}

	// move in x and y directions
	branch->x += branch->dx;
	branch->y += branch->dy;
	if(conf->proceduralMode && branch->type != dying && branch->type != dead)
		update_position_history(branch);

	chooseColor(branch->type, objects->treeWin);

	// choose string to use for this branch
	char *branchStr = chooseString(conf, branch->type, branch->life, branch->dx, branch->dy);

	// grab wide character from branchStr
	wchar_t wc = 0;
	mbstate_t *ps = 0;
	mbrtowc(&wc, branchStr, 32, ps);

	// print, but ensure wide characters don't overlap
	if(branch->x % wcwidth(wc) == 0)
		mvwprintw(objects->treeWin, branch->y, branch->x, "%s", branchStr);

	wattroff(objects->treeWin, A_BOLD);
	free(branchStr);
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
void createMessageWindows(struct ncursesObjects *objects, char* message) {
	int maxY, maxX;
	getmaxyx(stdscr, maxY, maxX);

	int boxWidth = 0;
	int boxHeight = 0;

	if (strlen(message) + 3 <= (0.25 * maxX)) {
		boxWidth = strlen(message) + 1;
		boxHeight = 1;
	} else {
		boxWidth = 0.25 * maxX;
		boxHeight = (strlen(message) / boxWidth) + (strlen(message) / boxWidth);
	}

	// create separate box for message border
	objects->messageBorderWin = newwin(boxHeight + 2, boxWidth + 4, (maxY * 0.3) - 1, (maxX * 0.7) - 2);
	objects->messageWin = newwin(boxHeight, boxWidth + 1, maxY * 0.3, maxX * 0.7);

	// draw box
	wattron(objects->messageBorderWin, COLOR_PAIR(8) | A_BOLD);
	wborder(objects->messageBorderWin, '|', '|', '-', '-', '+', '+', '+', '+');

	// create message panels
	objects->messageBorderPanel = new_panel(objects->messageBorderWin);
	objects->messagePanel = new_panel(objects->messageWin);
}

int drawMessage(struct config *conf, struct ncursesObjects *objects, char* message) {
	if (!message) return 1;

	conf->messageStartTime = time(NULL);  // Set start time when drawing message
	createMessageWindows(objects, message);

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
			mvwprintw(objects->treeWin, 11, 5, "word buffer: |% 15s|", wordBuffer);
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

		// define color pairs
		for(int i=0; i<16; i++){
			init_pair(i, i, bg);
		}

		float blend_ratio;
		enum Season season = get_current_season_with_blend(&blend_ratio);
		
		// Get current and next season colors
		struct ColorRGB current_colors = season_colors[season];
		struct ColorRGB prev_colors = season_colors[(season + 4) % 5];
		
		// Interpolate between seasons
		int r = interpolate_color(current_colors.r, prev_colors.r, blend_ratio);
		int g = interpolate_color(current_colors.g, prev_colors.g, blend_ratio);
		int b = interpolate_color(current_colors.b, prev_colors.b, blend_ratio);
		int r_2 = interpolate_color(current_colors.r_2, prev_colors.r_2, blend_ratio);
		int g_2 = interpolate_color(current_colors.g_2, prev_colors.g_2, blend_ratio);
		int b_2 = interpolate_color(current_colors.b_2, prev_colors.b_2, blend_ratio);
		
		init_color(16, 540, 270, 0);    // Lighter brown for trunk
		init_color(17, 280, 140, 0);    // Darker brown for branches
		init_color(18, r, g, b);  		// Main leaf color
		init_color(19, r_2, g_2, b_2);  // Darker variant

        init_pair(20, 16, bg);  // Trunk color 		(darker)
        init_pair(21, 17, bg); 	// branch color 	(lighter)
		init_pair(22, 18, bg);  // Leaf color 1		trunk-leaf
        init_pair(23, 19, bg); 	// Leaf color 2		branch-leaf

		if (can_change_color() == FALSE) {
			if (conf->verbosity > 0) {
				mvwprintw(objects->treeWin, 15, 5, "Terminal cannot change colors");
			}
		}
		
		// restrict color pallete in non-256color terminals (e.g. screen or linux)
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
	} else {
		printf("%s", "Warning: terminal does not have color support.\n");
		// Terminal doesn't support changing colors, fallback to basic colors
        init_pair(20, COLOR_YELLOW, bg);  // Trunk color
        init_pair(21, COLOR_YELLOW, bg);  // Branch color
        init_pair(22, COLOR_GREEN, bg);   // Main leaf color
        init_pair(23, COLOR_GREEN, bg);   // Secondary leaf color
	}

	// define and draw windows, then create panels
	drawWins(conf->baseType, objects);
	drawMessage(conf, objects, conf->message);
}

WINDOW* duplicateWindow(WINDOW* source) {
	int height, width, startx, starty;
	getbegyx(source, starty, startx);
	getmaxyx(source, height, width);
	
	WINDOW* duplicate = newwin(height, width, starty, startx);
	
	// Copy content and attributes
	for (int y = 0; y < height; y++) {
		for (int x = 0; x < width; x++) {
			chtype ch = mvwinch(source, y, x);
			mvwaddch(duplicate, y, x, ch);
		}
	}
	
	return duplicate;
}

// Simple recursive function to generate leaves at a position
void generateLeaves(struct config *conf, WINDOW* win, enum branchType type, int x, int y, int life, unsigned int leaf_seed) {
	while (life > 0) {
		if (life <= 0) return;
		life--;

		int dx = 0, dy = 0, dice;
		switch (type)
		{
		case 3: // dying: discourage vertical growth(?); trend left/right (-3,3)
			dice = rand_r(&leaf_seed) % 10;
			if (dice >= 0 && dice <=0) dy = -1;
			else if (dice >= 1 && dice <=8) dy = 0;
			else if (dice >= 9 && dice <=9) dy = 1;

			dice = rand_r(&leaf_seed) % 15;
			if (dice >= 0 && dice <=0) dx = -3;
			else if (dice >= 1 && dice <= 2) dx = -2;
			else if (dice >= 3 && dice <= 5) dx = -1;
			else if (dice >= 6 && dice <= 8) dx = 0;
			else if (dice >= 9 && dice <= 11) dx = 1;
			else if (dice >= 12 && dice <= 13) dx = 2;
			else if (dice >= 14 && dice <= 14) dx = 3;
			break;

		case 4: // dead: fill in surrounding area
			dice = rand_r(&leaf_seed) % 12;
			if (dice >= 0 && dice <= 1) dy = -1;
			else if (dice >= 2 && dice <= 8) dy = 0;
			else if (dice >= 9 && dice <= 11) dy = 1;
			
			dice = rand_r(&leaf_seed) % 15;
			if (dice >= 0 && dice <=1) dx = -3;
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

		int maxY, maxX;
		getmaxyx(win, maxY, maxX);
		if (dy > 0 && y > (maxY - 2)) dy--;

		generateLeaves(conf, win, type, x, y, life, rand_r(&leaf_seed)); // more leaves

		x += dx;
		y += dy;

		if (x >= 0 && x < maxX && y >= 0 && y < maxY) {

			switch(type) 
			{
			case trunk:
			case shootLeft:
			case shootRight:
				break;
			case dying:
				if (rand_r(&leaf_seed) % 6 == 0) wattron(win, COLOR_PAIR(22));
				else if (rand_r(&leaf_seed) % 2 == 0) wattron(win, A_BOLD | COLOR_PAIR(23));
				else wattron(win, COLOR_PAIR(23));
				break;

			case dead:
				if (rand_r(&leaf_seed) % 7 == 0) wattron(win, A_BOLD | COLOR_PAIR(22));
				else if (rand_r(&leaf_seed) % 2 == 0) wattron(win, A_BOLD | COLOR_PAIR(23));
				else wattron(win, COLOR_PAIR(23));
				break;
			}

			mvwprintw(win, y, x, "%s", conf->leaves[rand_r(&leaf_seed) % conf->leavesSize]);
		}
	}
}

int delayWithKeyCheck(struct config *conf, struct counters *myCounters, struct ncursesObjects *objects, float waitTime) {
	const float CHECK_INTERVAL = 0.2;  // Check every 0.2 seconds
	float remainingTime = waitTime;

	while (remainingTime > 0) {
		float sleepTime = (remainingTime > CHECK_INTERVAL) ? CHECK_INTERVAL : remainingTime;

		// Check if message should be cleared
		if (conf->messageTimeout > 0 && conf->message != NULL && objects->messagePanel != NULL) {
			time_t currentTime = time(NULL);
			if (currentTime - conf->messageStartTime >= conf->messageTimeout) {
				clearMessage(objects);
				conf->message = NULL;  // Clear the message pointer
			}
		}

		updateScreen(sleepTime);
		
		if (checkKeyPress(conf, myCounters) == 1) {
			return 1;  // Signal that we should quit
		}
		
		remainingTime -= sleepTime;
	}
	return 0;
}

void growTree(struct config *conf, struct ncursesObjects *objects, struct counters *myCounters) {
	int maxY, maxX;
	getmaxyx(objects->treeWin, maxY, maxX);
	WINDOW* tempState = NULL;
	PANEL* tempPanel = NULL;

	if (conf->proceduralMode && conf->live) {
		del_panel(objects->treePanel);
		objects->treePanel = NULL;
	}

	if (conf->verbosity > 0) {
		mvwprintw(objects->treeWin, 2, 5, "maxX: %03d, maxY: %03d", maxX, maxY);
	}

	// Initialize branch list
	struct BranchList branchList;
	initBranchList(&branchList);

	// reset counters
	myCounters->trunks = 0;
	myCounters->shoots = 0;
	myCounters->branches = 0;
	myCounters->shootCounter = 5;
	myCounters->globalTime = 0;
	myCounters->trunkSplitCooldown = 0;
	
	// Create initial trunk branch
	struct Branch initialBranch = {
		.x = maxX / 2,
		.y = maxY - 1,
		.life = conf->lifeStart,
		.age = 0,
		.type = trunk,
		.shootCooldown = conf->multiplier,
		.dripLeafCooldown = conf->multiplier + conf->lifeStart / 4,
		.totalLife = conf->lifeStart,
		.multiplier = conf->multiplier
	};
	addBranch(&branchList, initialBranch, myCounters);

	if (conf->live && conf->proceduralMode && tempState == NULL) {
		tempState = duplicateWindow(objects->treeWin);
		tempPanel = new_panel(tempState);

		// If we have message windows, recreate them on top
		if (objects->messageWin && objects->messageBorderWin) {
			// Store current panels/windows
			PANEL* oldMessagePanel = objects->messagePanel;
			PANEL* oldBorderPanel = objects->messageBorderPanel;
			WINDOW* messageWin = objects->messageWin;
			WINDOW* borderWin = objects->messageBorderWin;
			
			// Create new panels for existing windows
			objects->messagePanel = new_panel(messageWin);
			objects->messageBorderPanel = new_panel(borderWin);
			
			// Delete old panels
			del_panel(oldMessagePanel);
			del_panel(oldBorderPanel);
			
			// Ensure message panels are on top
			top_panel(objects->messageBorderPanel);
			top_panel(objects->messagePanel);
		}
	}

	// Main growth loop
	int turn = 0;
	while (branchList.count > 0) {
		myCounters->globalTime++;

		if (branchList.branches[turn].life <= 0) {
			struct Branch* b = &branchList.branches[turn];
			if (conf->proceduralMode &&
				b->type != dying && b->type != dead) {
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

				generateLeaves(conf, objects->treeWin, newType, avg_x, avg_y, leafLife, leaf_seed);
			}
			removeBranch(&branchList, turn);
			if (turn >= branchList.count) {
				turn = 0;
			}
			continue;
		}

		// do the tree state update
		updateBranch(conf, objects, myCounters, &branchList.branches[turn], &branchList);

		if (conf->live && conf->proceduralMode) {
			werase(tempState);
			overwrite(objects->treeWin, tempState);

			for (int i = 0; i < branchList.count; i++) {
				struct Branch* b = &branchList.branches[i];

				double lifeRatio = ((double)b->age) / b->totalLife;

				// Skip if branch is already dying/dead or not a main growth branch
				if (b->type != trunk && b->type != shootLeft && b->type != shootRight) 
					continue;

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

				generateLeaves(conf, tempState, newType, avg_x, avg_y, leafLife, leaf_seed);
			}
		}

		turn = (turn + 1) % branchList.count;
		
		if (conf->live && !(conf->load && myCounters->globalTime < conf->targetGlobalTime)) {
			if (checkKeyPress(conf, myCounters) == 1) {
				freeBranchList(&branchList);

				if (tempState) {
					del_panel(tempPanel);
					delwin(tempState);
				}
				return;
			}

			if (!conf->no_disp && delayWithKeyCheck(conf, myCounters, objects, conf->timeStep)) quit(conf, objects, 0);
		}
	}
	
	freeBranchList(&branchList);

	// display changes
	if (!conf->no_disp) {
		update_panels();
		doupdate();
	}
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
		.lifeStart = 120,
		.multiplier = 8,
		.baseType = 1,
		.seed = 0,
		.leavesSize = 0,
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
		{0, 0, 0, 0}
	};

	struct ncursesObjects objects = { NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL };

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
						/* 0 can legitimately be returned, so we cannot check wether
						   strtold(optarg, NULL) != 0.  We need to set errno to zero
						   before the conversion attempt, and check it it changed
						   afterwards. */
						errno = 0;
						strtold(optarg, NULL);
						if (!errno) conf.baseType = strtod(optarg, NULL);
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
			if (conf.multiplier < 0) {
				printf("error: invalid multiplier: '%s'\n", optarg);
				quit(&conf, &objects, 1);
			}
			break;
		case 'L':
			if (strtold(optarg, NULL) != 0) conf.lifeStart = strtod(optarg, NULL);
			else {
				printf("error: invalid initial life: '%s'\n", optarg);
				quit(&conf, &objects, 1);
			}
			if (conf.lifeStart < 0) {
				printf("error: invalid initial life: '%s'\n", optarg);
				quit(&conf, &objects, 1);
			}
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
		growTree(&conf, &objects, &myCounters);
		conf.no_disp = 0;

		conf.secondsPerTick = targetSec / myCounters.globalTime;
		conf.timeStep = conf.secondsPerTick;
		conf.creationTime = time(NULL);
		srand(conf.seed);
	}

	do {
		init(&conf, &objects);
		growTree(&conf, &objects, &myCounters);
		if (conf.load) conf.targetGlobalTime = 0;
		if (conf.infinite) {
			timeout(conf.timeWait * 1000);
			if (checkKeyPress(&conf, &myCounters) == 1)
				quit(&conf, &objects, 0);

			// seed random number generator
			conf.seed = time(NULL);
			srand(conf.seed);
		}
	} while (conf.infinite);

	wgetch(objects.treeWin);
	finish(&conf, &myCounters);

	quit(&conf, &objects, 0);
}
