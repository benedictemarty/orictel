/**
 * @file test_videotex.c
 * @brief Tests unitaires pour le decodeur Videotex OricTel
 *
 * Compile et execute sur l'hote (pas sur 6502):
 *   gcc -Wall -Wextra -Isrc -o build/test_videotex \
 *       tests/test_videotex.c src/videotex.c src/fonts.c -DTEST_HOST
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "videotex.h"

/* Compteurs de tests */
static int tests_run = 0;
static int tests_passed = 0;
static int tests_failed = 0;

#define ASSERT_EQ(msg, expected, actual) do { \
    tests_run++; \
    if ((expected) == (actual)) { \
        tests_passed++; \
    } else { \
        tests_failed++; \
        printf("  ECHEC: %s (attendu=%d, obtenu=%d)\n", \
               msg, (int)(expected), (int)(actual)); \
    } \
} while(0)

#define ASSERT_STR(msg, expected, actual, len) do { \
    tests_run++; \
    if (memcmp((expected), (actual), (len)) == 0) { \
        tests_passed++; \
    } else { \
        tests_failed++; \
        printf("  ECHEC: %s\n", msg); \
    } \
} while(0)

/* ===================================================================
 *  Helper: envoyer une sequence d'octets
 * =================================================================== */
static void send_bytes(vtx_context_t* ctx, const unsigned char* data, int len)
{
    int i;
    for (i = 0; i < len; ++i) {
        vtx_process(ctx, data[i]);
    }
}

/* ===================================================================
 *  Test: initialisation
 * =================================================================== */
static void test_init(void)
{
    vtx_context_t ctx;

    printf("Test: Initialisation\n");
    vtx_init(&ctx);

    ASSERT_EQ("state = NORMAL", VTX_STATE_NORMAL, ctx.state);
    ASSERT_EQ("cur_x = 0", 0, ctx.cur_x);
    ASSERT_EQ("cur_y = 1", 1, ctx.cur_y);
    ASSERT_EQ("charset = G0", CHARSET_G0, ctx.charset);
    ASSERT_EQ("fg = WHITE", VTX_WHITE, ctx.fg_color);
    ASSERT_EQ("bg = BLACK", VTX_BLACK, ctx.bg_color);
    ASSERT_EQ("screen[1][0].ch = espace", ' ', ctx.screen[1][0].ch);
}

/* ===================================================================
 *  Test: affichage de caracteres
 * =================================================================== */
static void test_print_chars(void)
{
    vtx_context_t ctx;

    printf("Test: Affichage de caracteres\n");
    vtx_init(&ctx);

    /* Envoyer "ABC" */
    vtx_process(&ctx, 'A');
    vtx_process(&ctx, 'B');
    vtx_process(&ctx, 'C');

    ASSERT_EQ("screen[1][0] = A", 'A', ctx.screen[1][0].ch);
    ASSERT_EQ("screen[1][1] = B", 'B', ctx.screen[1][1].ch);
    ASSERT_EQ("screen[1][2] = C", 'C', ctx.screen[1][2].ch);
    ASSERT_EQ("cur_x = 3", 3, ctx.cur_x);
    ASSERT_EQ("cur_y = 1", 1, ctx.cur_y);
    ASSERT_EQ("dirty[1] = 1", 1, ctx.dirty[1]);
}

/* ===================================================================
 *  Test: codes de controle
 * =================================================================== */
static void test_control_codes(void)
{
    vtx_context_t ctx;

    printf("Test: Codes de controle\n");
    vtx_init(&ctx);

    /* CR - retour chariot */
    vtx_process(&ctx, 'A');
    vtx_process(&ctx, 'B');
    vtx_process(&ctx, 0x0D);  /* CR */
    ASSERT_EQ("CR: cur_x = 0", 0, ctx.cur_x);

    /* LF - curseur bas */
    vtx_process(&ctx, 0x0A);  /* LF */
    ASSERT_EQ("LF: cur_y = 2", 2, ctx.cur_y);

    /* BS - curseur gauche */
    vtx_process(&ctx, 'X');
    vtx_process(&ctx, 0x08);  /* BS */
    ASSERT_EQ("BS: cur_x = 0", 0, ctx.cur_x);

    /* VT - curseur haut */
    vtx_process(&ctx, 0x0B);  /* VT */
    ASSERT_EQ("VT: cur_y = 1", 1, ctx.cur_y);

    /* RS - home */
    vtx_process(&ctx, 0x0A);  /* LF -> y=2 */
    vtx_process(&ctx, 'X');   /* x=1 */
    vtx_process(&ctx, 0x1E);  /* RS = home */
    ASSERT_EQ("RS: cur_x = 0", 0, ctx.cur_x);
    ASSERT_EQ("RS: cur_y = 1", 1, ctx.cur_y);
}

/* ===================================================================
 *  Test: effacement ecran
 * =================================================================== */
