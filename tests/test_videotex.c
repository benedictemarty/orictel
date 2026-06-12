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

/* Stubs des dependances HW non disponibles en mode TEST_HOST.
 * serial_send capture les octets envoyes pour pouvoir les inspecter
 * dans les tests d'identification (ENQ, ENQROM). */
#ifdef TEST_HOST
unsigned char g_global_mask = 1;  /* defaut: cellules concealed cachees */
static unsigned char tx_buf[64];
static int tx_len = 0;
static void tx_reset(void) { tx_len = 0; }
void serial_send(unsigned char byte) {
    if (tx_len < (int)sizeof(tx_buf)) tx_buf[tx_len++] = byte;
}
void serial_send_buf(const unsigned char* buf, unsigned char len) { (void)buf; (void)len; }
unsigned char serial_recv(void) { return 0xFF; }
unsigned char serial_poll(void) { return 0; }
void serial_init(void) {}
void display_clear(void) {}
void display_beep(void) {}
void display_render(vtx_context_t* ctx) { (void)ctx; }
#endif

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

    /* ESC $5D = inversion ON (convention telenet/miedit) */
    vtx_process(&ctx, 0x1B);
    vtx_process(&ctx, 0x5D);
    ASSERT_EQ("ESC invert on", ATTR_INVERT, ctx.attr_flags & ATTR_INVERT);

    /* ESC $5C = inversion OFF (convention telenet/miedit) */
    vtx_process(&ctx, 0x1B);
    vtx_process(&ctx, 0x5C);
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
 *  Test: re-sync ESC en milieu de sequence
 * =================================================================== */
static void test_esc_resync(void)
{
    vtx_context_t ctx;
    printf("Test: Re-sync ESC en milieu de sequence\n");
    vtx_init(&ctx);

    /* Sequence CSI tronquee suivie d'un ESC + commande valide.
     * Avant le fix, l'ESC etait avale comme parametre invalide. */
    vtx_process(&ctx, 0x1B);  /* ESC */
    vtx_process(&ctx, 0x5B);  /* [ - debut CSI */
    vtx_process(&ctx, '1');
    vtx_process(&ctx, '2');
    vtx_process(&ctx, 0x1B);  /* ESC interrompt: re-sync attendu */
    vtx_process(&ctx, 0x44);  /* ESC $44 = INK bleu */
    ASSERT_EQ("re-sync ESC: state retombe NORMAL", VTX_STATE_NORMAL, ctx.state);
    ASSERT_EQ("re-sync ESC: INK bleu applique", VTX_BLUE, ctx.fg_color);

    /* Sequence US tronquee + ESC */
    vtx_init(&ctx);
    vtx_process(&ctx, 0x1F);  /* US */
    vtx_process(&ctx, 0x1B);  /* ESC interrompt avant la ligne */
    vtx_process(&ctx, 0x46);  /* ESC $46 = INK cyan */
    ASSERT_EQ("re-sync ESC depuis US: INK cyan", VTX_CYAN, ctx.fg_color);

    /* Sequence PRO tronquee + ESC */
    vtx_init(&ctx);
    vtx_process(&ctx, 0x1B);
    vtx_process(&ctx, 0x39);  /* ESC $39 = PRO1 */
    vtx_process(&ctx, 0x1B);  /* ESC interrompt avant la commande */
    vtx_process(&ctx, 0x42);  /* ESC $42 = INK vert */
    ASSERT_EQ("re-sync ESC depuis PRO: INK vert", VTX_GREEN, ctx.fg_color);
}

/* ===================================================================
 *  Test: sequences PRO1 / PRO2 / PRO3 (STUM 1B)
 * =================================================================== */
