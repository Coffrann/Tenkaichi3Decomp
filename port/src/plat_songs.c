/* The manifest of songs added to the music select (see plat_songs.h). Runs at start-up: it gives the game the
   added tracks' list offsets and names, and the file layer where their files live. */
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "plat_songs.h"

char gPortSongNames[PORT_SONG_MAX][64];
int gPortSongCount;
int gPortSongOffsets[PORT_SONG_MAX];

typedef struct SongAlias {
    char rel[48];     /* the path the game asks for, under the data root */
    char target[192]; /* where it is served from */
} SongAlias;

static SongAlias sAliases[PORT_SONG_MAX];
static int sAliasCount;
static int sDone;

static const char *data_root(void) {
    const char *r = getenv("BT3_DATA");
    return r != NULL ? r : "gamedata";
}

static char *trim(char *s) {
    char *e;
    while (*s == ' ' || *s == '\t') {
        s++;
    }
    e = s + strlen(s);
    while (e > s && (e[-1] == ' ' || e[-1] == '\t' || e[-1] == '\r' || e[-1] == '\n')) {
        *--e = '\0';
    }
    return s;
}

static char sDir[256]; /* the folder the songs are in (plat_extras.c) */
static char sFiles[PORT_SONG_MAX][128];

extern int PortExtras_Dir(const char *kind, const char *env, char *out, unsigned size);
extern int PortExtras_Scan(const char *dir, const char *ext, char names[][128], int max);
extern void PortExtras_NameFromFile(const char *file, char *name, unsigned size);

void PortSongs_Init(void);

/* The order the files it does not mention follow: by name without regard to case, as PortExtras_Scan sorts them. */
static int song_cmp(const void *a, const void *b) {
    const unsigned char *x = a, *y = b;
    for (; *x != '\0' && tolower(*x) == tolower(*y); x++, y++) {
    }
    return tolower(*x) != tolower(*y) ? tolower(*x) - tolower(*y) : strcmp((const char *)a, (const char *)b);
}

const char *PortSongs_Dir(void) {
    PortSongs_Init(); /* (the overlay may ask before the game has started) */
    return sDir;
}

/* Adds one song: its place in the music list, its name, and the file the game's id for it stands for. */
static void add_song(const char *file, const char *name) {
    int i, offset, index;

    for (i = 0; i < gPortSongCount; i++) {
        if (strcmp(sFiles[i], file) == 0) {
            return;
        }
    }
    if (gPortSongCount >= PORT_SONG_MAX) {
        fprintf(stderr, "bt3: songs: more than %d added, %s is left out\n", PORT_SONG_MAX, file);
        return;
    }
    if (strlen(file) >= sizeof(sFiles[0]) || strlen(sDir) + 1 + strlen(file) >= sizeof(sAliases[0].target)) {
        /* (cut short, the name was that of no file: the song was in the list and silent) */
        fprintf(stderr, "bt3: songs: %s is left out: the folder and the name together are too long\n", file);
        return;
    }
    i = gPortSongCount;
    snprintf(sFiles[i], sizeof(sFiles[i]), "%s", file);
    offset = PORT_SONG_FIRST_OFFSET + i;
    index = PORT_SONG_FILE_INDEX + i; /* PORT_BGM_FILE(offset), as an index of the second archive */
    gPortSongOffsets[i] = offset;
    snprintf(gPortSongNames[i], 64, "%s", name);
    snprintf(sAliases[sAliasCount].rel, sizeof(sAliases[sAliasCount].rel), "pzs3us2/%05d.bin", index);
    snprintf(sAliases[sAliasCount].target, sizeof(sAliases[sAliasCount].target), "%s/%s", sDir, file);
    sAliasCount++;
    gPortSongCount++;
}

/* Reads the folder and adds the songs that are not in the list yet, at its end: a song keeps its place (the game
   holds the place's number while a fight with that music is set up and runs). */
