/*
 * Extra fighters (the experiment of more than two fighters): who they are, and whether a fight has them.
 *
 * The battle code (src/battle) asks through port/include/port_fighters.h: how many extra fighters the fight has
 * (Port_FightExtras, 0..PORT_EXTRA_MAX) and for extra k its character, costume, team and computer level. Extra k
 * is roster index 2 + k; fighters 0 and 1 are the two sides' own. The answers are decided ONCE PER FIGHT, by
 * Port_ExtraSetup, which the loader calls just before it asks for the fighters' models: a setting changed in the
 * middle of a fight changes the next one, and a fight never sees two answers. (Port_ThirdFighter, Port_ThirdTeam
 * and Port_ThirdLevel are extra 0 by its older name.)
 *
 * Where the answers come from, the first that applies:
 *   - the environment (the tests), in every kind of fight:
 *       BT3_EXTRA_FIGHTERS=<character>:<team>[:<level>],<character>:<team>,...   any number up to PORT_EXTRA_MAX;
 *                         team 0 is player 1's, 1 player 2's, 2 and up are further teams; level 0..29
 *       BT3_THIRD_FIGHTER=<character 0..160>   one, with BT3_THIRD_TEAM (1 with player 2, the default; 0 with
 *                         player 1; 2 on its own) and BT3_THIRD_LEVEL (0..29)
 *   - the sides' own team rosters, when the setting modes_fight says so: 1 Team Battle, 2 Free-for-All (written
 *     by the menu code, cleared at the main menu; BT3_MODES_FIGHT=<n> stands in for it in a test, in any kind of
 *     fight). Every member the players picked on the game's own team select fights AT ONCE: fighter 0 is side 0's
 *     first member, fighter 1 side 1's, and the other members are the extra fighters, side 0's first. In a Team
 *     Battle a fighter's team is its side; in a Free-for-All everyone is alone. The loader hands the members over
 *     (Port_ExtraAdd) and then leaves each side ONE member, so that the game's own switching of members has
 *     nothing to switch. Only in a fight of the versus menu, with no script, one view, not a replay.
 *   - else the settings' rows (the Experiments tab of the settings window, port/src/gs/ui.cpp, and the Training
 *     entry of the page of extra modes, plat_modes.c):
 *         extra_fighters   how many extra fighters (the Experiments switch), or modes_extras with modes_fight 3
 *         extraN_chara     extra N's character
 *         extraN_team      its team: 0 player 1's, 1 player 2's, -1 a team of its own (team 2 + N)
 *         extraN_level     its computer level, 0..29
 *     From the settings a fight gets them only when it is a plain one against one of Duel or of the training
 *     mode: no script, one member a side, one view, not a replay.
 *
 * Whatever asked, there are none in an online match (the two copies must compute the game's own fight), and a
 * character whose files are not in the data folder is left out (the loader would wait for them for ever).
 *
 * The characters' names for the window are read from the player's own data (the versus select's text in the
 * first menu archive): nothing of them is in the repository.
 */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "gs/menu_labels.h"
#include "plat_fighters.h"
#include "port_fighters.h"

#define CHARA_COUNT 161  /* characters 0..160 (MCHR_RANDOM, include/menu/menu_d.h) */
#define EXTRA_MAX PORT_EXTRA_MAX /* extra fighters a fight can have so far */
#define FILE_CHARA 0x590 /* file ids of character c: c * 10 + this is its model, + 8 and + 9 its two animation files
                            (what battle_load.c asks for) */

extern int Port_Setting(const char *name, int def); /* plat_settings.c */
extern void Port_SettingSave(const char *name, int value);
extern void Port_SettingsWrite(void);
extern int Port_FilePath(int ptid, int flid, const char *fname, char *out, int size); /* plat_file.c */
extern int Port_NetActive(void);                                    /* gs/net.c */
extern int PortA11y_Utf8(char *out, int cap, const uint16_t *str); /* gs/a11y.c: the game's text as UTF-8 */

