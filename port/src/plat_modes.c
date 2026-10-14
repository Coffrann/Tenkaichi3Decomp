/*
 * The page of extra modes: SELECT on the game's main menu opens a page, drawn by the port over the menu, that lists
 * the kinds of fight with more than two fighters (Team Battle, Free-for-All, Training), sets one up (how many extra
 * fighters, and for each its character, team and computer level) and starts it THROUGH THE GAME'S OWN SCREENS: Start
 * makes the main menu choose the versus mode's or the training mode's character select, where the player picks
 * their own fighter, the opponent and the stage as always; the fight that follows has the extra fighters.
 *
 * Who does what:
 *   - src/menu/menu_a.c (the main menu) calls Port_ModesMenu on every frame it takes input: that is the only place
 *     the page can be opened from, it is what shows the hint, and it is where Start's request is handed over.
 *     Port_ModesLeave, at the menu's start, drops the choice: the game's own Duel is a fight of two again.
 *   - Port_ModesPad looks at every pad read (scePad2Read, plat_stub.c), after a recording or the controllers have
 *     answered it: while the page is up the buttons work the page and the game is handed idle pads, as it is while
 *     the settings window is up (gPortOverlayOpen), and after the page closes until the buttons are let go
 *     (PortInput_Swallow's rule). Being behind the pad read, the page works the same from the keyboard (through the
 *     player's bindings), from a controller and from a recording of pad reads (BT3_PAD_PLAY).
 *   - port/src/gs/ui.cpp draws the hint and the page from Port_ModesView. Nothing is decided there.
 *   - plat_fighters.c (Port_ExtraSetup) gives a fight its extra fighters while Port_ModesFight says a mode is on.
 *
 * What Start stores, in the settings: the rows of the extra fighters, extraN_chara / _team / _level, the SAME rows
 * the Experiments tab of the settings window edits; and two of its own, modes_fight (PORT_MODE_) and modes_extras
 * (how many), which Port_ModesLeave clears. The Experiments tab's switch (extra_fighters) is not touched: it is
 * "every Duel and training fight", this is "the fights started from the page".
 *
 * None of it in an online session: the page does not open, and a fight there never has extra fighters.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "plat_fighters.h"

#define CHARA_COUNT 161 /* characters 0..160 */
#define LEVEL_MAX 29
#define REPEAT_DELAY 20 /* pad reads (60 a second in the menus) a direction is held before it repeats */
#define REPEAT_EVERY 4

/* the bits of the pad's two button bytes together, 1 = pressed (include/sys/pad.h) */
enum { B_SELECT = 0x0001, B_START = 0x0008, B_UP = 0x0010, B_RIGHT = 0x0020, B_DOWN = 0x0040, B_LEFT = 0x0080, B_L1 = 0x0400,
       B_R1 = 0x0800, B_TRIANGLE = 0x1000, B_CIRCLE = 0x2000, B_CROSS = 0x4000 };
#define B_DIRS (B_UP | B_DOWN | B_LEFT | B_RIGHT | B_L1 | B_R1)

/* the game's menu sounds (Snd_PlaySe(1, n), as the main menu plays them) */
enum { SE_MOVE, SE_OK, SE_CANCEL };

/* the progress modes Start asks the main menu for (src/menu/menu_a_b.c) */
#define PROGRESS_VERSUS_SELECT 39
#define PROGRESS_TRAINING_SELECT 45

extern int Port_Setting(const char *name, int def); /* plat_settings.c */
extern void Port_SettingSave(const char *name, int value);
extern void Port_SettingsWrite(void);
extern int Port_NetActive(void), Port_NetSession(void); /* gs/net.c */

/* The page (worked on the game's thread by the pad reads; the drawing thread only reads it). */
static volatile int sScreen, sCursor, sMode, sCount, sRefused;
static volatile int sChara[PORT_EXTRA_MAX], sTeam[PORT_EXTRA_MAX], sLevel[PORT_EXTRA_MAX];
static volatile int sHint;    /* pad reads the hint still has: the main menu renews it on every frame it takes input */
static volatile int sKeyBack; /* Escape was pressed (the window's thread) */
static int sListCursor;       /* the list's row, kept while a set-up is open */
static unsigned sPrev;        /* the buttons of the last read, for "newly pressed" */
static int sHeld;             /* reads the same direction has been held */
static int sHold;             /* the page closed: the game gets idle pads until pad 1's buttons are let go */
static int sBlank;            /* this round of reads (pad 1, then pad 2) is not the game's */
static int sRequest;          /* Start: the progress mode the main menu is to choose */
static int sSound = -1;       /* a menu sound for the main menu to play */
static unsigned sReads;       /* pad 1 reads so far (BT3_MODES_LOG) */
extern unsigned gGsFrame;     /* gs/gs_core.c: pictures drawn so far (the same) */

