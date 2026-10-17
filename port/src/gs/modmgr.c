/*
 * The Mod Manager's library (main menu, "Mod Manager": src/menu/port_modmgr.c, port_modmgr2.c, port_mmkit.c draw it):
 * the character packages of `characters` (port/src/plat_charas.c, docs/port/character_packages.md) as the manager's
 * list. Nothing of its own is kept on disk: a package is its folder and its character.txt, which is on or off and in
 * which order is load_order.txt (PortCharas_OrderWrite), and every change is read back from there
 * (PortCharas_Reload). The menu's code calls these with the game's pointers; the records it gets are plain
 * (include/menu/port_mods.h).
 *
 * A "mod" of the manager is a folder directly in `characters`: one that replaces a disc character (replaces=) or adds
 * one (a "new fighter", on the Mod Fighters grid, in the order the added characters take their ids). Its model on
 * the manager's platform is the package's files answering for the disc character it replaces or stands on
 * (PortCharas_Preview), on or not.
 *
 * Online (a session) none of it is there: the package reader answers as if there were no folder.
 */
#include <SDL3/SDL.h>
#include <ctype.h>
#include <dirent.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#ifdef _WIN32
#include <windows.h>
#include <direct.h>
#define mkdir_one(p) _mkdir(p)
#else
#define mkdir_one(p) mkdir(p, 0755)
#endif

typedef int32_t s32;
typedef uint16_t u16;
typedef uint32_t u32;
#include "../../../include/menu/port_mods.h"
#include "../../include/plat_charas.h"

extern int Port_NetSession(void);                /* gs/net.c */
extern char *PortTbo_FighterName(s32 chara);     /* src/menu/port_tbo.c */
extern const char *Port_CharaName(int index);    /* plat_charas.c: an added character's name */
extern void PortTbo_TextMode(int on);            /* gs_input.c */
extern int PortTbo_TextRead(void);
extern int PortInput_DropTake(char *out, unsigned size);

#define LIB_MAX 256
#define DISC 161

typedef struct {
    char folder[64];
    PortCharaDesc d;
    int on;
    int place;   /* its place in the order of use (load order), 0 first */
} Pkg;

static Pkg sLib[LIB_MAX];
static int sLibCount;
static int sLibRead;
static long sStamp;
static char sBrowse[700];     /* the folder the browser looked at (Port_DirLook) */
static int sBrowseChara = -1;
static int sChipEdit[3];      /* Port_ModSet 3..5: the chip's square, written with the last of the three */

/* ---- the library ---------------------------------------------------------------------------------------------- */

static int name_cmp(const void *a, const void *b) {
    const Pkg *x = a, *y = b;
    return SDL_strcasecmp(x->folder, y->folder);
}

static long folder_stamp(void) {
    char root[300], path[420];
    struct stat st;
    long stamp = 0;
    DIR *d;
    struct dirent *e;
    if (!PortCharas_Root(root, sizeof(root)) || (d = opendir(root)) == NULL) {
        return 0;
    }
    while ((e = readdir(d)) != NULL) {
        if (e->d_name[0] == '.') {
            continue;
        }
        snprintf(path, sizeof(path), "%s/%s", root, e->d_name);
        if (stat(path, &st) == 0) {
            stamp = stamp * 31 + (long)st.st_mtime + (long)st.st_size;
        }
        stamp++;
    }
    closedir(d);
    return stamp;
}

/* The folders and what each is: every folder directly in `characters` that is a package, by name. */
static void lib_read(void) {
    static char order[LIB_MAX][128];
    static unsigned char on[LIB_MAX];
    int n = PortCharas_OrderGet(order, on, LIB_MAX), i, k;

    sLibCount = 0;
    for (i = 0; i < n && sLibCount < LIB_MAX; i++) {
        Pkg *p = &sLib[sLibCount];
        if (strlen(order[i]) >= sizeof(p->folder) || !PortCharas_Describe(order[i], &p->d)) {
            continue;
        }
        snprintf(p->folder, sizeof(p->folder), "%s", order[i]);
        p->on = on[i];
        p->place = i;
        sLibCount++;
    }
    qsort(sLib, (size_t)sLibCount, sizeof(Pkg), name_cmp);
    for (k = 0; k < 3; k++) {
        sChipEdit[k] = 0;
    }
    sStamp = folder_stamp();
    sLibRead = 1;
}