static void test_pro_sequences(void)
{
    vtx_context_t ctx;
    printf("Test: Sequences PRO1/PRO2/PRO3\n");

    /* PRO1 = ESC $39 + 1 octet. Apres 1 octet de payload, NORMAL. */
    vtx_init(&ctx);
    vtx_process(&ctx, 0x1B);
    vtx_process(&ctx, 0x39);
    ASSERT_EQ("PRO1: state=PRO apres ESC $39", VTX_STATE_PRO, ctx.state);
    ASSERT_EQ("PRO1: 1 octet a consommer", 1, (ctx.pro_kind - ctx.pro_idx));
    vtx_process(&ctx, 0x41);  /* commande quelconque */
    ASSERT_EQ("PRO1: state=NORMAL apres 1 octet", VTX_STATE_NORMAL, ctx.state);

    /* Apres PRO1, un caractere normal doit s'afficher (sync OK) */
    vtx_process(&ctx, 'A');
    ASSERT_EQ("PRO1: A affiche apres sequence", 'A', ctx.screen[1][0].ch);

    /* PRO2 = ESC $3A + 2 octets */
    vtx_init(&ctx);
    vtx_process(&ctx, 0x1B);
    vtx_process(&ctx, 0x3A);
    ASSERT_EQ("PRO2: 2 octets a consommer", 2, (ctx.pro_kind - ctx.pro_idx));
    vtx_process(&ctx, 0x69);  /* commande AIGUILLAGE */
    ASSERT_EQ("PRO2: 1 octet restant", 1, (ctx.pro_kind - ctx.pro_idx));
    ASSERT_EQ("PRO2: state=PRO encore", VTX_STATE_PRO, ctx.state);
    vtx_process(&ctx, 0x43);  /* parametre */
    ASSERT_EQ("PRO2: state=NORMAL apres 2 octets", VTX_STATE_NORMAL, ctx.state);
    vtx_process(&ctx, 'B');
    ASSERT_EQ("PRO2: B affiche apres sequence", 'B', ctx.screen[1][0].ch);

    /* PRO3 = ESC $3B + 3 octets */
    vtx_init(&ctx);
    vtx_process(&ctx, 0x1B);
    vtx_process(&ctx, 0x3B);
    ASSERT_EQ("PRO3: 3 octets a consommer", 3, (ctx.pro_kind - ctx.pro_idx));
    vtx_process(&ctx, 0x69);
    vtx_process(&ctx, 0x50);
    vtx_process(&ctx, 0x52);
    ASSERT_EQ("PRO3: state=NORMAL apres 3 octets", VTX_STATE_NORMAL, ctx.state);
    vtx_process(&ctx, 'C');
    ASSERT_EQ("PRO3: C affiche apres sequence", 'C', ctx.screen[1][0].ch);
}

/* ===================================================================
 *  Test: ENQROM (PRO1 + $7B = identification terminal)
 * =================================================================== */
static void test_enqrom(void)
{
    vtx_context_t ctx;
    printf("Test: ENQROM (PRO1 + $7B)\n");
    vtx_init(&ctx);

    tx_reset();
    vtx_process(&ctx, 0x1B);
    vtx_process(&ctx, 0x39);
    vtx_process(&ctx, 0x7B);  /* ENQROM */

    ASSERT_EQ("ENQROM: 5 octets emis", 5, tx_len);
    ASSERT_EQ("ENQROM: SOH", 0x01, tx_buf[0]);
    ASSERT_EQ("ENQROM: constructeur Matra", 0x7B, tx_buf[1]);
    ASSERT_EQ("ENQROM: type Minitel 1B", 0x74, tx_buf[2]);
    ASSERT_EQ("ENQROM: version", 0x63, tx_buf[3]);
    ASSERT_EQ("ENQROM: EOT", 0x04, tx_buf[4]);
    ASSERT_EQ("ENQROM: state retombe NORMAL", VTX_STATE_NORMAL, ctx.state);

    /* PRO1 inconnu: aucune emission */
    tx_reset();
    vtx_process(&ctx, 0x1B);
    vtx_process(&ctx, 0x39);
    vtx_process(&ctx, 0x41);  /* code arbitraire non gere */
    ASSERT_EQ("PRO1 inconnu: 0 octet emis", 0, tx_len);
}

/* ===================================================================
 *  Test: PRO2 mode minuscules (lowercase) - $3A $69/$6A $45
 * =================================================================== */
static void test_pro2_lowercase(void)
{
    vtx_context_t ctx;
    printf("Test: PRO2 mode minuscules\n");
    vtx_init(&ctx);

    /* Defaut: lowercase_mode = 0, 'a' s'affiche en 'A' */
    ASSERT_EQ("defaut: lowercase_mode = 0", 0, ctx.lowercase_mode);
    vtx_process(&ctx, 'a');
    ASSERT_EQ("defaut: 'a' force en 'A'", 'A', ctx.screen[1][0].ch);

    /* PRO2 START LOWERCASE */
    vtx_process(&ctx, 0x1B); vtx_process(&ctx, 0x3A);
    vtx_process(&ctx, 0x69); vtx_process(&ctx, 0x45);
    ASSERT_EQ("apres START LOWERCASE: lowercase_mode = 1", 1, ctx.lowercase_mode);
    vtx_process(&ctx, 'b');
    ASSERT_EQ("LOWERCASE on: 'b' affiche tel quel", 'b', ctx.screen[1][1].ch);

    /* PRO2 STOP LOWERCASE */
    vtx_process(&ctx, 0x1B); vtx_process(&ctx, 0x3A);
    vtx_process(&ctx, 0x6A); vtx_process(&ctx, 0x45);
    ASSERT_EQ("apres STOP LOWERCASE: lowercase_mode = 0", 0, ctx.lowercase_mode);
    vtx_process(&ctx, 'c');
    ASSERT_EQ("LOWERCASE off: 'c' force en 'C'", 'C', ctx.screen[1][2].ch);
}

/* ===================================================================
 *  Test: PRO2 mode rouleau (rolling) - $3A $69/$6A $43
 * =================================================================== */
