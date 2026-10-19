/* The Mod Manager's library (port/src/gs/modmgr.c) over the character packages (port/src/plat_charas.c), on their
 * own: the FORMS screen's transformations and fusions written into character.txt, read back, and put into a model's
 * parameter block as the fight does (Port_CharaParamApply); a disc character's forms in a package made for them.
 * Needs a model of the player's own disc (the game data folder); nothing of it is kept.
 *
 *   gcc -g -O1 -fsanitize=address,undefined -fno-sanitize-recover=all -Iport/include \
 *       port/tests/chara_packages/modmgr_test.c port/src/plat_charas.c port/src/gs/modmgr.c \
 *       $(pkg-config --cflags --libs sdl3) -lm -o modmgr_test
 *   modmgr_test <game data> <a new folder>     ends with "modmgr OK"
 */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

typedef int32_t s32;
typedef uint16_t u16;
typedef uint32_t u32;
#include "../../../include/menu/port_mods.h"
#include "plat_charas.h"

/* what the two files ask of the game, stood in for */
static int sSession;
int Port_NetSession(void) { return sSession; }
static char sRoot[600];
int PortExtras_Dir(const char *kind, const char *env, char *out, unsigned size) {
    (void)kind;
    (void)env;
    snprintf(out, size, "%s", sRoot);
    return 1;
}
int Port_Setting(const char *name, int def) { (void)name; return def; }
void Port_SettingSave(const char *name, int value) { (void)name; (void)value; }
const char *Port_DiscCharaName(int chara) { (void)chara; return "?"; } /* src/menu/port_tbo.c: a disc character's name */
void *Heap_Alloc(int size, int align, int fromTail, int heap) { (void)align; (void)fromTail; (void)heap; return malloc((size_t)size); }
void Heap_Free(void *ptr) { free(ptr); }
void Res_RelocateOffsets(void *out, void *base, void *hdr) { (void)out; (void)base; (void)hdr; }
char *PortTbo_FighterName(s32 chara) {
    static char name[48];
    snprintf(name, sizeof(name), "Fighter %d", chara);
    return name;
}
void PortTbo_TextMode(int on) { (void)on; }
int PortTbo_TextRead(void) { return 0; }
int PortInput_DropTake(char *out, unsigned size) { (void)out; (void)size; return 0; }
extern void Port_CharaParamApply(int chara, void *param);

static int sFail;
#define CHECK(c, ...) do { if (!(c)) { printf("FAIL: " __VA_ARGS__); printf("\n"); sFail++; } } while (0)

static long copy(const char *from, const char *to) {
    FILE *a = fopen(from, "rb"), *b;
    char buf[65536];
    size_t n;
    long all = 0;
    if (a == NULL || (b = fopen(to, "wb")) == NULL) {
        if (a != NULL) {
            fclose(a);
        }
        return -1;
    }
    while ((n = fread(buf, 1, sizeof(buf), a)) > 0) {
        fwrite(buf, 1, n, b);
        all += (long)n;
    }
    fclose(a);
    fclose(b);
    return all;
}

static void text(const char *path, const char *t) {
    FILE *f = fopen(path, "wb");
    fputs(t, f);
    fclose(f);
}

/* the parameter block of a model file (its entry 17), copied out */
static int param_of(const char *path, unsigned char *out) {
    FILE *f = fopen(path, "rb");
    static unsigned char d[0x180000];
    long n;
    uint32_t a;
    if (f == NULL) {
        return 0;
    }
    n = (long)fread(d, 1, sizeof(d), f);
    fclose(f);
    memcpy(&a, d + 4 + 17 * 4, 4);
    if (n < 0x100 || a + 0xC6 > (uint32_t)n) {
        return 0;
    }
    memcpy(out, d + a, 0xC6);
    return 1;
}

static int index_of(const char *folder) {
    PortModInfo m;
    int i;
    for (i = 0; i < Port_ModCount(); i++) {
        Port_ModGetInfo(i, &m);
        if (strcmp(m.name, folder) == 0) {
            return i;
        }
    }
    return -1;
}

