/* Extra fighters (plat_fighters.c) and the page of extra modes that sets them up (plat_modes.c). C and C++. */
#ifndef PORT_PLAT_FIGHTERS_H
#define PORT_PLAT_FIGHTERS_H
#ifdef __cplusplus
extern "C" {
#endif

/* How many extra fighters a fight can have in this build, PORT_EXTRA_MAX (eight: ten fighters in all). THE one
   number: the battle code's tables, the settings' rows, the page's count control and the Experiments tab are all
   held to it. */
#include "../include/port_fighters.h"

/* plat_fighters.c: the settings, held to their ranges */
int Port_ExtraCount(void);      /* the Experiments tab's switch: how many extra fighters every Duel / training fight gets */
int Port_ExtraChara(int index); /* extra fighter `index`: its character 0..160 */
int Port_ExtraTeam(int index);  /* 0, 1: a player's team; -1: its own */
int Port_ExtraLevel(int index); /* its computer level 0..29 */
void Port_ExtraSet(int count, int index, int chara, int team, int level);
void Port_ExtraSetFighter(int index, int chara, int team, int level); /* the same without the switch (not written to the file yet) */
int Port_ExtraCharaOk(int chara, char *why, int size); /* its files are in the game data */
int Port_ExtraLast(void);                              /* what the last fight's set-up decided */
int Port_ExtraSetup(int mode, int members0, int members1, int split, int replay, int script); /* the loader's: a fight is being loaded */
int Port_ExtraAdd(int side, int index, int chara, int costume, int level);
void Port_ExtraDone(void);
int Port_TestRoster(int side, int index);
const char *Port_CharaName(int chara); /* a character's name from the game data ("" if it gave none); the window's thread only */

/* plat_modes.c: the page of extra modes (SELECT on the game's main menu) */
enum { PORT_MODE_NONE, PORT_MODE_TEAM, PORT_MODE_FFA, PORT_MODE_TRAINING, PORT_MODE_COUNT };
/* the rows of a mode's set-up screen */
enum { PORT_MODES_ROW_COUNT, PORT_MODES_ROW_CHARA, PORT_MODES_ROW_TEAM, PORT_MODES_ROW_LEVEL, PORT_MODES_ROW_START, PORT_MODES_ROW_BACK };

typedef struct PortModesView { /* the page as it is now, for the code that draws it (gs/ui.cpp) */
    int hint;   /* the game's main menu is up and takes input: "SELECT: more modes" is shown */
    int screen; /* 0 the page is closed, 1 the list of modes, 2 a mode's set-up */
    int cursor; /* the row the cursor is on */
    int mode;   /* PORT_MODE_ of the set-up */
    int count;  /* extra fighters chosen */
    int chara[PORT_EXTRA_MAX], team[PORT_EXTRA_MAX], level[PORT_EXTRA_MAX];
    int refused; /* 1 + the extra fighter whose character Start did not take (its files are missing); 0 none */
} PortModesView;

void Port_ModesView(PortModesView *out);
int Port_ModesRow(int mode, int count, int row, int *fighter); /* PORT_MODES_ROW_ of a set-up row (and whose), -1 past the last */
void Port_ModesKey(int back);  /* a key the bindings do not carry: Escape (back) */
int Port_ModesFight(void);     /* PORT_MODE_ the fights of now were asked with on the page, 0: none */
int Port_ModesCount(void);     /* ... and with how many extra fighters */
const char *Port_ModesName(int mode);

#ifdef __cplusplus
}
#endif
#endif