static void test_pro2_rolling(void)
{
    vtx_context_t ctx;
    printf("Test: PRO2 mode rouleau\n");
    vtx_init(&ctx);

    /* Defaut: rolling_mode = 0 */
    ASSERT_EQ("defaut: rolling_mode = 0", 0, ctx.rolling_mode);

    /* PRO2 START ROLLING */
    vtx_process(&ctx, 0x1B); vtx_process(&ctx, 0x3A);
    vtx_process(&ctx, 0x69); vtx_process(&ctx, 0x43);
    ASSERT_EQ("apres START ROLLING: rolling_mode = 1", 1, ctx.rolling_mode);

    /* Tester le scroll: poser un 'X' en ligne 2, descendre en bas
     * d'ecran, faire scroller, verifier que 'X' a remonte. */
    vtx_set_cursor(&ctx, 2, 5);
    vtx_process(&ctx, 'X');
    ASSERT_EQ("avant scroll: X en (2,5)", 'X', ctx.screen[2][5].ch);

    /* Forcer cur_y a la derniere ligne et provoquer un scroll via LF */
    vtx_set_cursor(&ctx, VTX_ROWS - 1, 0);
    vtx_process(&ctx, 0x0A);  /* LF */
    /* Apres scroll, le 'X' doit etre passe de ligne 2 a ligne 1 */
    ASSERT_EQ("apres scroll: X en (1,5)", 'X', ctx.screen[1][5].ch);
    ASSERT_EQ("apres scroll: ligne 2 vide", ' ', ctx.screen[2][5].ch);

    /* PRO2 STOP ROLLING */
    vtx_process(&ctx, 0x1B); vtx_process(&ctx, 0x3A);
    vtx_process(&ctx, 0x6A); vtx_process(&ctx, 0x43);
    ASSERT_EQ("apres STOP ROLLING: rolling_mode = 0", 0, ctx.rolling_mode);
}

/* ===================================================================
 *  Test: PRO3 AIGUILLAGE (SWITCH ON/OFF entre modules)
 * =================================================================== */
static void test_pro3_aiguillage(void)
{
    vtx_context_t ctx;
    printf("Test: PRO3 AIGUILLAGE\n");
    vtx_init(&ctx);

    /* Defaut Minitel 1B: MODEM->ECRAN ON, CLAVIER->MODEM ON, autres OFF */
    ASSERT_EQ("defaut: MDM->SCR ON",  AIG_MDM_TO_SCR, ctx.aiguillages & AIG_MDM_TO_SCR);
    ASSERT_EQ("defaut: KBD->MDM ON",  AIG_KBD_TO_MDM, ctx.aiguillages & AIG_KBD_TO_MDM);
    ASSERT_EQ("defaut: KBD->SCR OFF", 0, ctx.aiguillages & AIG_KBD_TO_SCR);
    ASSERT_EQ("defaut: SCR->MDM OFF", 0, ctx.aiguillages & AIG_SCR_TO_MDM);

    /* PRO3 SWITCH ON: CLAVIER ($51) -> ECRAN ($58) = echo local activate */
    vtx_process(&ctx, 0x1B); vtx_process(&ctx, 0x3B);
    vtx_process(&ctx, 0x61);  /* ON */
    vtx_process(&ctx, 0x58);  /* dest = ECRAN */
    vtx_process(&ctx, 0x51);  /* source = CLAVIER */
    ASSERT_EQ("apres SWITCH ON KBD->SCR", AIG_KBD_TO_SCR, ctx.aiguillages & AIG_KBD_TO_SCR);
    ASSERT_EQ("state retombe NORMAL", VTX_STATE_NORMAL, ctx.state);

    /* PRO3 SWITCH OFF: rompre le meme lien */
    vtx_process(&ctx, 0x1B); vtx_process(&ctx, 0x3B);
    vtx_process(&ctx, 0x60);  /* OFF */
    vtx_process(&ctx, 0x58);
    vtx_process(&ctx, 0x51);
    ASSERT_EQ("apres SWITCH OFF KBD->SCR", 0, ctx.aiguillages & AIG_KBD_TO_SCR);

    /* PRO3 SWITCH OFF: MODEM -> ECRAN (couper l'affichage des donnees serveur) */
    vtx_process(&ctx, 0x1B); vtx_process(&ctx, 0x3B);
    vtx_process(&ctx, 0x60);
    vtx_process(&ctx, 0x58);
    vtx_process(&ctx, 0x59);
    ASSERT_EQ("apres SWITCH OFF MDM->SCR", 0, ctx.aiguillages & AIG_MDM_TO_SCR);

    /* PRO3 SWITCH ON: ECRAN -> MODEM (echo distant) */
    vtx_process(&ctx, 0x1B); vtx_process(&ctx, 0x3B);
    vtx_process(&ctx, 0x61);
    vtx_process(&ctx, 0x59);
    vtx_process(&ctx, 0x58);
    ASSERT_EQ("apres SWITCH ON SCR->MDM", AIG_SCR_TO_MDM, ctx.aiguillages & AIG_SCR_TO_MDM);

    /* Couple non gere: pas de modification */
    unsigned char before = ctx.aiguillages;
    vtx_process(&ctx, 0x1B); vtx_process(&ctx, 0x3B);
    vtx_process(&ctx, 0x61);
    vtx_process(&ctx, 0x50);  /* PRISE - pas gere */
    vtx_process(&ctx, 0x51);
    ASSERT_EQ("couple inconnu: aiguillages inchanges", before, ctx.aiguillages);

    /* Sync: apres PRO3 le decodeur accepte un caractere normal */
    vtx_set_cursor(&ctx, 5, 0);
    vtx_process(&ctx, 'Z');
    ASSERT_EQ("Z affiche apres PRO3", 'Z', ctx.screen[5][0].ch);
}