/* The fight's answers (set by Port_ExtraSetup on the game's thread, read by it). */
typedef struct FightExtra {
    int chara, costume, team, level;
    int member; /* side * 8 + index of the member of the battle setup it was made from, -1: none */
} FightExtra;
static FightExtra sFight[PORT_EXTRA_MAX];
static int sFightCount;
static int sFightRoster; /* PORT_MODE_TEAM or PORT_MODE_FFA while the sides' members are being handed over, and after */
static const char *sFightFrom = ""; /* who asked, for the log */
/* What the last set-up decided, for the window (one of PORT_EXTRA_*; an int, the window is another thread's). */
enum { EXTRA_NONE, EXTRA_IN, EXTRA_MODE, EXTRA_TEAMS, EXTRA_SPLIT, EXTRA_REPLAY, EXTRA_SCRIPT, EXTRA_ONLINE, EXTRA_FILES };
static volatile int sLast = EXTRA_NONE;

/* The name of a setting of entry `index`: "extra0_chara", "extra1_team", ... (into the caller's buffer: the window's
   thread asks too). */
typedef char SettingName[24];
static const char *setting_name(SettingName out, int index, const char *what) {
    snprintf(out, sizeof(SettingName), "extra%d_%s", index, what);
    return out;
}

/* The settings, held to their ranges (the file may have been written by hand). */
int Port_ExtraCount(void) {
    int n = Port_Setting("extra_fighters", 0);
    return n < 0 ? 0 : n > EXTRA_MAX ? EXTRA_MAX : n;
}
int Port_ExtraChara(int index) {
    SettingName name;
    int c = index >= 0 && index < EXTRA_MAX ? Port_Setting(setting_name(name, index, "chara"), 0) : 0;
    return c < 0 || c >= CHARA_COUNT ? 0 : c;
}
int Port_ExtraTeam(int index) { /* 0, 1: a player's team; -1: its own */
    SettingName name;
    int t = index >= 0 && index < EXTRA_MAX ? Port_Setting(setting_name(name, index, "team"), 1) : 1;
    return t < -1 || t > 1 ? 1 : t;
}
int Port_ExtraLevel(int index) {
    SettingName name;
    int v = index >= 0 && index < EXTRA_MAX ? Port_Setting(setting_name(name, index, "level"), 13) : 13;
    return v < 0 ? 0 : v > 29 ? 29 : v;
}
void Port_ExtraSetFighter(int index, int chara, int team, int level) { /* (the caller writes the file) */
    SettingName name;

    if (index < 0 || index >= EXTRA_MAX) {
        return;
    }
    Port_SettingSave(setting_name(name, index, "chara"), chara);
    Port_SettingSave(setting_name(name, index, "team"), team);
    Port_SettingSave(setting_name(name, index, "level"), level);
}
void Port_ExtraSet(int count, int index, int chara, int team, int level) {
    if (index < 0 || index >= EXTRA_MAX) {
        return;
    }
    Port_SettingSave("extra_fighters", count);
    Port_ExtraSetFighter(index, chara, team, level);
    Port_SettingsWrite();
}

/* 1 when the three files a fight reads for character `chara` are in the data folder and not empty. `why`: the
   first one that is not, for the window and the log. */
int Port_ExtraCharaOk(int chara, char *why, int size) {
    static const int kFiles[3] = {0, 8, 9};
    char path[1024];
    int i;

    if (chara < 0 || chara >= CHARA_COUNT) {
        snprintf(why, (size_t)size, "there is no character %d (0..%d)", chara, CHARA_COUNT - 1);
        return 0;
    }
    for (i = 0; i < 3; i++) {
        int id = chara * 10 + FILE_CHARA + kFiles[i]; /* (ids from 1 are the second archive's files, from 0) */
        FILE *fp = Port_FilePath(1, id - 1, NULL, path, sizeof(path)) ? fopen(path, "rb") : NULL;
        long bytes = 0;
        if (fp != NULL) {
            fseek(fp, 0, SEEK_END);
            bytes = ftell(fp);
            fclose(fp);
        }
        if (bytes <= 0) {
            snprintf(why, (size_t)size, "its %s file (%05d.bin) is %s the game data", i == 0 ? "model" : "animation", id - 1,
                     fp != NULL ? "empty in" : "not in");
            return 0;
        }
    }
    return 1;
}

/* One more extra fighter for the fight being set up. 0: not taken (the table is full, there is no such character,
   its files are missing: the reason is logged, and kept for the window). */