static int logging(void) { /* BT3_MODES_LOG=1: what the page did, with the pad read it happened at (for making test input) */
    static int on = -1;
    if (on < 0) {
        on = getenv("BT3_MODES_LOG") != NULL;
    }
    return on;
}

const char *Port_ModesName(int mode) {
    static const char *const kNames[PORT_MODE_COUNT] = {"", "Team Battle", "Free-for-All", "Training"};
    return mode > PORT_MODE_NONE && mode < PORT_MODE_COUNT ? kNames[mode] : "";
}

/* The settings that say fights are being started from the page. */
int Port_ModesFight(void) {
    int m = Port_Setting("modes_fight", 0);
    return m > PORT_MODE_NONE && m < PORT_MODE_COUNT ? m : PORT_MODE_NONE;
}
int Port_ModesCount(void) {
    int n = Port_Setting("modes_extras", 0);
    return Port_ModesFight() == PORT_MODE_NONE || n < 0 ? 0 : n > PORT_EXTRA_MAX ? PORT_EXTRA_MAX : n;
}

/* The rows of a mode's set-up: the number of extra fighters; for each of them its character, its team where the
   mode has a choice of teams (in a Free-for-All everyone is on a team of their own), its level; Start; Back. */
int Port_ModesRow(int mode, int count, int row, int *fighter) {
    int each = mode == PORT_MODE_FFA ? 2 : 3;

    if (fighter != NULL) {
        *fighter = 0;
    }
    if (row < 0) {
        return -1;
    }
    if (row == 0) {
        return PORT_MODES_ROW_COUNT;
    }
    row--;
    if (row < count * each) {
        if (fighter != NULL) {
            *fighter = row / each;
        }
        row %= each;
        return row == 0 ? PORT_MODES_ROW_CHARA : row == each - 1 ? PORT_MODES_ROW_LEVEL : PORT_MODES_ROW_TEAM;
    }
    row -= count * each;
    return row == 0 ? PORT_MODES_ROW_START : row == 1 ? PORT_MODES_ROW_BACK : -1;
}

static int rows(void) {
    int n = 0;
    while (Port_ModesRow(sMode, sCount, n, NULL) >= 0) {
        n++;
    }
    return n;
}

void Port_ModesView(PortModesView *out) {
    int i;

    out->hint = sHint > 0 && sScreen == 0;
    out->screen = sScreen;
    out->cursor = sCursor;
    out->mode = sMode;
    out->count = sCount;
    out->refused = sRefused;
    for (i = 0; i < PORT_EXTRA_MAX; i++) {
        out->chara[i] = sChara[i];
        out->team[i] = sTeam[i];
        out->level[i] = sLevel[i];
    }
}

void Port_ModesKey(int back) {
    if (back && sScreen != 0) {
        sKeyBack = 1;
    }
}

static void close_page(int sound) {
    sScreen = 0;
    sHold = 1;
    sSound = sound;
    if (logging()) {
        fprintf(stderr, "bt3: modes: the page closed (pad read %u, frame %u)\n", sReads, gGsFrame);
    }
}

/* A mode's set-up opens with what the settings have: the rows the last Start (or the Experiments tab) left. */
static void open_setup(int mode) {
    int i;

    sMode = mode;
    sCount = Port_Setting("modes_count", 1);
    sCount = sCount < 1 ? 1 : sCount > PORT_EXTRA_MAX ? PORT_EXTRA_MAX : sCount;
    for (i = 0; i < PORT_EXTRA_MAX; i++) {
        sChara[i] = Port_ExtraChara(i);
        sLevel[i] = Port_ExtraLevel(i);
        sTeam[i] = Port_ExtraTeam(i);
        if (mode == PORT_MODE_TEAM && sTeam[i] < 0) {
            sTeam[i] = 1; /* (a Team Battle has the two players' teams) */
        }
    }
    sRefused = 0;
    sCursor = 0;
    sScreen = 2;
}