int main(int argc, char **argv) {
    char path[900], model[900], v[300];
    unsigned char param[0xC6];
    PortTrans t;
    int hero, rival, k;

    if (argc != 3) {
        fprintf(stderr, "modmgr_test <game data> <a new folder>\n");
        return 2;
    }
    setenv("BT3_DATA", argv[1], 1);
    snprintf(sRoot, sizeof(sRoot), "%s", argv[2]);
    if (mkdir(sRoot, 0755) != 0) {
        perror(sRoot);
        return 2;
    }
    /* Hero adds a character with disc character 55's model; Rival replaces 3 */
    snprintf(model, sizeof(model), "%s/pzs3us1/%05d.bin", argv[1], 0x590 + 55 * 10 - 1);
    snprintf(path, sizeof(path), "%s/Hero", sRoot);
    mkdir(path, 0755);
    snprintf(path, sizeof(path), "%s/Hero/hero_1p.pak", sRoot);
    if (copy(model, path) <= 0) {
        printf("no model at %s\n", model);
        return 2;
    }
    snprintf(path, sizeof(path), "%s/Hero/character.txt", sRoot);
    text(path, "format=1\nname=Hero\nbase=55\n");
    snprintf(path, sizeof(path), "%s/Rival", sRoot);
    mkdir(path, 0755);
    snprintf(path, sizeof(path), "%s/Rival/character.txt", sRoot);
    text(path, "format=1\nreplaces=3\nname=Rival\n");
    PortCharas_Init();
    hero = index_of("Hero");
    rival = index_of("Rival");
    CHECK(hero >= 0 && rival >= 0, "the library: hero %d rival %d", hero, rival);

    /* the model's own, then a set */
    Port_TransGet(PORT_REF_MOD + hero, &t);
    printf("Hero's own: targets %d %d %d %d, costs %d %d %d %d, default %d\n", t.target[0], t.target[1], t.target[2], t.target[3], t.cost[0], t.cost[1],
           t.cost[2], t.cost[3], t.next);
    CHECK(!t.set, "nothing set yet");
    t.set = 1;
    t.target[0] = PORT_REF_MOD + rival; /* left: Rival, which is disc character 3 */
    t.target[1] = 12;
    t.target[2] = -1;
    t.cost[0] = 4;
    t.type[1] = 2;
    t.effect[0] = 7;
    t.assist[1] = 20;
    t.fresh[0] = 1;
    t.next = 1;
    t.fuseSet = 1;
    t.fuse[0] = 30;
    t.partner[0][0] = 40;
    t.partner[0][1] = PORT_REF_MOD + rival;
    t.fuseCost[0] = 5;
    t.fuseType[0] = 2;
    t.fuseLead[0] = 40;
    Port_TransSet(PORT_REF_MOD + hero, &t);
    hero = index_of("Hero");
    rival = index_of("Rival");
    PortCharas_ManifestGet("Hero", "left", v, sizeof(v));
    CHECK(strcmp(v, "Rival") == 0, "left=%s", v);
    PortCharas_ManifestGet("Hero", "partner1", v, sizeof(v));
    CHECK(strcmp(v, "disc:40,Rival,none,none") == 0, "partner1=%s", v);
    PortCharas_ManifestGet("Hero", "fusion1_type", v, sizeof(v));
    CHECK(strcmp(v, "potara") == 0, "fusion1_type=%s", v);

    /* read back as set */
    memset(&t, 0, sizeof(t));
    Port_TransGet(PORT_REF_MOD + hero, &t);
    CHECK(t.set && t.fuseSet, "set %d fuseSet %d", t.set, t.fuseSet);
    CHECK(t.target[0] == PORT_REF_MOD + rival && t.target[1] == 12 && t.target[2] == -1, "targets %d %d %d", t.target[0], t.target[1], t.target[2]);
    CHECK(t.cost[0] == 4 && t.type[1] == 2 && t.effect[0] == 7 && t.assist[1] == 20 && t.fresh[0] == 1 && t.next == 1, "numbers back");
    CHECK(t.fuse[0] == 30 && t.partner[0][0] == 40 && t.partner[0][1] == PORT_REF_MOD + rival && t.fuseCost[0] == 5 && t.fuseType[0] == 2 &&
              t.fuseLead[0] == 40, "fusion back");

    /* the fight: Hero is added character 0xC6; its model's parameter block gets what was set */
    CHECK(param_of(model, param), "the model's parameter block");
    Port_CharaParamApply(0xC6, param);
    printf("into the block: 98 %02x %02x %02x | 9c %02x | a0 %02x %02x | a4 %02x | a8 %02x %02x | ac %02x | ad %02x | ae %02x b1 %02x b4 %02x b7 %02x ba %02x %02x\n",
           param[0x98], param[0x99], param[0x9A], param[0x9C], param[0xA0], param[0xA1], param[0xA4], param[0xA8], param[0xA9], param[0xAC],
           param[0xAD], param[0xAE], param[0xB1], param[0xB4], param[0xB7], param[0xBA], param[0xBB]);
    CHECK(param[0x98] == 3 && param[0x99] == 12 && param[0x9A] == 0xFF && param[0x9C] == 4 && param[0xA1] == 2 && param[0xA4] == 7 && param[0xA9] == 20 &&
              param[0xAC] == 1 && (param[0xAD] & 0x10) && param[0xAE] == 5 && param[0xB1] == 2 && param[0xB4] == 30 && param[0xB7] == 40 &&
              param[0xBA] == 40 && param[0xBB] == 3,
          "the bytes");
    sSession = 1;
    memset(param, 0x77, sizeof(param));
    Port_CharaParamApply(0xC6, param);
    CHECK(param[0x98] == 0x77 && param[0x9C] == 0x77, "written in a session");
    sSession = 0;

    /* back to the model's own */
    t.set = t.fuseSet = 0;
    Port_TransSet(PORT_REF_MOD + index_of("Hero"), &t);
    CHECK(!PortCharas_ManifestGet("Hero", "left", v, sizeof(v)) && !PortCharas_ManifestGet("Hero", "fusion1_cost", v, sizeof(v)), "reset");
    CHECK(PortCharas_ManifestGet("Hero", "name", v, sizeof(v)) && strcmp(v, "Hero") == 0, "the name kept");

    /* a disc character's forms: a package made for them */
    Port_TransGet(10, &t);
    t.set = 1;
    t.target[0] = 11;
    Port_TransSet(10, &t);
    k = index_of("Fighter 10 forms");
    CHECK(k >= 0, "the forms package");
    PortCharas_ManifestGet("Fighter 10 forms", "replaces", v, sizeof(v));
    CHECK(strcmp(v, "10") == 0, "replaces=%s", v);
    Port_TransGet(10, &t);
    CHECK(t.set && t.target[0] == 11, "disc forms back: set %d target %d", t.set, t.target[0]);
    printf(sFail == 0 ? "modmgr OK\n" : "modmgr FAILED (%d)\n", sFail);
    return sFail != 0;
}