/* ===================================================================
 *  Test: PRO2 mode protocole VIDEOTEX/MIXED ($3A $32 $7E/$7D)
 * =================================================================== */
static void test_pro2_mode_protocole(void)
{
    vtx_context_t ctx;
    printf("Test: PRO2 mode protocole (VIDEOTEX/MIXED)\n");
    vtx_init(&ctx);

    ASSERT_EQ("defaut: terminal_mode = VIDEOTEX",
              TERM_MODE_VIDEOTEX, ctx.terminal_mode);

    /* PRO2 demande VIDEOTEX: ACK SEP+$71 emis */
    tx_reset();
    vtx_process(&ctx, 0x1B); vtx_process(&ctx, 0x3A);
    vtx_process(&ctx, 0x32); vtx_process(&ctx, 0x7E);
    ASSERT_EQ("VIDEOTEX request: 2 octets ACK", 2, tx_len);
    ASSERT_EQ("VIDEOTEX request: SEP", 0x13, tx_buf[0]);
    ASSERT_EQ("VIDEOTEX request: ack code $71", 0x71, tx_buf[1]);
    ASSERT_EQ("apres VIDEOTEX: terminal_mode inchange",
              TERM_MODE_VIDEOTEX, ctx.terminal_mode);

    /* PRO2 demande MIXED: refus silencieux (pas d'ACK, mode inchange) */
    tx_reset();
    vtx_process(&ctx, 0x1B); vtx_process(&ctx, 0x3A);
    vtx_process(&ctx, 0x32); vtx_process(&ctx, 0x7D);
    ASSERT_EQ("MIXED request: 0 octet emis", 0, tx_len);
    ASSERT_EQ("MIXED request: terminal_mode reste VIDEOTEX",
              TERM_MODE_VIDEOTEX, ctx.terminal_mode);

    /* Sync OK apres la sequence */
    vtx_set_cursor(&ctx, 5, 0);
    vtx_process(&ctx, 'M');
    ASSERT_EQ("M affiche apres PRO2 mode", 'M', ctx.screen[5][0].ch);
}

/* ===================================================================
 *  Test: PRO2 status keyboard ($3A $72 $51/$59)
 *
 *  Reponse attendue: ESC $3B $73 + module + status_byte (5 octets).
 * =================================================================== */
static void test_pro2_status_keyboard(void)
{
    vtx_context_t ctx;
    printf("Test: PRO2 status keyboard\n");
    vtx_init(&ctx);

    /* Status KEYBOARD_IN ($59), etat par defaut */
    tx_reset();
    vtx_process(&ctx, 0x1B); vtx_process(&ctx, 0x3A);
    vtx_process(&ctx, 0x72); vtx_process(&ctx, 0x59);
    ASSERT_EQ("KBD_IN: 5 octets emis", 5, tx_len);
    ASSERT_EQ("KBD_IN: ESC", 0x1B, tx_buf[0]);
    ASSERT_EQ("KBD_IN: PRO3", 0x3B, tx_buf[1]);
    ASSERT_EQ("KBD_IN: ack $73", 0x73, tx_buf[2]);
    ASSERT_EQ("KBD_IN: module $59", 0x59, tx_buf[3]);
    ASSERT_EQ("KBD_IN: status defaut $C0", 0xC0, tx_buf[4]);

    /* Activer lowercase et rolling, refaire la requete */
    vtx_process(&ctx, 0x1B); vtx_process(&ctx, 0x3A);
    vtx_process(&ctx, 0x69); vtx_process(&ctx, 0x45);  /* START LOWERCASE */
    vtx_process(&ctx, 0x1B); vtx_process(&ctx, 0x3A);
    vtx_process(&ctx, 0x69); vtx_process(&ctx, 0x43);  /* START ROLLING */

    tx_reset();
    vtx_process(&ctx, 0x1B); vtx_process(&ctx, 0x3A);
    vtx_process(&ctx, 0x72); vtx_process(&ctx, 0x59);
    ASSERT_EQ("KBD_IN modes on: status $C6",
              0xC0 | 0x02 | 0x04, tx_buf[4]);

    /* Status KEYBOARD_OUT ($51) doit aussi repondre */
    tx_reset();
    vtx_process(&ctx, 0x1B); vtx_process(&ctx, 0x3A);
    vtx_process(&ctx, 0x72); vtx_process(&ctx, 0x51);
    ASSERT_EQ("KBD_OUT: 5 octets emis", 5, tx_len);
    ASSERT_EQ("KBD_OUT: module $51", 0x51, tx_buf[3]);

    /* Cible inconnue: pas d'emission */
    tx_reset();
    vtx_process(&ctx, 0x1B); vtx_process(&ctx, 0x3A);
    vtx_process(&ctx, 0x72); vtx_process(&ctx, 0x40);
    ASSERT_EQ("cible $40 inconnue: 0 octet", 0, tx_len);

    /* Sync OK apres */
    vtx_set_cursor(&ctx, 5, 0);
    vtx_process(&ctx, 'K');
    ASSERT_EQ("K affiche apres status", 'K', ctx.screen[5][0].ch);
}