static void ensure(void) {
    if (!sLibRead) {
        lib_read();
    }
}

static int valid(int i) {
    ensure();
    return i >= 0 && i < sLibCount && !Port_NetSession();
}

/* Writes the order of use with folder `at` moved to `to` (0 first; -1 where it is) and switched `on` (-1 as it is),
   then reads the packages again. */
static void order_put(const char *folder, int to, int on) {
    static char order[LIB_MAX][128];
    static unsigned char ons[LIB_MAX];
    int n = PortCharas_OrderGet(order, ons, LIB_MAX), i, from = -1;
    char keep[128];
    unsigned char keepOn;

    for (i = 0; i < n; i++) {
        if (strcmp(order[i], folder) == 0) {
            from = i;
        }
    }
    if (from < 0) {
        return;
    }
    if (on >= 0) {
        ons[from] = (unsigned char)(on != 0);
    }
    if (to >= 0 && to < n && to != from) {
        snprintf(keep, sizeof(keep), "%s", order[from]);
        keepOn = ons[from];
        if (to < from) {
            memmove(order[to + 1], order[to], (size_t)(from - to) * sizeof(order[0]));
            memmove(ons + to + 1, ons + to, (size_t)(from - to));
        } else {
            memmove(order[from], order[from + 1], (size_t)(to - from) * sizeof(order[0]));
            memmove(ons + from, ons + from + 1, (size_t)(to - from));
        }
        snprintf(order[to], sizeof(order[0]), "%s", keep);
        ons[to] = keepOn;
    }
    PortCharas_OrderWrite((const char(*)[128])order, ons, n);
}

/* After a change: the packages read again, then the list. */
static void changed(void) {
    PortCharas_Reload();
    lib_read();
}

static int chara_of(const Pkg *p) {
    int c = p->d.replaces >= 0 ? p->d.replaces : p->d.base;
    return c >= 0 && c < DISC ? c : 0;
}

/* Another package that is on and replaces the same disc character (only one of them is used: the first in the order). */
static int rival_of(int i) {
    int j, best = -1;
    if (sLib[i].d.replaces < 0) {
        return -1;
    }
    for (j = 0; j < sLibCount; j++) {
        if (j != i && sLib[j].on && sLib[j].d.replaces == sLib[i].d.replaces && (best < 0 || sLib[j].place < sLib[best].place)) {
            best = j;
        }
    }
    return best;
}

/* The added characters that are on, in the order they take their ids: the Mod Fighters grid. */
static int skins(int *out, int max) {
    int n = 0, i, j;
    for (i = 0; i < sLibCount && n < max; i++) {
        if (sLib[i].on && sLib[i].d.replaces < 0) {
            out[n++] = i;
        }
    }
    for (i = 1; i < n; i++) { /* (by place) */
        int v = out[i];
        for (j = i; j > 0 && sLib[out[j - 1]].place > sLib[v].place; j--) {
            out[j] = out[j - 1];
        }
        out[j] = v;
    }
    return n;
}

static int problem_of(const PortCharaDesc *d) {
    if (d->problem[0] == '\0') {
        return 0;
    }
    if (strstr(d->problem, "more than") != NULL) {
        return 2;
    }
    if (strstr(d->problem, "empty") != NULL) {
        return 3;
    }
    if (strstr(d->problem, "chip") != NULL) {
        return 4;
    }
    return 1;
}

void Port_ModRescan(void) {
    if (!Port_NetSession()) {
        PortCharas_Reload();
    }
    lib_read();
}

s32 Port_ModsChanged(void) {
    return sLibRead && folder_stamp() != sStamp;
}

s32 Port_ModCount(void) {
    ensure();
    return Port_NetSession() ? 0 : sLibCount;
}