static int fight_add(int chara, int costume, int team, int level, int member) {
    char why[160];
    FightExtra *f;

    if (sFightCount >= PORT_EXTRA_MAX) {
        fprintf(stderr, "bt3: extra fighter: character %d is left out (a fight has %d fighters at most)\n", chara, PORT_FIGHTER_MAX);
        return 0;
    }
    if (!Port_ExtraCharaOk(chara, why, sizeof(why))) {
        fprintf(stderr, "bt3: extra fighter: character %d is left out (%s)\n", chara, why);
        sLast = EXTRA_FILES;
        return 0;
    }
    f = &sFight[sFightCount++];
    f->chara = chara;
    f->costume = costume < 0 ? 0 : costume;
    f->team = team < 0 ? 0 : team >= PORT_TEAM_MAX ? PORT_TEAM_MAX - 1 : team;
    f->level = level > 29 ? 29 : level;
    f->member = member;
    return 1;
}

/* BT3_EXTRA_FIGHTERS=<character>:<team>[:<level>],... (the tests): any number of extra fighters, in any fight. */
static void fight_from_list(const char *p) {
    while (*p != '\0') {
        char *end;
        long chara = strtol(p, &end, 10), team = 1, level = -1;

        if (end == p) {
            break;
        }
        if (*end == ':') {
            team = strtol(end + 1, &end, 10);
            if (*end == ':') {
                level = strtol(end + 1, &end, 10);
            }
        }
        fight_add((int)chara, 0, (int)team, (int)level, -1);
        p = end;
        while (*p == ',' || *p == ' ') {
            p++;
        }
    }
}

/* The mode the fights of now are of: the page's or the menu flow's setting, or BT3_MODES_FIGHT in a test (which
   then holds for every kind of fight: `forced`). */
static int fight_mode(int *forced) {
    const char *e = getenv("BT3_MODES_FIGHT");

    *forced = e != NULL;
    if (e != NULL) {
        int m = atoi(e);
        return m > PORT_MODE_NONE && m < PORT_MODE_COUNT ? m : PORT_MODE_NONE;
    }
    return Port_ModesFight();
}

static void fight_log(void) {
    int i;

    for (i = 0; i < sFightCount; i++) {
        fprintf(stderr, "bt3: extra fighter: character %d, team %d, level %d (%s)\n", sFight[i].chara, sFight[i].team, sFight[i].level,
                sFightFrom);
    }
    if (sFightCount > 1) {
        fprintf(stderr, "bt3: extra fighters: %d, so %d in the fight\n", sFightCount, sFightCount + 2);
    }
}

/* A fight is being loaded: does it have extra fighters? Called by the loader (BtlLoad_StepInitial) with the
   battle's mode (include/battle/battle_setup.h: 0 the versus menu's, 6 training), each side's number of members,
   and whether the picture is split, a replay is played, the fight has a script.
   Returns 0 when the fight's extra fighters are decided (none, or the table is filled), or PORT_MODE_TEAM /
   PORT_MODE_FFA when they are the sides' own members: the loader then hands those over with Port_ExtraAdd and
   ends with Port_ExtraDone. */