/* The next character in a direction whose files the game data has (one that has not could not be loaded). */
static int chara_step(int chara, int by) {
    char why[160];
    int tries;

    for (tries = 0; tries < CHARA_COUNT; tries++) {
        chara = ((chara + by) % CHARA_COUNT + CHARA_COUNT) % CHARA_COUNT;
        if (Port_ExtraCharaOk(chara, why, sizeof(why))) {
            break;
        }
        by = by < 0 ? -1 : 1;
    }
    return chara;
}

/* Start: the choice goes into the settings, and the main menu is asked for the mode's character select. */
static void start(void) {
    char why[160];
    int i;

    for (i = 0; i < sCount; i++) {
        if (!Port_ExtraCharaOk(sChara[i], why, sizeof(why))) {
            sRefused = i + 1; /* (the page says so, and stays) */
            sSound = SE_CANCEL;
            fprintf(stderr, "bt3: modes: not started: extra fighter %d, character %d: %s\n", i + 1, sChara[i], why);
            return;
        }
    }
    for (i = 0; i < sCount; i++) {
        Port_ExtraSetFighter(i, sChara[i], sMode == PORT_MODE_FFA ? -1 : sTeam[i], sLevel[i]);
    }
    Port_SettingSave("modes_count", sCount); /* (what the set-up opens with next time) */
    Port_SettingSave("modes_extras", sCount);
    Port_SettingSave("modes_fight", sMode);
    Port_SettingsWrite();
    sRequest = sMode == PORT_MODE_TRAINING ? PROGRESS_TRAINING_SELECT : PROGRESS_VERSUS_SELECT;
    fprintf(stderr, "bt3: modes: %s with %d extra fighter%s: to the game's character select\n", Port_ModesName(sMode), sCount,
            sCount == 1 ? "" : "s");
    close_page(SE_OK);
}

/* One pad read while the page is up: `pressed` are the buttons newly down (a held direction comes again). */
static void page_input(unsigned pressed) {
    int ok = (pressed & (B_CROSS | B_START)) != 0, back = (pressed & (B_CIRCLE | B_TRIANGLE | B_SELECT)) != 0;
    int move = (pressed & B_DOWN ? 1 : 0) - (pressed & B_UP ? 1 : 0);
    int step = (pressed & B_RIGHT ? 1 : 0) - (pressed & B_LEFT ? 1 : 0);
    int big = (pressed & B_R1 ? 1 : 0) - (pressed & B_L1 ? 1 : 0);

    if (sScreen == 1) { /* the list: the modes, then Back */
        int n = PORT_MODE_COUNT; /* (PORT_MODE_COUNT - 1 modes and Back) */
        if (back || (ok && sCursor == n - 1)) {
            close_page(SE_CANCEL);
        } else if (ok) {
            sListCursor = sCursor;
            open_setup(sCursor + 1);
            sSound = SE_OK;
        } else if (move != 0) {
            sCursor = (sCursor + move + n) % n;
            sSound = SE_MOVE;
        }
    } else {
        int who, row = Port_ModesRow(sMode, sCount, sCursor, &who), n = rows();
        int teams = sMode == PORT_MODE_TEAM ? 2 : 3; /* yours, the opponent's; in training also one of its own */

        if (back || (ok && row == PORT_MODES_ROW_BACK)) {
            sScreen = 1;
            sCursor = sListCursor;
            sSound = SE_CANCEL;
        } else if (ok && row == PORT_MODES_ROW_START) {
            start();
        } else if (move != 0) {
            sCursor = (sCursor + move + n) % n;
            sSound = SE_MOVE;
        } else if (step != 0 || big != 0 || ok) { /* (Cross on a value: its next) */
            int by = step != 0 ? step : big != 0 ? big * 10 : 1;
            if (row == PORT_MODES_ROW_COUNT) {
                int c = sCount + (by < 0 ? -1 : 1);
                sCount = c < 1 ? 1 : c > PORT_EXTRA_MAX ? PORT_EXTRA_MAX : c;
            } else if (row == PORT_MODES_ROW_CHARA) {
                sChara[who] = chara_step(sChara[who], by);
                sRefused = 0;
            } else if (row == PORT_MODES_ROW_TEAM) { /* 0 yours, 1 the opponent's, -1 its own: in that order */
                int at = sTeam[who] < 0 ? 2 : sTeam[who];
                at = (at + (by < 0 ? -1 : 1) + teams) % teams;
                sTeam[who] = at == 2 ? -1 : at;
            } else if (row == PORT_MODES_ROW_LEVEL) {
                int v = sLevel[who] + by;
                sLevel[who] = v < 0 ? 0 : v > LEVEL_MAX ? LEVEL_MAX : v;
            }
            sSound = SE_MOVE;
        }
    }
}