void Port_ModGetInfo(s32 i, PortModInfo *out) {
    const Pkg *p;
    int list[LIB_MAX], n, k, onCount = 0, before = 0;
    char v[64];

    memset(out, 0, sizeof(*out));
    out->skin = out->rival = -1;
    out->chara = -1;
    if (!valid(i)) {
        return;
    }
    p = &sLib[i];
    snprintf(out->name, sizeof(out->name), "%s", p->folder);
    out->chara = chara_of(p);
    Port_ModCharaName(out->chara, out->target, sizeof(out->target));
    if (p->d.nameSet) {
        snprintf(out->white, sizeof(out->white), "%s", p->d.name);
    }
    if (p->d.labelSet) {
        snprintf(out->yellow, sizeof(out->yellow), "%s", p->d.label);
    }
    out->named = p->d.nameSet || p->d.labelSet;
    out->on = p->on;
    out->kinds = (p->d.files & 0x3FF) | ((p->d.files >> PORT_DESC_PORTRAIT) & 1) << 10;
    out->costumes = p->d.costumes;
    for (k = 0; k < 11; k++) {
        out->files += (out->kinds >> k) & 1;
    }
    out->large = p->d.large;
    out->isNew = p->d.replaces < 0;
    out->problem = problem_of(&p->d);
    out->charas = 1;
    out->charaList[0] = out->chara;
    out->art[3] = out->artH = 100;
    if (PortCharas_ManifestGet(p->folder, "chip_square", v, sizeof(v))) {
        sscanf(v, "%d,%d,%d", &out->chip[0], &out->chip[1], &out->chip[2]);
    }
    for (k = 0; k < sLibCount; k++) {
        onCount += sLib[k].on;
        before += sLib[k].on && sLib[k].place < p->place;
    }
    out->onCount = onCount;
    out->priority = p->on ? before + 1 : 0;
    out->rival = rival_of(i);
    out->conflict = p->on && out->rival >= 0;
    out->wins = !out->conflict || sLib[out->rival].place > p->place;
    n = skins(list, LIB_MAX);
    for (k = 0; k < n; k++) {
        if (list[k] == i) {
            out->skin = k;
        }
    }
}

/* Switches a package on or off. One switched on goes first in the order (it wins), and another that is on for the
   same disc character is switched off; that one is returned, -1 none. */
s32 Port_ModToggle(s32 i) {
    int rival, off = -1;
    char folder[64], other[64] = "";
    if (!valid(i)) {
        return -1;
    }
    snprintf(folder, sizeof(folder), "%s", sLib[i].folder);
    if (sLib[i].on) {
        order_put(folder, -1, 0);
    } else {
        rival = rival_of(i);
        if (rival >= 0) {
            snprintf(other, sizeof(other), "%s", sLib[rival].folder);
            order_put(other, -1, 0);
        }
        order_put(folder, 0, 1);
    }
    changed();
    for (i = 0; other[0] != '\0' && i < sLibCount; i++) {
        if (strcmp(sLib[i].folder, other) == 0) {
            off = i;
        }
    }
    return off;
}

/* -1: one place earlier in the order of those that are on (it wins over one more); +1 one later. */
void Port_ModPriority(s32 i, s32 dir) {
    int j, best = -1;
    if (!valid(i) || !sLib[i].on) {
        return;
    }
    for (j = 0; j < sLibCount; j++) { /* the next one that is on, before or after it */
        if (j == i || !sLib[j].on) {
            continue;
        }
        if (dir < 0 && sLib[j].place < sLib[i].place && (best < 0 || sLib[j].place > sLib[best].place)) {
            best = j;
        }
        if (dir > 0 && sLib[j].place > sLib[i].place && (best < 0 || sLib[j].place < sLib[best].place)) {
            best = j;
        }
    }
    if (best >= 0) {
        order_put(sLib[i].folder, sLib[best].place, -1);
        changed();
    }
}

/* The package whose files the platform shows (-1 none, -2 the folder the browser looked at), for the disc character
   it replaces or stands on. */
void Port_ModPreview(s32 i) {
    char root[300], path[400];
    if (i == -2 && sBrowse[0] != '\0') {
        PortCharas_Preview(sBrowse, sBrowseChara >= 0 ? sBrowseChara : 0);
        return;
    }
    if (!valid(i) || !PortCharas_Root(root, sizeof(root))) {
        PortCharas_Preview(NULL, -1);
        return;
    }
    snprintf(path, sizeof(path), "%s/%s", root, sLib[i].folder);
    PortCharas_Preview(path, chara_of(&sLib[i]));
}