int Port_ExtraSetup(int mode, int members0, int members1, int split, int replay, int script) {
    static const char *const kWhy[] = {"", "", "this mode's fights are left as the game's own", "a side has more than one member",
                                       "the picture is split for two players", "a replay is being played",
                                       "the fight has a script", "an online match", ""};
    const char *list = getenv("BT3_EXTRA_FIGHTERS"), *env = getenv("BT3_THIRD_FIGHTER"), *e;
    int asked = 0, last = EXTRA_NONE, forced = 0, i;
    int page = fight_mode(&forced); /* the mode asked on the page of extra modes or by the menu flow, 0: none */
    int roster = page == PORT_MODE_TEAM || page == PORT_MODE_FFA;

    sFightCount = 0;
    sFightRoster = 0;
    sFightFrom = "";
    sLast = EXTRA_NONE;
    if (Port_NetActive()) {
        /* (nothing of this in an online match, whoever asks) */
        if (list != NULL || env != NULL || (roster ? members0 + members1 > 2 : page != PORT_MODE_NONE ? Port_ModesCount() > 0 : Port_ExtraCount() > 0)) {
            sLast = EXTRA_ONLINE;
            fprintf(stderr, "bt3: extra fighter: not in this fight (%s)\n", kWhy[EXTRA_ONLINE]);
        }
        return 0;
    }
    if (list != NULL) {
        sFightFrom = "BT3_EXTRA_FIGHTERS";
        fight_from_list(list);
        asked = 1;
    } else if (env != NULL) {
        int team = 1, level = -1;

        sFightFrom = "BT3_THIRD_FIGHTER";
        if ((e = getenv("BT3_THIRD_TEAM")) != NULL) {
            team = atoi(e) < 0 || atoi(e) > 2 ? 1 : atoi(e);
        }
        if ((e = getenv("BT3_THIRD_LEVEL")) != NULL) {
            level = atoi(e);
        }
        if (atoi(env) >= 0) {
            fight_add(atoi(env), 0, team, level, -1);
            asked = 1;
        }
    } else if (roster) {
        /* The sides' own members, all at once. (A fight of one member a side is the game's own fight of two.) */
        if (members0 + members1 > 2) {
            asked = 1;
            if (!forced && mode != 0) {
                last = EXTRA_MODE;
            } else if (script) {
                last = EXTRA_SCRIPT;
            } else if (split) {
                last = EXTRA_SPLIT;
            } else if (replay) {
                last = EXTRA_REPLAY;
            } else {
                sFightRoster = page;
                sFightFrom = Port_ModesName(page);
                return page;
            }
        }
    } else if (page != PORT_MODE_NONE ? Port_ModesCount() > 0 : Port_ExtraCount() > 0) {
        int count = page != PORT_MODE_NONE ? Port_ModesCount() : Port_ExtraCount();

        asked = 1;
        if (!forced && (page != PORT_MODE_NONE ? mode != 6 : mode != 0 && mode != 6)) {
            last = EXTRA_MODE; /* (the page's choice is for the kind of fight it led into, and no other) */
        } else if (script) {
            last = EXTRA_SCRIPT;
        } else if (members0 != 1 || members1 != 1) {
            last = EXTRA_TEAMS;
        } else if (split) {
            last = EXTRA_SPLIT;
        } else if (replay) {
            last = EXTRA_REPLAY;
        } else {
            sFightFrom = page != PORT_MODE_NONE ? "the modes page: Training" : "the settings";
            for (i = 0; i < count; i++) {
                int team = Port_ExtraTeam(i);
                /* (a team of its own: the first number no player and no extra fighter before it has) */
                fight_add(Port_ExtraChara(i), 0, team < 0 ? 2 + i : team, Port_ExtraLevel(i), -1);
            }
        }
    }
    if (sFightCount > 0) {
        last = EXTRA_IN;
        fight_log();
    } else if (asked) {
        if (sLast == EXTRA_FILES) {
            last = EXTRA_FILES;
        } else {
            fprintf(stderr, "bt3: extra fighter: not in this fight (%s)\n", kWhy[last]);
        }
    }
    sLast = last;
    if (getenv("BT3_EXTRA_FLIP") != NULL) {
        /* (the experiment's tool) Testing without a person at the window: the switch is turned over after every
           set-up, as if the player had done it during the fight, so a run of several fights has them with and
           without in turn. */
        Port_SettingSave("extra_fighters", Port_ExtraCount() > 0 ? 0 : 1);
    }
    return 0;
}

/* The loader hands over a member of a side's team (Port_ExtraSetup returned a roster mode): `side` and `index`
   say which. Its team is its side in a Team Battle and its own in a Free-for-All; its level is the member's, where the member has one (a player's own team has none: -1, and the
   battle code gives it the other side's). Returns 1 if it is in the fight. */
int Port_ExtraAdd(int side, int index, int chara, int costume, int level) {
    if (sFightRoster == 0) {
        return 0;
    }
    return fight_add(chara, costume, sFightRoster == PORT_MODE_FFA ? 2 + sFightCount : side != 0, level < 0 ? -1 : level,
                     side * 8 + index);
}
void Port_ExtraDone(void) {
    if (sFightCount > 0) {
        sLast = EXTRA_IN;
        fight_log();
    }
}

/* What the last fight's set-up decided (EXTRA_*), for the window. */
int Port_ExtraLast(void) {
    return sLast;
}