/* ===================================================================
 *  Test: PRO3 START/STOP module ($3B $69/$6A $59 $41/$43)
 * =================================================================== */
static void test_pro3_start_stop_module(void)
{
    vtx_context_t ctx;
    printf("Test: PRO3 START/STOP module\n");
    vtx_init(&ctx);

    ASSERT_EQ("defaut: kbd_extended OFF", 0, ctx.kbd_extended);
    ASSERT_EQ("defaut: kbd_cursor OFF",   0, ctx.kbd_cursor);

    /* START extended keyboard: ESC $3B $69 $59 $41 */
    vtx_process(&ctx, 0x1B); vtx_process(&ctx, 0x3B);
    vtx_process(&ctx, 0x69); vtx_process(&ctx, 0x59); vtx_process(&ctx, 0x41);
    ASSERT_EQ("apres START extended: kbd_extended ON", 1, ctx.kbd_extended);
    ASSERT_EQ("apres START extended: kbd_cursor inchange", 0, ctx.kbd_cursor);

    /* START cursor keyboard: ESC $3B $69 $59 $43 */
    vtx_process(&ctx, 0x1B); vtx_process(&ctx, 0x3B);
    vtx_process(&ctx, 0x69); vtx_process(&ctx, 0x59); vtx_process(&ctx, 0x43);
    ASSERT_EQ("apres START cursor: kbd_cursor ON", 1, ctx.kbd_cursor);
    ASSERT_EQ("apres START cursor: kbd_extended inchange", 1, ctx.kbd_extended);

    /* STOP extended: ESC $3B $6A $59 $41 */
    vtx_process(&ctx, 0x1B); vtx_process(&ctx, 0x3B);
    vtx_process(&ctx, 0x6A); vtx_process(&ctx, 0x59); vtx_process(&ctx, 0x41);
    ASSERT_EQ("apres STOP extended: kbd_extended OFF", 0, ctx.kbd_extended);

    /* Module non gere ($50 PRISE au lieu de $59 KEYBOARD_IN): ignore */
    unsigned char before_ext = ctx.kbd_extended;
    unsigned char before_cur = ctx.kbd_cursor;
    vtx_process(&ctx, 0x1B); vtx_process(&ctx, 0x3B);
    vtx_process(&ctx, 0x69); vtx_process(&ctx, 0x50); vtx_process(&ctx, 0x41);
    ASSERT_EQ("module $50 ignore: kbd_extended inchange", before_ext, ctx.kbd_extended);
    ASSERT_EQ("module $50 ignore: kbd_cursor inchange",   before_cur, ctx.kbd_cursor);

    /* Sub-cmd inconnue ($42): pas d'effet */
    vtx_process(&ctx, 0x1B); vtx_process(&ctx, 0x3B);
    vtx_process(&ctx, 0x69); vtx_process(&ctx, 0x59); vtx_process(&ctx, 0x42);
    ASSERT_EQ("sub-cmd $42 inconnue: kbd_extended inchange", before_ext, ctx.kbd_extended);
    ASSERT_EQ("sub-cmd $42 inconnue: kbd_cursor inchange",   before_cur, ctx.kbd_cursor);

    /* Sync OK */
    vtx_set_cursor(&ctx, 5, 0);
    vtx_process(&ctx, 'P');
    ASSERT_EQ("P affiche apres PRO3 START/STOP", 'P', ctx.screen[5][0].ch);
}

/* ===================================================================
 *  Test: ACK PRO3 SWITCH ON/OFF
 *
 *  Format: ESC $3B $63 + dest + status_byte (5 octets).
 *  Status: bit6 fixe + bits selon les liens "src -> dest" connus.
 * =================================================================== */