/* 0: a new fighter (value 1) or a replacement (0); 1: the disc character a new one stands on; 2: the one it
   replaces; 3..5: the chip's square (x, y, size; 0 0 0: the usual one), written with 5. */
void Port_ModSet(s32 i, s32 field, s32 value) {
    char num[48];
    Pkg *p;
    if (!valid(i)) {
        return;
    }
    p = &sLib[i];
    switch (field) {
    case 0:
        snprintf(num, sizeof(num), "%d", chara_of(p));
        if (value) {
            PortCharas_ManifestSet(p->folder, "replaces", NULL);
            PortCharas_ManifestSet(p->folder, "base", num);
        } else {
            PortCharas_ManifestSet(p->folder, "replaces", num);
        }
        break;
    case 1:
    case 2:
        if (value < 0 || value >= DISC) {
            return;
        }
        snprintf(num, sizeof(num), "%d", value);
        PortCharas_ManifestSet(p->folder, field == 1 ? "base" : "replaces", num);
        break;
    case 3:
    case 4:
    case 5:
        sChipEdit[field - 3] = value;
        if (field != 5) {
            return;
        }
        if (sChipEdit[2] <= 0) {
            PortCharas_ManifestSet(p->folder, "chip_square", NULL);
        } else {
            snprintf(num, sizeof(num), "%d,%d,%d", sChipEdit[0], sChipEdit[1], sChipEdit[2]);
            PortCharas_ManifestSet(p->folder, "chip_square", num);
        }
        break;
    default:
        return;
    }
    changed();
}

/* 0 the name (white), 1 the form's label (yellow), 2 author, 3 version, 4 the description. */
void Port_ModSetText(s32 i, s32 which, char *str) {
    static const char *const kKey[5] = {"name", "form", "author", "version", "about"};
    if (!valid(i) || which < 0 || which > 4 || str == NULL) {
        return;
    }
    PortCharas_ManifestSet(sLib[i].folder, kKey[which], str[0] != '\0' || which == 1 ? str : NULL);
    changed();
}

/* author (32 bytes), version (16), description (200) */
void Port_ModAbout(s32 i, char *out) {
    memset(out, 0, 248);
    if (!valid(i)) {
        return;
    }
    PortCharas_ManifestGet(sLib[i].folder, "author", out, 32);
    PortCharas_ManifestGet(sLib[i].folder, "version", out + 32, 16);
    PortCharas_ManifestGet(sLib[i].folder, "about", out + 48, 200);
}

/* ---- removed packages: moved to characters_removed beside `characters`, never deleted ------------------------- */

static int removed_dir(char *out, size_t size) {
    char root[300];
    char *slash;
    if (!PortCharas_Root(root, sizeof(root))) {
        return 0;
    }
    slash = strrchr(root, '/');
    if (slash != NULL) {
        snprintf(out, size, "%.*s/characters_removed", (int)(slash - root), root);
    } else {
        snprintf(out, size, "characters_removed");
    }
    return 1;
}

static int exists(const char *path) {
    struct stat st;
    return stat(path, &st) == 0;
}

s32 Port_ModRemove(s32 i) {
    char root[300], gone[320], from[400], to[420];
    int n;
    if (!valid(i) || !PortCharas_Root(root, sizeof(root)) || !removed_dir(gone, sizeof(gone))) {
        return 0;
    }
    mkdir_one(gone);
    snprintf(from, sizeof(from), "%s/%s", root, sLib[i].folder);
    snprintf(to, sizeof(to), "%s/%s", gone, sLib[i].folder);
    for (n = 2; exists(to) && n < 100; n++) {
        snprintf(to, sizeof(to), "%s/%s (%d)", gone, sLib[i].folder, n);
    }
    if (exists(to) || rename(from, to) != 0) {
        return 0;
    }
    changed();
    return 1;
}

static char sGone[LIB_MAX][64];
static int sGoneCount;

static int gone_cmp(const void *a, const void *b) {
    return SDL_strcasecmp(a, b);
}