static void test_clear_screen(void)
{
    vtx_context_t ctx;

    printf("Test: Effacement ecran\n");
    vtx_init(&ctx);

    /* Ecrire du contenu */
    vtx_process(&ctx, 'H');
    vtx_process(&ctx, 'I');

    /* FF - effacer ecran */
    vtx_process(&ctx, 0x0C);
    ASSERT_EQ("FF: screen[1][0] = espace", ' ', ctx.screen[1][0].ch);
    ASSERT_EQ("FF: cur_x = 0", 0, ctx.cur_x);
    ASSERT_EQ("FF: cur_y = 1", 1, ctx.cur_y);
}

/* ===================================================================
 *  Test: basculement G0/G1
 * =================================================================== */
static void test_charset_switch(void)
{
    vtx_context_t ctx;

    printf("Test: Basculement G0/G1\n");
    vtx_init(&ctx);

    /* SO - basculer G1 */
    vtx_process(&ctx, 0x0E);
    ASSERT_EQ("SO: charset = G1", CHARSET_G1, ctx.charset);

    /* Ecrire un caractere mosaique */
    vtx_process(&ctx, 0x3F);
    ASSERT_EQ("G1 char", CHARSET_G1, ctx.screen[1][0].charset);

    /* SI - revenir G0 */
    vtx_process(&ctx, 0x0F);
    ASSERT_EQ("SI: charset = G0", CHARSET_G0, ctx.charset);
}

/* ===================================================================
 *  Test: sequences ESC (attributs)
 * =================================================================== */
static void test_esc_attributes(void)
{
    vtx_context_t ctx;

    printf("Test: Sequences ESC (attributs)\n");
    vtx_init(&ctx);

    /* ESC $42 = couleur encre verte */
    vtx_process(&ctx, 0x1B);
    vtx_process(&ctx, 0x42);
    ASSERT_EQ("ESC ink green", VTX_GREEN, ctx.fg_color);

    /* ESC $48 = flash on */
    vtx_process(&ctx, 0x1B);
    vtx_process(&ctx, 0x48);
    ASSERT_EQ("ESC flash on", ATTR_FLASH, ctx.attr_flags & ATTR_FLASH);

    /* ESC $49 = flash off */
    vtx_process(&ctx, 0x1B);
    vtx_process(&ctx, 0x49);
    ASSERT_EQ("ESC flash off", 0, ctx.attr_flags & ATTR_FLASH);

    /* ESC $4D = double hauteur */
    vtx_process(&ctx, 0x1B);
    vtx_process(&ctx, 0x4D);
    ASSERT_EQ("ESC double height", SIZE_DOUBLE_HEIGHT, ctx.attr_size);

    /* ESC $4C = taille normale */
    vtx_process(&ctx, 0x1B);
    vtx_process(&ctx, 0x4C);
    ASSERT_EQ("ESC normal size", SIZE_NORMAL, ctx.attr_size);

    /* ESC $5C = inversion on */
    vtx_process(&ctx, 0x1B);
    vtx_process(&ctx, 0x5C);
    ASSERT_EQ("ESC invert on", ATTR_INVERT, ctx.attr_flags & ATTR_INVERT);

    /* ESC $5D = inversion off */
    vtx_process(&ctx, 0x1B);
    vtx_process(&ctx, 0x5D);
    ASSERT_EQ("ESC invert off", 0, ctx.attr_flags & ATTR_INVERT);

    /* ESC $58 = concealed */
    vtx_process(&ctx, 0x1B);
    vtx_process(&ctx, 0x58);
    ASSERT_EQ("ESC concealed", ATTR_CONCEALED, ctx.attr_flags & ATTR_CONCEALED);

    /* Retour etat normal */
    ASSERT_EQ("state = NORMAL", VTX_STATE_NORMAL, ctx.state);
}

/* ===================================================================
 *  Test: positionnement US
 * =================================================================== */
static void test_us_positioning(void)
{
    vtx_context_t ctx;

    printf("Test: Positionnement US\n");
    vtx_init(&ctx);

    /* US row col: $1F $4A $4B = ligne 10, colonne 10 */
    vtx_process(&ctx, 0x1F);
    vtx_process(&ctx, 0x4A);  /* row = $4A - $40 = 10 */
    vtx_process(&ctx, 0x4B);  /* col = $4B - $41 = 10 */

    ASSERT_EQ("US: cur_y = 10", 10, ctx.cur_y);
    ASSERT_EQ("US: cur_x = 10", 10, ctx.cur_x);
    ASSERT_EQ("state = NORMAL", VTX_STATE_NORMAL, ctx.state);
}

/* ===================================================================
 *  Test: sequences CSI
 * =================================================================== */