static void collect(void) {
    char path[512];
    char line[256];
    FILE *fp;

    /* the list first (its names and its order), then whatever else is in the folder, by name */
    snprintf(path, sizeof(path), "%s/songs.txt", sDir);
    fp = fopen(path, "rb");
    while (fp != NULL && fgets(line, sizeof(line), fp) != NULL) {
        char *bar;
        if (line[0] == '\0' || line[0] == '#' || line[0] == '\n' || line[0] == '\r') {
            continue;
        }
        bar = strchr(line, '|');
        if (bar == NULL) {
            continue;
        }
        *bar = '\0';
        /* The line may name an ADX, an mp3, a wav or a flac: the file it names is the one played (gs/snd_ma.c for
           the last three). */
        add_song(trim(line), trim(bar + 1));
    }
    if (fp != NULL) {
        fclose(fp);
    }
    {
        /* Everything else in the folder, of any of the formats the port plays, by name. The four scans' names are
           put together and sorted once, so the order does not depend on the extension. */
        static const char *exts[] = { ".adx", ".mp3", ".wav", ".flac" };
        static char found[PORT_SONG_MAX * 4][128];
        static char one[64][128];
        int n = 0, e, k;

        for (e = 0; e < (int)(sizeof(exts) / sizeof(exts[0])); e++) {
            int m = PortExtras_Scan(sDir, exts[e], one, 64), j;
            for (j = 0; j < m && n < (int)(sizeof(found) / sizeof(found[0])); j++) {
                snprintf(found[n++], 128, "%s", one[j]);
            }
        }
        qsort(found, (size_t)n, 128, song_cmp);
        for (k = 0; k < n; k++) {
            char name[64];
            PortExtras_NameFromFile(found[k], name, sizeof(name));
            add_song(found[k], name);
        }
    }
}

void PortSongs_Init(void) {
    if (sDone) {
        return;
    }
    sDone = 1;
    if (!PortExtras_Dir("songs", "BT3_SONGS", sDir, sizeof(sDir))) {
        return;
    }
    collect();
    if (gPortSongCount > 0) {
        fprintf(stderr, "bt3: songs: %d added from %s\n", gPortSongCount, sDir);
    }
}

/* Kept for the game's select screens (src/menu/menu_c_e.c, menu_e_b.c), which took in the songs a background
   conversion had finished. Nothing is converted any more: the folder is read once, at start, and a song in it is
   played as it is (gs/snd_ma.c), so the list does not change while the program runs. */
void PortSongs_Refresh(void) {
}

const char *PortSongs_Name(int index) {
    return index >= 0 && index < gPortSongCount ? gPortSongNames[index] : "";
}

int PortSongs_Count(void) {
    return gPortSongCount;
}

int PortSongs_Offset(int index) {
    if (index < 0 || index >= gPortSongCount) {
        return 0;
    }
    return gPortSongOffsets[index];
}

/* Whether `path` is the file of a song added from outside the disc (for the sound code: such a file is played
   round and round like the disc's music, though it has no loop points of its own). */
int PortSongs_IsFile(const char *path) {
    size_t pl = strlen(path), tl;
    int i;
    for (i = 0; i < sAliasCount; i++) {
        tl = strlen(sAliases[i].target);
        if (tl != 0 && tl <= pl && strcmp(path + pl - tl, sAliases[i].target) == 0) {
            return 1;
        }
    }
    return 0;
}

int PortSongs_Alias(const char *rel, char *out, unsigned n) {
    int i;
    for (i = 0; i < sAliasCount; i++) {
        if (strcmp(sAliases[i].rel, rel) == 0) {
            snprintf(out, n, "%s", sAliases[i].target);
            return 1;
        }
    }
    return 0;
}

/* BT3_TEST_BGM=<entry>: every battle is set up with that entry of the music list, whatever the menus chose
   (testing: 0x1A is the first added song). */
int Port_TestBgm(void) {
    const char *e = getenv("BT3_TEST_BGM");
    return e != NULL && e[0] != '\0' ? (int)strtol(e, NULL, 0) : -1;
}