s32 Port_RemovedList(void) {
    char gone[320], path[420];
    DIR *d;
    struct dirent *e;
    struct stat st;
    sGoneCount = 0;
    if (!removed_dir(gone, sizeof(gone)) || (d = opendir(gone)) == NULL) {
        return 0;
    }
    while ((e = readdir(d)) != NULL && sGoneCount < LIB_MAX) {
        snprintf(path, sizeof(path), "%s/%s", gone, e->d_name);
        if (e->d_name[0] != '.' && strlen(e->d_name) < 64 && stat(path, &st) == 0 && S_ISDIR(st.st_mode)) {
            snprintf(sGone[sGoneCount++], 64, "%s", e->d_name);
        }
    }
    closedir(d);
    qsort(sGone, (size_t)sGoneCount, 64, gone_cmp);
    return sGoneCount;
}

void Port_RemovedName(s32 i, char *out) {
    snprintf(out, 64, "%s", i >= 0 && i < sGoneCount ? sGone[i] : "");
}

/* Back into `characters` (switched off); its place in the list, -1 not done. */
s32 Port_RemovedRestore(s32 i) {
    char root[300], gone[320], from[420], to[400], name[64];
    int k;
    if (i < 0 || i >= sGoneCount || !PortCharas_Root(root, sizeof(root)) || !removed_dir(gone, sizeof(gone))) {
        return -1;
    }
    snprintf(name, sizeof(name), "%s", sGone[i]);
    snprintf(from, sizeof(from), "%s/%s", gone, name);
    snprintf(to, sizeof(to), "%s/%s", root, name);
    if (exists(to) || rename(from, to) != 0) {
        return -1;
    }
    order_put(name, -1, 0); /* (it may not be in the list: then it comes back on, as any new folder) */
    changed();
    Port_RemovedList();
    for (k = 0; k < sLibCount; k++) {
        if (strcmp(sLib[k].folder, name) == 0) {
            return k;
        }
    }
    return -1;
}

/* The package's folder (-1 `characters`) in the file manager of the PC. */
void Port_ModShowFolder(s32 i) {
    char root[300], path[400], full[1100], url[1200];
    if (!PortCharas_Root(root, sizeof(root))) {
        return;
    }
    mkdir_one(root);
    if (i >= 0 && valid(i)) {
        snprintf(path, sizeof(path), "%s/%s", root, sLib[i].folder);
    } else {
        snprintf(path, sizeof(path), "%s", root);
    }
#ifdef _WIN32
    if (_fullpath(full, path, sizeof(full)) == NULL) {
        return;
    }
    for (char *c = full; *c != '\0'; c++) {
        if (*c == '\\') {
            *c = '/';
        }
    }
    snprintf(url, sizeof(url), "file:///%s", full);
#else
    if (realpath(path, full) == NULL) {
        return;
    }
    snprintf(url, sizeof(url), "file://%s", full);
#endif
    SDL_OpenURL(url);
}

/* ---- names, small things ------------------------------------------------------------------------------------- */

void Port_ModCharaName(s32 chara, char *out, s32 size) {
    const char *name = NULL;
    if (size <= 0) {
        return;
    }
    if (chara >= 0 && chara < DISC) {
        name = PortTbo_FighterName(chara);
    } else if (chara >= 0xC6) {
        name = Port_CharaName(chara - 0xC6);
    }
    snprintf(out, (size_t)size, "%s", name != NULL && name[0] != '\0' ? name : "?");
}

s32 Port_ReadFile(char *path, void *buf, s32 max) {
    FILE *f;
    long n;
    if (path == NULL || max <= 0 || (f = fopen(path, "rb")) == NULL) {
        return 0;
    }
    fseek(f, 0, SEEK_END);
    n = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (n <= 0 || n > max || fread(buf, 1, (size_t)n, f) != (size_t)n) {
        n = 0;
    }
    fclose(f);
    return (s32)n;
}

s32 Port_ModTestStage(void) {
    const char *v = getenv("BT3_TEST_STAGE");
    return v != NULL ? atoi(v) : 0;
}

void Port_TextMode(s32 on) {
    PortTbo_TextMode(on);
}

s32 Port_TextRead(void) {
    return PortTbo_TextRead();
}

s32 Port_DropTake(char *path) {
    return PortInput_DropTake(path, 700);
}

