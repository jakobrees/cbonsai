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

/*

	==== Change Ideas: ====

	Make branching likelihood proportional to life of main trunk
		- For a brief period at start make shoots very unlikely

	Have turnk growing phases where there is no branching possible
	and vertical growth is unlikely (yes back and forth growth)
		- cause snaking behavior
		- cause thicker trunk (more so at bottom?)
		- critical: grow the base trunk in the pot. (looks more natural)
					reuqires checking of base stuff. We could maintain as
					we are on lowest level.

	Try leaf phases where droopy leaves are added
		- May require (leaf-shoot type)

	Try not to overwrite leaves (random chance ~30% that we do)
		- Requires chekcing ncurses object(?)
		- May be too much and unecessary

	We want distinct branches, how can we do this? Increase branching cooldown

*/

enum branchType {trunk, shootLeft, shootRight, dying, dead};

struct config {
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
	int targetBranchCount;

	double timeWait;
	double timeStep;

	char* message;
	char* leaves[64];
	char* saveFile;
	char* loadFile;
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
	delObjects(objects);
	free(conf->saveFile);
	free(conf->loadFile);
	exit(returnCode);
}

int saveToFile(char* fname, int seed, int branchCount) {
	FILE *fp = fopen(fname, "w");

	if (!fp) {
		printf("error: file was not opened properly for writing: %s\n", fname);
		return 1;
	}

	fprintf(fp, "%d %d", seed, branchCount);
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

	int seed, targetBranchCount;
	if (fscanf(fp, "%i %i", &seed, &targetBranchCount) != 2) {
		printf("error: save file could not be read\n");
		return 1;
	}

	conf->seed = seed;
	conf->targetBranchCount = targetBranchCount;

	fclose(fp);

	return 0;
}