/* A pad read was answered (18 bytes: two button bytes, 0 = pressed, then the sticks; pad 1 is read first, then
   pad 2). While the page is up, and after it until pad 1's buttons are let go, both pads are handed on idle. */
void Port_ModesPad(int socket, unsigned char *data) {
    int i;

    if (socket == 0) {
        unsigned held = (unsigned)~(data[0] | data[1] << 8) & 0xFFFF, pressed;

        sReads++;
        if (sHint > 0) {
            sHint--;
        }
        if (data[5] < 0x40) { /* the left stick moves the cursor as the d-pad does */
            held |= B_UP;
        } else if (data[5] > 0xC0) {
            held |= B_DOWN;
        } else if (data[4] < 0x40) {
            held |= B_LEFT;
        } else if (data[4] > 0xC0) {
            held |= B_RIGHT;
        }
        pressed = held & ~sPrev;
        if ((held & B_DIRS) != 0 && (held & B_DIRS) == (sPrev & B_DIRS)) {
            if (++sHeld >= REPEAT_DELAY && (sHeld - REPEAT_DELAY) % REPEAT_EVERY == 0) {
                pressed |= held & B_DIRS;
            }
        } else {
            sHeld = 0;
        }
        sPrev = held;
        if (sScreen != 0 && (Port_NetSession() || Port_NetActive())) {
            sScreen = 0; /* (an online session began under the page) */
        }
        if (sScreen != 0) {
            if (sKeyBack) {
                pressed |= B_CIRCLE;
            }
            page_input(pressed);
            sBlank = 1;
        } else if (sHold) {
            sHold = held != 0;
            sBlank = sHold;
        } else {
            sBlank = 0;
        }
        sKeyBack = 0;
    }
    if (sBlank && socket >= 0 && socket < 2) {
        data[0] = data[1] = 0xFF;
        for (i = 2; i < 6; i++) {
            data[i] = 0x80;
        }
        for (i = 6; i < 18; i++) {
            data[i] = 0;
        }
    }
}

/* The main menu is up and takes input this frame (src/menu/menu_a.c); `select`: SELECT was pressed on pad 1.
   Returns the progress mode the menu is to choose (the page's Start), -1 when this frame's input is the page's,
   0 when it is the menu's. */
int Port_ModesMenu(int select) {
    int r;

    if (Port_NetSession() || Port_NetActive()) {
        return 0;
    }
    if (sHint == 0 && logging()) {
        fprintf(stderr, "bt3: modes: the main menu takes input (pad read %u, frame %u)\n", sReads, gGsFrame);
    }
    sHint = 3; /* (some reads' worth: the menu's frames come one read apart) */
    if (sRequest != 0) {
        r = sRequest;
        sRequest = 0;
        return r;
    }
    if (sScreen != 0) {
        return -1;
    }
    if (select) {
        sScreen = 1;
        sCursor = sListCursor = 0;
        sPrev = 0xFFFF; /* (what is held now, SELECT, is not a press on the page) */
        sHeld = 0;
        sSound = SE_OK;
        if (logging()) {
            fprintf(stderr, "bt3: modes: the page opened (pad read %u, frame %u)\n", sReads, gGsFrame);
        }
        return -1;
    }
    return 0;
}

/* A menu sound the page asks for (the main menu plays it: Snd_PlaySe(1, n)), -1: none. */
int Port_ModesSound(void) {
    int s = sSound;
    sSound = -1;
    return s;
}

/* The main menu starts: what the page asked for is over (and a choice left by a run that was closed in the middle
   of such a fight goes the same way: every start of the program comes through here before a fight). */
void Port_ModesLeave(void) {
    sScreen = 0;
    sRequest = 0;
    sSound = -1;
    if (Port_Setting("modes_fight", 0) != 0) {
        Port_SettingSave("modes_fight", 0);
        Port_SettingSave("modes_extras", 0);
        Port_SettingsWrite();
        fprintf(stderr, "bt3: modes: back at the main menu: fights are the game's own again\n");
    }
}
