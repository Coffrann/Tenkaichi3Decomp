/*
 * Extra fighters (the third-fighter experiment): who they are, and whether a fight has them.
 *
 * The battle code (src/battle: battle_load.c, battle.c, battle_work.c, btl_ai_cond.c, btl_char_flag_opp.c) asks
 * three things: Port_ThirdFighter() (a character, or -1: no third fighter), Port_ThirdTeam() and
 * Port_ThirdLevel(). The answers are decided ONCE PER FIGHT, by Port_ExtraSetup, which the loader calls just before
 * it asks for the fighters' models: a setting changed in the middle of a fight changes the next one, and a fight
 * never sees two answers.
 *
 * Where the answers come from:
 *   - the environment (the tests): BT3_THIRD_FIGHTER=<character 0..160>, BT3_THIRD_TEAM (1 with player 2, the
 *     default; 0 with player 1; 2 on its own), BT3_THIRD_LEVEL (0..29). Each one that is set wins over its setting,
 *     and BT3_THIRD_FIGHTER applies to every kind of fight, as it always did;
 *   - else the settings (the Experiments tab of the settings window, port/src/gs/ui.cpp):
 *         extra_fighters   how many extra fighters (0 or 1 today)
 *         extra0_chara     the first one's character
 *         extra0_team      its team: 0 player 1's, 1 player 2's, -1 a team of its own
 *         extra0_level     its computer level, 0..29
 *     The names carry an index and the count is a number because the aim is more than one (up to ten in a fight);
 *     only entry 0 is read so far, and docs/port/tagteam_menu.md says what the rest needs.
 *     From the settings a fight gets its extra fighter only when it is a plain one against one of Duel or of the
 *     training mode: no script, one member a side, one view, not a replay. Everything else is the game's own.
 *   - or the page of extra modes (plat_modes.c: SELECT on the game's main menu). Its Start writes the same
 *     extraN_* rows and two settings of its own, modes_fight (which mode) and modes_extras (how many), which stand
 *     until the main menu is back. While they stand a fight has the extra fighters under the same rules as above,
 *     whatever the Experiments tab's switch says, and only in the kind of fight the mode is (the versus menu's, or
 *     training's).
 *
 * Whatever asked, there is none in an online match (the two copies must compute the game's own fight), and none
 * when the character's files are not in the data folder (the loader would wait for them for ever or read nothing).
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
static int sChara = -1, sTeam = 1, sLevel = -1;
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

/* A fight is being loaded: does it have an extra fighter? Called by the loader (BtlLoad_StepInitial) with the
   battle's mode (include/battle/battle_setup.h: 0 the versus menu's, 6 training), each side's number of members,
   and whether the picture is split, a replay is played, the fight has a script. */
void Port_ExtraSetup(int mode, int members0, int members1, int split, int replay, int script) {
    const char *env = getenv("BT3_THIRD_FIGHTER"), *e;
    char why[160];
    int asked = 0, last = EXTRA_NONE;
    int page = Port_ModesFight(); /* the mode asked on the page of extra modes, 0: none */

    sChara = -1;
    sTeam = 1;
    sLevel = -1; /* (the side's own level, src/battle/btl_ai_cond.c) */
    why[0] = '\0';
    if (env != NULL) {
        sChara = atoi(env);
        asked = sChara >= 0;
        if (sChara >= CHARA_COUNT) {
            sChara = -1;
        }
    } else if (page != PORT_MODE_NONE ? Port_ModesCount() > 0 : Port_ExtraCount() > 0) {
        asked = 1;
        if (Port_NetActive()) {
            last = EXTRA_ONLINE;
        } else if (page != PORT_MODE_NONE ? mode != (page == PORT_MODE_TRAINING ? 6 : 0) : mode != 0 && mode != 6) {
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
            int team = Port_ExtraTeam(0);
            sChara = Port_ExtraChara(0);
            sTeam = team < 0 ? 2 : team; /* (a team of its own: the first number no player has) */
            sLevel = Port_ExtraLevel(0);
        }
    }
    if ((e = getenv("BT3_THIRD_TEAM")) != NULL) {
        sTeam = atoi(e) < 0 || atoi(e) > 2 ? 1 : atoi(e);
    }
    if ((e = getenv("BT3_THIRD_LEVEL")) != NULL) {
        sLevel = atoi(e) > 29 ? 29 : atoi(e);
    }
    if (sChara >= 0 && Port_NetActive()) {
        sChara = -1;
        last = EXTRA_ONLINE;
    }
    if (sChara >= 0 && !Port_ExtraCharaOk(sChara, why, sizeof(why))) {
        sChara = -1;
        last = EXTRA_FILES;
    }
    if (sChara >= 0) {
        last = EXTRA_IN;
        fprintf(stderr, "bt3: extra fighter: character %d, team %d, level %d (%s%s)\n", sChara, sTeam, sLevel,
                env != NULL ? "BT3_THIRD_FIGHTER" : page != PORT_MODE_NONE ? "the modes page: " : "the settings",
                env == NULL && page != PORT_MODE_NONE ? Port_ModesName(page) : "");
    } else if (asked) {
        static const char *const kWhy[] = {"", "", "this mode's fights are left as the game's own", "a side has more than one member",
                                           "the picture is split for two players", "a replay is being played",
                                           "the fight has a script", "an online match", ""};
        fprintf(stderr, "bt3: extra fighter: not in this fight (%s)\n", last == EXTRA_FILES ? why : kWhy[last]);
    }
    sLast = last;
    if (getenv("BT3_EXTRA_FLIP") != NULL) {
        /* (the experiment's tool) Testing without a person at the window: the switch is turned over after every
           set-up, as if the player had done it during the fight, so a run of several fights has them with and
           without in turn. */
        Port_SettingSave("extra_fighters", Port_ExtraCount() > 0 ? 0 : 1);
    }
}

/* What the last fight's set-up decided (EXTRA_*), for the window. */
int Port_ExtraLast(void) {
    return sLast;
}

/* The battle code's three questions, answered for the fight being loaded or fought. */
int Port_ThirdFighter(void) {
    return sChara;
}
int Port_ThirdLevel(void) { /* -1: its side's own */
    return sLevel;
}
int Port_ThirdTeam(void) { /* 0 with player 1, 1 with player 2, 2 on its own */
    return sTeam;
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