/* ---- the chip and the portrait (the package's own files; making them: Port_ModArt*) --------------------------- */

static long file_into(const char *path, void *out, long max) {
    FILE *f = fopen(path, "rb");
    long n = 0;
    if (f == NULL) {
        return 0;
    }
    fseek(f, 0, SEEK_END);
    n = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (n <= 0 || n > max || fread(out, 1, (size_t)n, f) != (size_t)n) {
        n = 0;
    }
    fclose(f);
    return n;
}

/* A package's chip.bin (one entry of the select's chip pack, 5504 bytes, as the reader took it); 0 without one. */
s32 Port_ModChip(s32 i, void *out, s32 max) {
    char root[300], path[420];
    if (!valid(i) || !(sLib[i].d.files >> PORT_DESC_CHIP & 1) || !PortCharas_Root(root, sizeof(root)) || max < 5504) {
        return 0;
    }
    snprintf(path, sizeof(path), "%s/%s/chip.bin", root, sLib[i].folder);
    return file_into(path, out, max) == 5504 ? 5504 : 0;
}

/* The package's portrait file (portrait.bin or *_img.cdbt, the game's packed form); 0 without one. */
s32 Port_ModPortrait(s32 i, void *out, s32 max) {
    char root[300], dir[420], path[600];
    DIR *d;
    struct dirent *e;
    long n = 0;
    if (!valid(i) || !(sLib[i].d.files >> PORT_DESC_PORTRAIT & 1) || !PortCharas_Root(root, sizeof(root))) {
        return 0;
    }
    snprintf(dir, sizeof(dir), "%s/%s", root, sLib[i].folder);
    if ((d = opendir(dir)) == NULL) {
        return 0;
    }
    while (n == 0 && (e = readdir(d)) != NULL) {
        size_t l = strlen(e->d_name);
        if (SDL_strcasecmp(e->d_name, "portrait.bin") == 0 || (l > 9 && SDL_strcasecmp(e->d_name + l - 9, "_img.cdbt") == 0)) {
            snprintf(path, sizeof(path), "%s/%s", dir, e->d_name);
            n = file_into(path, out, max);
        }
    }
    closedir(d);
    return (s32)n;
}

/* ---- the Mod Fighters grid: the added characters that are on ------------------------------------------------- */

s32 Port_SkinCount(void) {
    int list[LIB_MAX];
    ensure();
    return Port_NetSession() ? 0 : skins(list, LIB_MAX);
}

s32 Port_SkinMod(s32 skin) {
    int list[LIB_MAX], n;
    ensure();
    n = skins(list, LIB_MAX);
    return skin >= 0 && skin < n ? list[skin] : -1;
}

/* The grid's order is the order the added characters take their ids in: two of them change places in it. */
void Port_RosterMove(s32 skin, s32 dir) {
    int list[LIB_MAX], n;
    ensure();
    n = skins(list, LIB_MAX);
    if (skin < 0 || skin >= n || skin + dir < 0 || skin + dir >= n || dir == 0) {
        return;
    }
    order_put(sLib[list[skin]].folder, sLib[list[skin + dir]].place, -1);
    changed();
}

/* An added character's id: the game's own number for it (0xC6 on), -1 if it is not one that is on. */
s32 Port_ModFighterId(s32 i) {
    int k;
    ensure();
    for (k = 0; valid(i) && sLib[i].on && sLib[i].d.replaces < 0 && k < Port_CharaPkgCount(); k++) {
        const PortCharaPkg *p = Port_CharaPkg(k);
        if (p->kind == PORT_PKG_ADDS && strcmp(p->folder, sLib[i].folder) == 0) {
            return p->id;
        }
    }
    return -1;
}

/* (the costume codes of the patch's skins are not used: an added character has an id of its own) */
s32 Port_CostumeOf(s32 code) {
    return code >= 8 ? (code - 8) & 3 : code;
}
s32 Port_SkinFile(s32 chara, s32 code, s32 kind, s32 variant) {
    (void)code;
    (void)variant;
    return chara * 10 + 0x590 + kind;
}
s32 Port_CharaOfFile(s32 id) {
    return (id - 0x590) / 10;
}