static void test_pro3_switch_ack(void)
{
    vtx_context_t ctx;
    printf("Test: ACK PRO3 SWITCH\n");
    vtx_init(&ctx);
    /* Defaut: AIG_MDM_TO_SCR + AIG_KBD_TO_MDM ON */

    /* Demande: SWITCH ON KBD->ECRAN. Apres, dest=ECRAN status doit
     * indiquer KBD->SCR (bit1) ET MDM->SCR (bit2) actifs. */
    tx_reset();
    vtx_process(&ctx, 0x1B); vtx_process(&ctx, 0x3B);
    vtx_process(&ctx, 0x61); vtx_process(&ctx, 0x58); vtx_process(&ctx, 0x51);
    ASSERT_EQ("ACK ON KBD->SCR: 5 octets", 5, tx_len);
    ASSERT_EQ("ACK: ESC", 0x1B, tx_buf[0]);
    ASSERT_EQ("ACK: PRO3 $3B", 0x3B, tx_buf[1]);
    ASSERT_EQ("ACK: code $63", 0x63, tx_buf[2]);
    ASSERT_EQ("ACK: module dest = SCR", 0x58, tx_buf[3]);
    ASSERT_EQ("ACK: status = $46 (bit6+bit2+bit1)",
              0x40 | 0x04 | 0x02, tx_buf[4]);

    /* Demande: SWITCH OFF MDM->SCR. Apres, status SCR garde KBD->SCR
     * (bit1) mais plus MDM->SCR (bit2). */
    tx_reset();
    vtx_process(&ctx, 0x1B); vtx_process(&ctx, 0x3B);
    vtx_process(&ctx, 0x60); vtx_process(&ctx, 0x58); vtx_process(&ctx, 0x59);
    ASSERT_EQ("ACK OFF MDM->SCR: status = $42 (bit6+bit1)",
              0x40 | 0x02, tx_buf[4]);

    /* Demande sur dest = MODEM. Defaut: KBD->MDM ON (bit1). */
    tx_reset();
    vtx_process(&ctx, 0x1B); vtx_process(&ctx, 0x3B);
    vtx_process(&ctx, 0x61); vtx_process(&ctx, 0x59);
    vtx_process(&ctx, 0x58);  /* SCR -> MDM */
    ASSERT_EQ("ACK ON SCR->MDM: dest = MDM", 0x59, tx_buf[3]);
    ASSERT_EQ("ACK ON SCR->MDM: status = $43 (bit6+bit1+bit0)",
              0x40 | 0x02 | 0x01, tx_buf[4]);

    /* Couple non gere: aucun ACK emis */
    tx_reset();
    vtx_process(&ctx, 0x1B); vtx_process(&ctx, 0x3B);
    vtx_process(&ctx, 0x61); vtx_process(&ctx, 0x50); vtx_process(&ctx, 0x51);
    ASSERT_EQ("couple inconnu: 0 octet emis", 0, tx_len);
}

/* ===================================================================
 *  Test: G2 etendu - symboles CEPT et accents majuscules
 * =================================================================== */
static void test_g2_extended(void)
{
    vtx_context_t ctx;
    printf("Test: G2 etendu (CEPT + maj accentuees)\n");

    /* Symboles SS2 standalone: SS2 ($19) + code G2 -> CHARSET_G2 */
    vtx_init(&ctx);
    vtx_process(&ctx, 0x19); vtx_process(&ctx, 0x24);  /* $ */
    ASSERT_EQ("G2: $24 = $", 0x24, ctx.screen[1][0].ch);
    ASSERT_EQ("G2: $24 charset=G2", CHARSET_G2, ctx.screen[1][0].charset);

    vtx_process(&ctx, 0x19); vtx_process(&ctx, 0x26);  /* # */
    ASSERT_EQ("G2: $26 = #", 0x26, ctx.screen[1][1].ch);

    vtx_process(&ctx, 0x19); vtx_process(&ctx, 0x3D);  /* 1/2 */
    ASSERT_EQ("G2: $3D = 1/2", 0x3D, ctx.screen[1][2].ch);

    vtx_process(&ctx, 0x19); vtx_process(&ctx, 0x6A);  /* OE majuscule */
    ASSERT_EQ("G2: $6A = OE maj", 0x6A, ctx.screen[1][3].ch);

    vtx_process(&ctx, 0x19); vtx_process(&ctx, 0x7A);  /* oe minuscule */
    ASSERT_EQ("G2: $7A = oe min", 0x7A, ctx.screen[1][4].ch);

    /* Accents majuscules: SS2 + accent + lettre majuscule */
    vtx_init(&ctx);
    /* À = SS2 grave A */
    vtx_process(&ctx, 0x19); vtx_process(&ctx, 0x41); vtx_process(&ctx, 0x41);
    ASSERT_EQ("SS2 grave+A: code interne $8D",
              0x8D, ctx.screen[1][0].ch);
    ASSERT_EQ("SS2 grave+A: charset=G2",
              CHARSET_G2, ctx.screen[1][0].charset);

    /* É = SS2 aigu E */
    vtx_process(&ctx, 0x19); vtx_process(&ctx, 0x42); vtx_process(&ctx, 0x45);
    ASSERT_EQ("SS2 aigu+E: code interne $8E", 0x8E, ctx.screen[1][1].ch);

    /* Ê = SS2 circ E */
    vtx_process(&ctx, 0x19); vtx_process(&ctx, 0x43); vtx_process(&ctx, 0x45);
    ASSERT_EQ("SS2 circ+E: code interne $90", 0x90, ctx.screen[1][2].ch);

    /* Ç = SS2 cedille C */
    vtx_process(&ctx, 0x19); vtx_process(&ctx, 0x4B); vtx_process(&ctx, 0x43);
    ASSERT_EQ("SS2 cedille+C: code interne $92", 0x92, ctx.screen[1][3].ch);

    /* Verifier que font_get_g2 retourne un glyphe non-vide pour chaque code */
    extern const unsigned char* font_get_g2(unsigned char ch);
    {
        unsigned char codes[] = {0x24, 0x26, 0x3C, 0x3D, 0x3E, 0x6A, 0x7A,
                                 0x8D, 0x8E, 0x8F, 0x90, 0x91, 0x92, 0x93};
        unsigned int i;
        for (i = 0; i < sizeof(codes); ++i) {
            const unsigned char* g = font_get_g2(codes[i]);
            int has_pixels = 0;
            int j;
            for (j = 0; j < 8; ++j) if (g[j]) { has_pixels = 1; break; }
            ASSERT_EQ("font_get_g2 retourne glyphe non-vide", 1, has_pixels);
        }
    }
}