void finish(const struct config *conf, struct counters *myCounters) {
	clear();
	refresh();
	endwin();	// delete ncurses screen
	if (conf->save)
		saveToFile(conf->saveFile, conf->seed, myCounters->branches);
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
			"  -i, --infinite         infinite mode: keep growing trees\n"
			"  -w, --wait=TIME        in infinite mode, wait TIME between each tree\n"
			"                           generation [default: 4.00]\n"
			"  -S, --screensaver      screensaver mode; equivalent to -li and\n"
			"                           quit on any keypress\n"
			"  -m, --message=STR      attach message next to the tree\n"
			"  -b, --base=INT         ascii-art plant base to use, 0 is none\n"
			"  -c, --leaf=LIST        list of comma-delimited strings randomly chosen\n"
			"                           for leaves\n"
			"  -M, --multiplier=INT   branch multiplier; higher -> more\n"
			"                           branching (0-20) [default: 5]\n"
			"  -L, --life=INT         life; higher -> more growth (0-200) [default: 32]\n"
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
		wattron(baseWin, COLOR_PAIR(2));
		wprintw(baseWin, "%s", "__________");
		wattron(baseWin, COLOR_PAIR(11));
		wprintw(baseWin, "%s", "./~~~~~\\.");
		wattron(baseWin, COLOR_PAIR(2));
		wprintw(baseWin, "%s", "__________");
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
		wprintw(baseWin, "%s", "./~~~\\.");
		wattron(baseWin, COLOR_PAIR(2));
		wprintw(baseWin, "%s", "---");
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
	if ((conf->screensaver && wgetch(stdscr) != ERR) || (wgetch(stdscr) == 'q')) {
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

// based on type of tree, determine what color a branch should be
void chooseColor(enum branchType type, WINDOW* treeWin) {
	switch(type) {
	case trunk:
	case shootLeft:
	case shootRight:
		if (rand() % 2 == 0) wattron(treeWin, A_BOLD | COLOR_PAIR(11));
		else wattron(treeWin, COLOR_PAIR(3));
		break;

	case dying:
		if (rand() % 10 == 0) wattron(treeWin, A_BOLD | COLOR_PAIR(2));
		else wattron(treeWin, COLOR_PAIR(2));
		break;

	case dead:
		if (rand() % 3 == 0) wattron(treeWin, A_BOLD | COLOR_PAIR(10));
		else wattron(treeWin, COLOR_PAIR(10));
		break;
	}
}

// Check if we're in the mid trunk phase (first 30% of life)
static inline int isMidTrunk(int age, int totalLife) {
	return age < (totalLife * 6 / 10);  // 60% of total life
}

// Check if we're in the early trunk phase (first 30% of life)
static inline int isEarlyTrunk(int age, int totalLife) {
	return age < (totalLife * 7 / 20);  // 35% of total life
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
			// every (multiplier * 0.5) steps, raise tree to next level
			if (age % (int) (multiplier * 0.5) == 0) dy = -1;
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

void branch(struct config *conf, struct ncursesObjects *objects, struct counters *myCounters, int y, int x, enum branchType type, int life) {
	myCounters->branches++;
	int dx = 0;
	int dy = 0;
	int age = 0;
	int shootCooldown = conf->multiplier;
    int dripLeafCooldown = conf->multiplier + life / 4;

	while (life > 0) {
		if (checkKeyPress(conf, myCounters) == 1)
			quit(conf, objects, 0);

		life--;		// decrement remaining life counter

		// Random die-off check - more likely on shoots
		if (type == trunk) {
			if (rand() % 66 == 0) {   // 2% chance for trunk
				life -= (life/2);       // Lose 1/4 of life
			}
		} else if (type == shootLeft || type == shootRight) {
			if (rand() % 20 == 0) {    // 5% chance for shoots
				life /= 2;              // Lose half of life
			}
		}

		age = conf->lifeStart - life;

		setDeltas(type, life, conf->lifeStart, age, conf->multiplier, &dx, &dy);

		int maxY = getmaxy(objects->treeWin);
		if (dy > 0 && y > (maxY - 2)) dy--; // reduce dy if too close to the ground

		// near-dead branch should branch into a lot of leaves
		if (life < 6) 
			branch(conf, objects, myCounters, y, x, dead, life);

		else if (type == shootLeft || type == shootRight) {
			//int leafThreshold = (conf->multiplier /3) + (conf->lifeStart / 12);
			if (life < 7 + (conf->multiplier /5)) {
				branch(conf, objects, myCounters, y, x, dying, life + 1);
			}
            else if (dripLeafCooldown <= 0 && (rand() % 7) == 0) {
                branch(conf, objects, myCounters, y, x, dying, 5);
                dripLeafCooldown = (conf->multiplier*2)/3;
            }
		}

		// dying trunk should branch into a lot of leaves
		else if (type == 0 && life < (conf->multiplier + 2))
			branch(conf, objects, myCounters, y, x, dying, life);

		// dying shoot should branch into a lot of leaves
		else if ((type == shootLeft || type == shootRight) && life < (conf->multiplier + 2))
			branch(conf, objects, myCounters, y, x, dying, life);

		// trunks should re-branch if not close to ground AND either randomly, or upon every <multiplier> steps
		/* else if (type == 0 && ( \ */
		/* 		(rand() % (conf.multiplier)) == 0 || \ */
		/* 		(life > conf.multiplier && life % conf.multiplier == 0) */
		/* 		) ) { */
		else if (type == trunk) {
			// First check for trunk splits - only in early phase and with enough life
			if (!isYoungTrunk(age, conf->lifeStart)) {
				// More likely to split near the bottom

				int splitThreshold = (24 - conf->multiplier) +  (2 * myCounters->trunks);
				double ageRatio = (double)age / conf->lifeStart;

				if (ageRatio < 0.1) {      // Bottom 10%
					splitThreshold = (splitThreshold * 2)/7;
				} else if (ageRatio < 0.4) {
					splitThreshold = (splitThreshold * 3)/7;
				} else {
                    splitThreshold = (splitThreshold * 5)/7;
                }

                //splitThreshold += 2 * myCounters->trunks;

				if (rand() % splitThreshold == 0) {
                    myCounters->trunks++;
					shootCooldown = (25 - conf->multiplier)/4;
					branch(conf, objects, myCounters, y, x, trunk, life - (rand() % 6));
				}

                life -= rand() % 3; // cost of sprouting 
			}
			
			// Then check for regular branch shoots
			int branchDice = getBranchRollThreshold(age, conf->lifeStart, conf->multiplier);
			if (shootCooldown <= 0 && (rand() % branchDice == 0)) {
				shootCooldown = myCounters->trunks + (25 - conf->multiplier)/6;
				int shootLife = (life + (rand() % conf->multiplier) - 2);
				
				myCounters->shoots++;
				myCounters->shootCounter++;
				if (conf->verbosity)
					mvwprintw(objects->treeWin, 4, 5, "shoots: %02d", myCounters->shoots);
					
				branch(conf, objects, myCounters, y, x, 
					(myCounters->shootCounter % 2) + 1, shootLife);
				
				life -= rand() % ((conf->lifeStart - age)/4); // cost of sprouting 
			}
		}
		shootCooldown--;
        dripLeafCooldown--;

		if (conf->verbosity > 0) {
			mvwprintw(objects->treeWin, 5, 5, "dx: %02d", dx);
			mvwprintw(objects->treeWin, 6, 5, "dy: %02d", dy);
			mvwprintw(objects->treeWin, 7, 5, "type: %d", type);
			mvwprintw(objects->treeWin, 8, 5, "shootCooldown: % 3d", shootCooldown);
		}

		// move in x and y directions
		x += dx;
		y += dy;

		chooseColor(type, objects->treeWin);

		// choose string to use for this branch
		char *branchStr = chooseString(conf, type, life, dx, dy);

		// grab wide character from branchStr
		wchar_t wc = 0;
		mbstate_t *ps = 0;
		mbrtowc(&wc, branchStr, 32, ps);

		// print, but ensure wide characters don't overlap
		if(x % wcwidth(wc) == 0)
			mvwprintw(objects->treeWin, y, x, "%s", branchStr);

		wattroff(objects->treeWin, A_BOLD);
		free(branchStr);

		// if live, update screen
		// skip updating if we're still loading from file
		if (conf->live && !(conf->load && myCounters->branches < conf->targetBranchCount))
			updateScreen(conf->timeStep);
	}
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
	objects->messageBorderWin = newwin(boxHeight + 2, boxWidth + 4, (maxY * 0.7) - 1, (maxX * 0.7) - 2);
	objects->messageWin = newwin(boxHeight, boxWidth + 1, maxY * 0.7, maxX * 0.7);

	// draw box
	wattron(objects->messageBorderWin, COLOR_PAIR(8) | A_BOLD);
	wborder(objects->messageBorderWin, '|', '|', '-', '-', '+', '+', '+', '+');

	// create message panels
	objects->messageBorderPanel = new_panel(objects->messageBorderWin);
	objects->messagePanel = new_panel(objects->messageWin);
}

int drawMessage(const struct config *conf, struct ncursesObjects *objects, char* message) {
	if (!message) return 1;

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

void init(const struct config *conf, struct ncursesObjects *objects) {
	savetty();	// save terminal settings
	initscr();	// init ncurses screen
	noecho();	// don't echo input to screen
	curs_set(0);	// make cursor invisible
	cbreak();	// don't wait for new line to grab user input
	nodelay(stdscr, TRUE);	// force getch to be a non-blocking call

	// if terminal has color capabilities, use them
	if (has_colors()) {
		start_color();

		// use native background color when possible
		int bg = COLOR_BLACK;
		if (use_default_colors() != ERR) bg = -1;

		// define color pairs
		for(int i=0; i<16; i++){
			init_pair(i, i, bg);
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
	}

	// define and draw windows, then create panels
	drawWins(conf->baseType, objects);
	drawMessage(conf, objects, conf->message);
}

void growTree(struct config *conf, struct ncursesObjects *objects, struct counters *myCounters) {
	// Clear the window at start
    werase(objects->treeWin);

	int maxY, maxX;
	getmaxyx(objects->treeWin, maxY, maxX);

	// reset counters
    myCounters->trunks = 0;
	myCounters->shoots = 0;
	myCounters->branches = 0;
	myCounters->shootCounter = rand();

	if (conf->verbosity > 0) {
		mvwprintw(objects->treeWin, 2, 5, "maxX: %03d, maxY: %03d", maxX, maxY);
	}

	// recursively grow tree trunk and branches
	branch(conf, objects, myCounters, maxY - 1, (maxX / 2), trunk, conf->lifeStart);

	// display changes
	update_panels();
	doupdate();
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
		.lifeStart = 80,
		.multiplier = 8,
		.baseType = 1,
		.seed = 0,
		.leavesSize = 0,
		.save = 0,
		.load = 0,
		.targetBranchCount = 0,

		.timeWait = 4,
		.timeStep = 0.03,

		.message = NULL,
		.leaves = {0},
		.saveFile = createDefaultCachePath(),
		.loadFile = createDefaultCachePath(),
	};

	struct option long_options[] = {
		{"live", no_argument, NULL, 'l'},
		{"time", required_argument, NULL, 't'},
		{"infinite", no_argument, NULL, 'i'},
		{"wait", required_argument, NULL, 'w'},
		{"screensaver", no_argument, NULL, 'S'},
		{"message", required_argument, NULL, 'm'},
		{"base", required_argument, NULL, 'b'},
		{"leaf", required_argument, NULL, 'c'},
		{"multiplier", required_argument, NULL, 'M'},
		{"life", required_argument, NULL, 'L'},
		{"print", required_argument, NULL, 'p'},
		{"seed", required_argument, NULL, 's'},
		{"save", required_argument, NULL, 'W'},
		{"load", required_argument, NULL, 'C'},
		{"verbose", no_argument, NULL, 'v'},
		{"help", no_argument, NULL, 'h'},
		{0, 0, 0, 0}
	};

	struct ncursesObjects objects = { NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL };

	char leavesInput[128] = "█,█,█,▒,▒,░";

	// parse arguments
	int option_index = 0;
	int c;
	while ((c = getopt_long(argc, argv, ":lt:iw:Sm:b:c:M:L:ps:C:W:vh", long_options, &option_index)) != -1) {
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

	do {
		init(&conf, &objects);
		growTree(&conf, &objects, &myCounters);
		if (conf.load) conf.targetBranchCount = 0;
		if (conf.infinite) {
			timeout(conf.timeWait * 1000);
			if (checkKeyPress(&conf, &myCounters) == 1)
				quit(&conf, &objects, 0);

			// seed random number generator
			srand(time(NULL));
		}
	} while (conf.infinite);

	if (conf.printTree) {
		finish(&conf, &myCounters);

		// overlay all windows onto stdscr
		overlay(objects.baseWin, stdscr);
		overlay(objects.treeWin, stdscr);
		overwrite(objects.messageBorderWin, stdscr);
		overwrite(objects.messageWin, stdscr);

		printstdscr();
	} else {
		wgetch(objects.treeWin);
		finish(&conf, &myCounters);
	}

	quit(&conf, &objects, 0);
}