/* ---- transformations and fusions (character.txt's links) ----------------------------------------------------- */

static void trans_clear(PortTrans *t) {
    int k, m;
    memset(t, 0, sizeof(*t));
    for (k = 0; k < 4; k++) {
        t->target[k] = t->assist[k] = -1;
        t->type[k] = k == 3 ? 1 : 0;
        t->effect[k] = k == 3 ? 1 : 2;
        t->cost[k] = k == 3 ? 0 : 1;
    }
    t->next = -1;
    for (k = 0; k < 3; k++) {
        t->fuse[k] = t->fuseLead[k] = -1;
        t->fuseType[k] = 1;
        t->fuseCost[k] = 3;
        for (m = 0; m < 4; m++) {
            t->partner[k][m] = -1;
        }
    }
}

void Port_TransGet(s32 who, PortTrans *out) {
    (void)who;
    trans_clear(out);
}

void Port_TransSet(s32 who, PortTrans *in) {
    (void)who;
    (void)in;
}

void Port_TransApplyFile(s32 modelFile, void *param) {
    (void)modelFile;
    (void)param;
}

/* ---- the moves of a package (its first costume's move list) -------------------------------------------------- */

s32 Port_ModMoves(s32 i, void *out, s32 max) {
    (void)i;
    (void)out;
    (void)max;
    return 0;
}

/* ---- pictures made in the manager (later) -------------------------------------------------------------------- */

s32 Port_ModArtBase(s32 i, void *out, s32 max) {
    (void)i;
    (void)out;
    (void)max;
    return 0;
}
s32 Port_ModArtSet2(s32 i, s32 x, s32 y, s32 zoom, s32 zoomY) {
    (void)i;
    (void)x;
    (void)y;
    (void)zoom;
    (void)zoomY;
    return 0;
}
s32 Port_ModArtSet(s32 i, s32 x, s32 y, s32 zoom) {
    return Port_ModArtSet2(i, x, y, zoom, zoom);
}
void Port_ModArtReset(s32 i) {
    (void)i;
}
s32 Port_ModArtSource(s32 i, char *path) {
    (void)i;
    (void)path;
    return 0;
}

/* ---- the browser: the PC's folders ---------------------------------------------------------------------------- */

typedef struct {
    char name[96];
    int isDir;
} DirEnt;

static DirEnt sDir[512], sShown[512];
static int sDirCount, sShownCount, sDirKind;

void Port_DirKind(s32 kind) {
    sDirKind = kind;
}

static int listed_file(const char *name) {
    static const char *const kMods[] = {".pak", ".unk", ".cdbt", ".dbt", ".bin", ".txt", ".zip", NULL};
    static const char *const kPics[] = {".png", ".bmp", ".jpg", ".jpeg", NULL};
    const char *const *ends = sDirKind == 1 ? kPics : kMods;
    size_t l = strlen(name);
    int k;
    for (k = 0; ends[k] != NULL; k++) {
        size_t e = strlen(ends[k]);
        if (l > e && SDL_strcasecmp(name + l - e, ends[k]) == 0) {
            return 1;
        }
    }
    return 0;
}

static int dir_cmp(const void *a, const void *b) {
    const DirEnt *x = a, *y = b;
    if (x->isDir != y->isDir) {
        return y->isDir - x->isDir;
    }
    return SDL_strcasecmp(x->name, y->name);
}