/* The battle code's questions, answered for the fight being loaded or fought. */
int Port_FightExtras(void) {
    return sFightCount;
}
int Port_FightChara(int k) {
    return k >= 0 && k < sFightCount ? sFight[k].chara : -1;
}
int Port_FightCostume(int k) {
    return k >= 0 && k < sFightCount ? sFight[k].costume : 0;
}
int Port_FightTeam(int k) {
    return k >= 0 && k < sFightCount ? sFight[k].team : 1;
}
int Port_FightLevel(int k) { /* -1: its side's own */
    return k >= 0 && k < sFightCount ? sFight[k].level : -1;
}
int Port_FightMember(int k) {
    return k >= 0 && k < sFightCount ? sFight[k].member : -1;
}
int Port_FightRoster(void) {
    return sFightCount > 0 ? sFightRoster : 0;
}
/* Extra k's controller: 0..3 (a player plays it), -1 the computer. For now from BT3_HUMAN_PADS=<fighter>:<pad>,...
   (fighter numbers 2..9 as in the logs, controllers 1..4), e.g. 2:3,6:4: fighter 2 on controller 3, fighter 6 on
   controller 4. */
int Port_FightPad(int k) {
    const char *p = getenv("BT3_HUMAN_PADS");

    if (k < 0 || k >= sFightCount || p == NULL) {
        return -1;
    }
    while (*p != '\0') {
        int f = atoi(p), pad = -1;
        const char *c = strchr(p, ':');
        if (c != NULL) {
            pad = atoi(c + 1) - 1;
        }
        if (f == k + 2 && pad >= 0 && pad < 4) {
            return pad;
        }
        p = strchr(p, ',');
        if (p == NULL) {
            break;
        }
        p++;
    }
    return -1;
}

/* (more than two fighters) A fighter played from a controller, said once a fight when it is set up. */
void Port_PlayerLog(int fighter, int pad, int cpu) {
    if (sFightCount > 0 && !cpu) {
        fprintf(stderr, "bt3: fighter %d is played on controller %d\n", fighter, pad + 1);
    }
}

/* A fighter of this fight is on controller 3 or 4 (src/sys/pad.c reads those then). */
int Port_ExtraPadsOn(void) {
    int k;

    for (k = 0; k < sFightCount; k++) {
        if (Port_FightPad(k) >= 2) {
            return 1;
        }
    }
    return 0;
}

/* (extra 0 by its older name) */
int Port_ThirdFighter(void) {
    return Port_FightChara(0);
}
int Port_ThirdLevel(void) {
    return Port_FightLevel(0);
}
int Port_ThirdTeam(void) {
    return Port_FightTeam(0);
}

/* BT3_DEMO_MODE=<battle mode> (the tests): the demo fight is given that mode when it loads; -1: not set. */
int Port_TestDemoMode(void) {
    const char *e = getenv("BT3_DEMO_MODE");

    return e != NULL && atoi(e) >= 0 && atoi(e) <= 9 ? atoi(e) : -1;
}

/* BT3_TEAM_ROSTER=<character>,<character>,.../<character>,... (the tests): the members of the two sides' teams,
   as the team select would have left them, for a fight that was set up without it (the demo fight). Member
   `index` of `side`, -1 past the last or when the variable is not set. */
int Port_TestRoster(int side, int index) {
    const char *p = getenv("BT3_TEAM_ROSTER");
    int i;

    if (p == NULL || side < 0 || side > 1 || index < 0 || index >= 5) {
        return -1;
    }
    if (side == 1) {
        p = strchr(p, '/');
        if (p == NULL) {
            return -1;
        }
        p++;
    }
    for (i = 0; i < index; i++) {
        while (*p != ',' && *p != '/' && *p != '\0') {
            p++;
        }
        if (*p != ',') {
            return -1;
        }
        p++;
    }
    if (*p < '0' || *p > '9') {
        return -1;
    }
    i = atoi(p);
    return i >= 0 && i < CHARA_COUNT ? i : -1;
}

/* ---- the characters' names -------------------------------------------------------------------------------------- */

/* The versus select shows a character's name and form from two texts of its screen (src/menu/menu_c_e.c: sections
   43 and 44 of member 2 of the first menu archive, the file gProgress->baseFile). The same file is read here, by the
   window's thread, from the data folder: every offset in it is checked against the buffer it is in. */
#if defined(__x86_64__) && defined(__clang__)
#undef __ptr32 /* the Windows headers of mingw define these two away */
#undef __uptr
#define GAME_PTR *__ptr32 __uptr /* (the game's pointers are 32 bits wide in the 64-bit program) */
#else
#define GAME_PTR *
#endif
extern struct { int unk0, baseFile; } GAME_PTR gProgress;