/* ===================================================================
 *  Test: Mask global (ESC # $20 $58/$5F)
 * =================================================================== */
static void test_mask_global(void)
{
    vtx_context_t ctx;
    printf("Test: Mask global\n");
    vtx_init(&ctx);

    ASSERT_EQ("defaut: global_mask = 1 (cache)", 1, ctx.global_mask);
    ASSERT_EQ("defaut: g_global_mask = 1", 1, g_global_mask);

    /* SET (ESC # $20 $58) - reste a 1 */
    vtx_process(&ctx, 0x1B); vtx_process(&ctx, 0x23);
    vtx_process(&ctx, 0x20); vtx_process(&ctx, 0x58);
    ASSERT_EQ("apres SET: global_mask = 1", 1, ctx.global_mask);
    ASSERT_EQ("apres SET: state = NORMAL", VTX_STATE_NORMAL, ctx.state);

    /* RESET (ESC # $20 $5F) - bascule a 0 */
    vtx_process(&ctx, 0x1B); vtx_process(&ctx, 0x23);
    vtx_process(&ctx, 0x20); vtx_process(&ctx, 0x5F);
    ASSERT_EQ("apres RESET: global_mask = 0", 0, ctx.global_mask);
    ASSERT_EQ("apres RESET: g_global_mask = 0", 0, g_global_mask);

    /* SET de nouveau - bascule a 1 */
    vtx_process(&ctx, 0x1B); vtx_process(&ctx, 0x23);
    vtx_process(&ctx, 0x20); vtx_process(&ctx, 0x58);
    ASSERT_EQ("re-SET: global_mask = 1", 1, ctx.global_mask);

    /* Sequence mal formee: ESC # mais sans $20 -> retour NORMAL */
    vtx_process(&ctx, 0x1B); vtx_process(&ctx, 0x23);
    vtx_process(&ctx, 0x41);  /* pas $20 */
    ASSERT_EQ("ESC # sans $20: state = NORMAL", VTX_STATE_NORMAL, ctx.state);
    ASSERT_EQ("ESC # mal forme: global_mask inchange", 1, ctx.global_mask);

    /* Sync OK apres */
    vtx_set_cursor(&ctx, 5, 0);
    vtx_process(&ctx, 'M');
    ASSERT_EQ("M affiche apres mask global", 'M', ctx.screen[5][0].ch);

    /* Sequence inconnue dans MASK_END (ni $58 ni $5F): retour NORMAL,
     * global_mask inchange */
    vtx_process(&ctx, 0x1B); vtx_process(&ctx, 0x23);
    vtx_process(&ctx, 0x20); vtx_process(&ctx, 0x42);
    ASSERT_EQ("MASK_END inconnu: state = NORMAL", VTX_STATE_NORMAL, ctx.state);
    ASSERT_EQ("MASK_END inconnu: global_mask = 1 (toujours)",
              1, ctx.global_mask);
}

/* ===================================================================
 *  Test: SEP en reception (etat dedie, pas de dispatch PRO)
 * =================================================================== */