/* Lists a folder: its folders first, then the files of the kind asked for. "" lists the drives (Windows) or "/". */
s32 Port_DirOpen(char *path) {
    char full[1100];
    DIR *d;
    struct dirent *e;
    struct stat st;

    sDirCount = 0;
#ifdef _WIN32
    if (path == NULL || path[0] == '\0') {
        DWORD drives = GetLogicalDrives();
        int k;
        for (k = 0; k < 26 && sDirCount < 512; k++) {
            if (drives & (1u << k)) {
                snprintf(sDir[sDirCount].name, sizeof(sDir[0].name), "%c:", 'A' + k);
                sDir[sDirCount++].isDir = 1;
            }
        }
        memcpy(sShown, sDir, sizeof(DirEnt) * (size_t)sDirCount);
        return sShownCount = sDirCount;
    }
#endif
    if ((d = opendir(path != NULL && path[0] != '\0' ? path : "/")) == NULL) {
        sShownCount = 0;
        return 0;
    }
    while ((e = readdir(d)) != NULL && sDirCount < 512) {
        if (e->d_name[0] == '.' || strlen(e->d_name) >= sizeof(sDir[0].name)) {
            continue;
        }
        snprintf(full, sizeof(full), "%s/%s", path != NULL && path[0] != '\0' ? path : "", e->d_name);
        if (stat(full, &st) != 0) {
            continue;
        }
        if (S_ISDIR(st.st_mode) || (S_ISREG(st.st_mode) && listed_file(e->d_name))) {
            snprintf(sDir[sDirCount].name, sizeof(sDir[0].name), "%s", e->d_name);
            sDir[sDirCount++].isDir = S_ISDIR(st.st_mode);
        }
    }
    closedir(d);
    qsort(sDir, (size_t)sDirCount, sizeof(DirEnt), dir_cmp);
    memcpy(sShown, sDir, sizeof(DirEnt) * (size_t)sDirCount);
    return sShownCount = sDirCount;
}

/* Keeps the entries with `text` in their name (any case); the count. */
s32 Port_DirFilter(char *text) {
    int i;
    sShownCount = 0;
    for (i = 0; i < sDirCount; i++) {
        if (text == NULL || text[0] == '\0' || SDL_strcasestr(sDir[i].name, text) != NULL) {
            sShown[sShownCount++] = sDir[i];
        }
    }
    return sShownCount;
}

void Port_DirGet(s32 i, PortDirEntry *out) {
    memset(out, 0, sizeof(*out));
    out->chara = -1;
    if (i >= 0 && i < sShownCount) {
        snprintf(out->name, sizeof(out->name), "%s", sShown[i].name);
        out->isDir = sShown[i].isDir;
    }
}

/* What a folder the browser rests on is: 1 a package (its record into out), 0 not one. It can then be previewed
   (Port_ModPreview(-2)). */
s32 Port_DirLook(char *path, PortModInfo *out) {
    PortCharaDesc d;
    memset(out, 0, sizeof(*out));
    out->skin = out->rival = out->chara = -1;
    sBrowse[0] = '\0';
    if (path == NULL || !PortCharas_DescribePath(path, &d)) {
        return 0;
    }
    snprintf(sBrowse, sizeof(sBrowse), "%s", path);
    sBrowseChara = d.replaces >= 0 ? d.replaces : d.base;
    snprintf(out->name, sizeof(out->name), "%s", strrchr(path, '/') != NULL ? strrchr(path, '/') + 1 : path);
    out->chara = sBrowseChara;
    Port_ModCharaName(out->chara, out->target, sizeof(out->target));
    if (d.nameSet) {
        snprintf(out->white, sizeof(out->white), "%s", d.name);
    }
    out->kinds = (d.files & 0x3FF) | ((d.files >> PORT_DESC_PORTRAIT) & 1) << 10;
    out->costumes = d.costumes;
    out->isNew = d.replaces < 0;
    out->problem = problem_of(&d);
    out->charas = 1;
    out->charaList[0] = out->chara;
    return 1;
}

/* ---- adding packages (later: folders, archives) --------------------------------------------------------------- */

static int sImportDone = -1;

s32 Port_ModImportStart(char *path, s32 all) {
    (void)path;
    (void)all;
    sImportDone = 0;
    return 1;
}

s32 Port_ModImportPoll(s32 *progress) {
    if (progress != NULL) {
        progress[0] = progress[1] = progress[2] = 0;
    }
    if (sImportDone == 0) {
        sImportDone = -1;
        return 2;
    }
    return 0;
}

s32 Port_ModImportResult(void) {
    return 0;
}

s32 Port_ModImportInfo(s32 *skipped) {
    if (skipped != NULL) {
        *skipped = 0;
    }
    return -1;
}

s32 Port_ModImport(char *path) {
    (void)path;
    return 0;
}

s32 Port_ModImportAll(char *path) {
    (void)path;
    return 0;
}

s32 Port_ModListName(s32 list, s32 i, char *out) {
    (void)list;
    (void)i;
    out[0] = '\0';
    return 0;
}