static void test_csi_sequences(void)
{
    vtx_context_t ctx;

    printf("Test: Sequences CSI\n");
    vtx_init(&ctx);

    /* Positionner le curseur */
    ctx.cur_x = 5;
    ctx.cur_y = 5;

    /* CSI A = curseur haut */
    vtx_process(&ctx, 0x1B);
    vtx_process(&ctx, 0x5B);
    vtx_process(&ctx, 'A');
    ASSERT_EQ("CSI A: cur_y = 4", 4, ctx.cur_y);

    /* CSI 3B = curseur bas x3 */
    vtx_process(&ctx, 0x1B);
    vtx_process(&ctx, 0x5B);
    vtx_process(&ctx, '3');
    vtx_process(&ctx, 'B');
    ASSERT_EQ("CSI 3B: cur_y = 7", 7, ctx.cur_y);

    /* CSI J = effacer ecran */
    vtx_process(&ctx, 'X');  /* Ecrire quelque chose */
    vtx_process(&ctx, 0x1B);
    vtx_process(&ctx, 0x5B);
    vtx_process(&ctx, 'J');
    ASSERT_EQ("CSI J: cur_x = 0", 0, ctx.cur_x);
    ASSERT_EQ("CSI J: cur_y = 1", 1, ctx.cur_y);
}

/* ===================================================================
 *  Test: repetition (REP)
 * =================================================================== */
static void test_repetition(void)
{
    vtx_context_t ctx;

    printf("Test: Repetition\n");
    vtx_init(&ctx);

    /* Ecrire 'X' puis REP 3 fois */
    vtx_process(&ctx, 'X');
    vtx_process(&ctx, 0x12);   /* REP */
    vtx_process(&ctx, 0x43);   /* 0x43 - 0x40 = 3 repetitions */

    ASSERT_EQ("REP: screen[1][0] = X", 'X', ctx.screen[1][0].ch);
    ASSERT_EQ("REP: screen[1][1] = X", 'X', ctx.screen[1][1].ch);
    ASSERT_EQ("REP: screen[1][2] = X", 'X', ctx.screen[1][2].ch);
    ASSERT_EQ("REP: screen[1][3] = X", 'X', ctx.screen[1][3].ch);
    ASSERT_EQ("REP: cur_x = 4", 4, ctx.cur_x);
}

/* ===================================================================
 *  Test: couleur fond (attribut serie)
 * =================================================================== */
static void test_background_color(void)
{
    vtx_context_t ctx;

    printf("Test: Couleur de fond (attribut serie)\n");
    vtx_init(&ctx);

    /* ESC $54 = fond bleu en attente */
    vtx_process(&ctx, 0x1B);
    vtx_process(&ctx, 0x54);
    ASSERT_EQ("pending_bg = BLUE", VTX_BLUE, ctx.pending_bg);
    ASSERT_EQ("has_pending = 1", 1, ctx.has_pending);

    /* L'espace (delimiteur) applique l'attribut en attente */
    vtx_process(&ctx, 0x20);
    ASSERT_EQ("bg_color = BLUE", VTX_BLUE, ctx.bg_color);
    ASSERT_EQ("cell bg = BLUE", VTX_BLUE, ctx.screen[1][0].bg);
}

/* ===================================================================
 *  Test: wrapping de ligne
 * =================================================================== */
static void test_line_wrapping(void)
{
    vtx_context_t ctx;
    unsigned char i;

    printf("Test: Wrapping de ligne\n");
    vtx_init(&ctx);

    /* Ecrire 41 caracteres (devrait wrapper) */
    for (i = 0; i < 41; ++i) {
        vtx_process(&ctx, 'A' + (i % 26));
    }

    ASSERT_EQ("wrap: cur_y = 2", 2, ctx.cur_y);
    ASSERT_EQ("wrap: cur_x = 1", 1, ctx.cur_x);
    ASSERT_EQ("wrap: screen[2][0]", 'A' + (40 % 26), ctx.screen[2][0].ch);
}

/* ===================================================================
 *  Test: G1 mosaiques
 * =================================================================== */
static void test_g1_mosaics(void)
{
    vtx_context_t ctx;

    printf("Test: Mosaiques G1\n");
    vtx_init(&ctx);

    /* Basculer en G1 */
    vtx_process(&ctx, 0x0E);

    /* Ecrire un caractere mosaique $3F = tous les blocs pleins */
    vtx_process(&ctx, 0x3F);
    ASSERT_EQ("G1: charset = G1", CHARSET_G1, ctx.screen[1][0].charset);
    ASSERT_EQ("G1: ch = $3F", 0x3F, ctx.screen[1][0].ch);
}

/* ===================================================================
 *  Point d'entree
 * =================================================================== */

int main(void)
{
    printf("=== OricTel - Tests unitaires decodeur Videotex ===\n\n");

    test_init();
    test_print_chars();
    test_control_codes();
    test_clear_screen();
    test_charset_switch();
    test_esc_attributes();
    test_us_positioning();
    test_csi_sequences();
    test_repetition();
    test_background_color();
    test_line_wrapping();
    test_g1_mosaics();

    printf("\n=== Resultats: %d/%d passes", tests_passed, tests_run);
    if (tests_failed > 0) {
        printf(", %d echecs", tests_failed);
    }
    printf(" ===\n");

    return tests_failed > 0 ? 1 : 0;
}