#define NAMES_FILE_DEFAULT 449 /* gProgress->baseFile on the USA disc: pzs3us1/00448.bin */
#define NAMES_MEMBER 2
#define NAMES_UNPACKED_MAX (64u * 1024 * 1024)

static char sNames[CHARA_COUNT][72];
static int sNamesState; /* 0 not tried, 1 read, -1 could not be */

static int get32(const uint8_t *p, size_t n, size_t at, uint32_t *v) {
    if (at > n || n - at < 4) {
        return 0;
    }
    *v = (uint32_t)p[at] | (uint32_t)p[at + 1] << 8 | (uint32_t)p[at + 2] << 16 | (uint32_t)p[at + 3] << 24;
    return 1;
}

/* Line `line` of a text section (a table of offsets, then UTF-16 strings that start with a byte order mark). */
static int text_line(const uint8_t *p, size_t n, int line, char *out, int cap) {
    uint16_t str[96];
    uint32_t first, off;
    size_t i;

    out[0] = '\0';
    if (!get32(p, n, 4, &first) || (uint32_t)line + 1 >= first >> 2 || !get32(p, n, ((size_t)line + 1) * 4, &off)) {
        return 0;
    }
    off &= ~3u;
    for (i = 0; i < sizeof(str) / sizeof(str[0]) - 1; i++) {
        if (off + i * 2 + 2 > n) {
            return 0;
        }
        str[i] = (uint16_t)(p[off + i * 2] | p[off + i * 2 + 1] << 8);
        if (str[i] == 0) {
            break;
        }
    }
    str[i] = 0;
    if (off == 0 || str[0] != 0xFEFF) {
        return 0;
    }
    return PortA11y_Utf8(out, cap, str);
}

static void names_read(void) {
    char path[1024], form[64];
    uint8_t *file = NULL, *res = NULL;
    uint32_t member, size, packed, name, formAt;
    long bytes = 0;
    FILE *fp;
    int id = gProgress != NULL ? gProgress->baseFile : 0, c;

    if (id < 1 || id >= 0xD48) { /* (before the game has set it: the USA disc's) */
        id = NAMES_FILE_DEFAULT;
    }
    if (!Port_FilePath(1, id - 1, NULL, path, sizeof(path)) || (fp = fopen(path, "rb")) == NULL) {
        return; /* (the archives are not known yet at the very start: asked again later) */
    }
    sNamesState = -1;
    fseek(fp, 0, SEEK_END);
    bytes = ftell(fp);
    fseek(fp, 0, SEEK_SET);
    if (bytes > 0 && bytes < (long)NAMES_UNPACKED_MAX && (file = malloc((size_t)bytes)) != NULL &&
        fread(file, 1, (size_t)bytes, fp) == (size_t)bytes && get32(file, (size_t)bytes, NAMES_MEMBER * 4, &member) &&
        (member &= ~3u) < (uint32_t)bytes && get32(file, (size_t)bytes, member, &size) && get32(file, (size_t)bytes, member + 4, &packed) &&
        size > 0 && size <= NAMES_UNPACKED_MAX && (res = malloc(size)) != NULL &&
        MenuLabels_Unpack(file + member, (size_t)bytes - member, res, size) == (long)size && get32(res, size, 43 * 4, &name) &&
        get32(res, size, 44 * 4, &formAt) && (name &= ~3u) < size && (formAt &= ~3u) < size) {
        for (c = 0; c < CHARA_COUNT; c++) {
            if (text_line(res + name, size - name, c, sNames[c], 40) > 0) {
                /* as the select shows it: the form after the name when it says something else */
                if (text_line(res + formAt, size - formAt, c, form, 24) > 0 && strcmp(form, sNames[c]) != 0) {
                    size_t len = strlen(sNames[c]);
                    snprintf(sNames[c] + len, sizeof(sNames[c]) - len, ", %s", form);
                }
                sNamesState = 1;
            }
        }
    }
    fclose(fp);
    free(file);
    free(res);
    if (sNamesState < 0) {
        fprintf(stderr, "bt3: extra fighter: the characters' names could not be read from %s (numbers are shown)\n", path);
    }
}

/* The name of a character as the versus select shows it, or "" when the data did not give one. For the settings
   window (its thread only). */
const char *Port_CharaName(int chara) {
    static int tries;

    if (sNamesState == 0 && tries++ % 60 == 0) {
        names_read();
    }
    return chara >= 0 && chara < CHARA_COUNT ? sNames[chara] : "";
}