static void test_sep_reception(void)
{
    vtx_context_t ctx;
    printf("Test: SEP reception (etat dedie)\n");
    vtx_init(&ctx);

    /* SEP + code fonction classique ($41): consomme, pas d'action */
    tx_reset();
    vtx_process(&ctx, 0x13);
    ASSERT_EQ("SEP: etat = SEP", VTX_STATE_SEP, ctx.state);
    vtx_process(&ctx, 0x41);
    ASSERT_EQ("SEP $41: retour NORMAL", VTX_STATE_NORMAL, ctx.state);
    ASSERT_EQ("SEP $41: rien emis", 0, tx_len);

    /* Regression: SEP + $7B ne doit PAS declencher la reponse ENQROM
     * (l'ancien code reutilisait le mecanisme PRO1, et dispatch_pro
     * repondait SOH..EOT sur $7B). */
    tx_reset();
    vtx_process(&ctx, 0x13);
    vtx_process(&ctx, 0x7B);
    ASSERT_EQ("SEP $7B: retour NORMAL", VTX_STATE_NORMAL, ctx.state);
    ASSERT_EQ("SEP $7B: pas de reponse ENQROM", 0, tx_len);

    /* Le code fonction n'est pas affiche */
    vtx_set_cursor(&ctx, 5, 0);
    vtx_process(&ctx, 0x13);
    vtx_process(&ctx, 0x46);
    vtx_process(&ctx, 'X');
    ASSERT_EQ("apres SEP, X affiche en (5,0)", 'X', ctx.screen[5][0].ch);
}

/* ===================================================================
 *  Test: re-init resynchronise g_global_mask
 * =================================================================== */
static void test_reinit_global_mask(void)
{
    vtx_context_t ctx;
    printf("Test: vtx_init resynchronise g_global_mask\n");
    vtx_init(&ctx);

    /* Lever le mask via le flux serveur */
    vtx_process(&ctx, 0x1B); vtx_process(&ctx, 0x23);
    vtx_process(&ctx, 0x20); vtx_process(&ctx, 0x5F);
    ASSERT_EQ("mask leve: g_global_mask = 0", 0, g_global_mask);

    /* Re-init: les deux copies doivent revenir a 1 */
    vtx_init(&ctx);
    ASSERT_EQ("re-init: ctx.global_mask = 1", 1, ctx.global_mask);
    ASSERT_EQ("re-init: g_global_mask = 1", 1, g_global_mask);
}

/* ===================================================================
 *  Test: double hauteur marque dirty la ligne du dessus
 * =================================================================== */
static void test_double_height_dirty(void)
{
    vtx_context_t ctx;
    printf("Test: double hauteur -> dirty ligne au-dessus\n");
    vtx_init(&ctx);

    /* Simuler un rendu: nettoyer les dirty flags */
    memset(ctx.dirty, 0, sizeof(ctx.dirty));
    ctx.full_refresh = 0;

    /* Caractere double hauteur en ligne 5 */
    vtx_set_cursor(&ctx, 5, 0);
    vtx_process(&ctx, 0x1B); vtx_process(&ctx, 0x4D);  /* ESC $4D = double hauteur */
    vtx_process(&ctx, 'A');
    ASSERT_EQ("ligne 5 dirty", 1, ctx.dirty[5]);
    ASSERT_EQ("ligne 4 dirty (moitie haute du glyphe)", 1, ctx.dirty[4]);

    /* Double taille: meme exigence */
    memset(ctx.dirty, 0, sizeof(ctx.dirty));
    vtx_set_cursor(&ctx, 8, 0);
    vtx_process(&ctx, 0x1B); vtx_process(&ctx, 0x4F);  /* ESC $4F = double taille */
    vtx_process(&ctx, 'B');
    ASSERT_EQ("ligne 8 dirty", 1, ctx.dirty[8]);
    ASSERT_EQ("ligne 7 dirty (moitie haute)", 1, ctx.dirty[7]);

    /* Taille normale: la ligne du dessus reste propre */
    memset(ctx.dirty, 0, sizeof(ctx.dirty));
    vtx_set_cursor(&ctx, 10, 0);
    vtx_process(&ctx, 0x1B); vtx_process(&ctx, 0x4C);  /* ESC $4C = normal */
    vtx_process(&ctx, 'C');
    ASSERT_EQ("ligne 10 dirty", 1, ctx.dirty[10]);
    ASSERT_EQ("ligne 9 non dirty (taille normale)", 0, ctx.dirty[9]);
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
    test_esc_resync();
    test_pro_sequences();
    test_enqrom();
    test_pro2_lowercase();
    test_pro2_rolling();
    test_pro3_aiguillage();
    test_pro2_mode_protocole();
    test_pro2_status_keyboard();
    test_pro3_start_stop_module();
    test_pro3_switch_ack();
    test_g2_extended();
    test_mask_global();
    test_sep_reception();
    test_reinit_global_mask();
    test_double_height_dirty();

    printf("\n=== Resultats: %d/%d passes", tests_passed, tests_run);
    if (tests_failed > 0) {
        printf(", %d echecs", tests_failed);
    }
    printf(" ===\n");

    return tests_failed > 0 ? 1 : 0;
}
