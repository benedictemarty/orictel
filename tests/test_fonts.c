/**
 * @file test_fonts.c
 * @brief Tests des glyphes jointifs G0 / G2 (STUM 1B, STUM 2 annexes 3.6, 3.9)
 *
 * Un caractere jointif touche le bord de la case (6 x 8, bits 5-0) pour se
 * raccorder a son voisin : barres continues sur deux cases, diagonales qui
 * se prolongent d'une case a l'autre, fleches jointes a l'oppose de la
 * pointe. Compile avec le compilateur host.
 */

#include <stdio.h>
#include "fonts.h"

static int tests_run = 0;
static int tests_passed = 0;

#define ASSERT_EQ(msg, expected, actual) do { \
    ++tests_run; \
    if ((expected) == (actual)) { ++tests_passed; } \
    else { printf("  FAIL: %s (attendu 0x%02X, obtenu 0x%02X)\n", \
                   msg, (unsigned)(expected), (unsigned)(actual)); } \
} while (0)

#define COL_BIT(c) (0x20 >> (c))    /* colonne 0 = bit 5 (gauche) */

static void test_bars(void)
{
    const unsigned char* g;
    unsigned char l, ok;

    printf("Test: barres jointives G0\n");
    /* Verticales : meme colonne sur les 8 lignes (continuite entre rangees) */
    g = font_get_g0(0x7B); ok = 1; for (l = 0; l < 8; ++l) ok &= (g[l] == COL_BIT(1));
    ASSERT_EQ("$7B barre gauche col. 1 sur 8 lignes", 1, ok);
    g = font_get_g0(0x7C); ok = 1; for (l = 0; l < 8; ++l) ok &= (g[l] == COL_BIT(3));
    ASSERT_EQ("$7C barre mediane col. 3 sur 8 lignes", 1, ok);
    g = font_get_g0(0x7D); ok = 1; for (l = 0; l < 8; ++l) ok &= (g[l] == COL_BIT(5));
    ASSERT_EQ("$7D barre droite col. 5 sur 8 lignes", 1, ok);
    /* Horizontales : 6 pixels (continuite entre colonnes) */
    ASSERT_EQ("$7E barre haute ligne 1", 0x3F, font_get_g0(0x7E)[1]);
    ASSERT_EQ("$60 barre mediane ligne 3", 0x3F, font_get_g0(0x60)[3]);
    ASSERT_EQ("$60 sur la ligne du tiret", 1, font_get_g0('-')[3] != 0);
    ASSERT_EQ("$5F barre basse ligne 6", 0x3F, font_get_g0(0x5F)[6]);
    ASSERT_EQ("$5F ligne 7 libre (soulignement)", 0x00, font_get_g0(0x5F)[7]);
    g = font_get_g0(0x7F); ok = 1; for (l = 0; l < 8; ++l) ok &= (g[l] == 0x3F);
    ASSERT_EQ("$7F pave plein", 1, ok);
}

static void test_diagonals_arrows(void)
{
    const unsigned char* g;
    const unsigned char* h;
    unsigned char l, ok;

    printf("Test: diagonales et fleches jointives\n");
    g = font_get_g0(0x2F);
    ASSERT_EQ("/ coin haut-droit", COL_BIT(5), g[0]);
    ASSERT_EQ("/ coin bas-gauche", COL_BIT(0), g[7]);
    g = font_get_g0(0x5C);
    ASSERT_EQ("\\ coin haut-gauche", COL_BIT(0), g[0]);
    ASSERT_EQ("\\ coin bas-droit", COL_BIT(5), g[7]);
    /* Un pixel par ligne : les deux diagonales sont symetriques */
    ok = 1;
    for (l = 0; l < 8; ++l) ok &= (font_get_g0(0x2F)[l] == font_get_g0(0x5C)[7 - l]);
    ASSERT_EQ("/ et \\ symetriques", 1, ok);

    g = font_get_g0(0x5E);
    ASSERT_EQ("$5E fleche : pointe en ligne 0", COL_BIT(3), g[0]);
    ASSERT_EQ("$5E fleche : hampe jointe en ligne 7 (axe de $7C)", font_get_g0(0x7C)[7], g[7]);

    h = font_get_g2(0x2D);
    ok = 1; for (l = 0; l < 8; ++l) ok &= (h[l] == g[l]);
    ASSERT_EQ("G2 fleche haut == G0 $5E", 1, ok);
    h = font_get_g2(0x2F);
    ASSERT_EQ("G2 fleche bas : hampe jointe en ligne 0", COL_BIT(3), h[0]);
    ASSERT_EQ("G2 fleche bas : pointe en ligne 7", COL_BIT(3), h[7]);
    h = font_get_g2(0x2C);
    ASSERT_EQ("G2 fleche gauche : tige ligne 3 (axe de $60)", 0x3F, h[3]);
    ASSERT_EQ("G2 fleche gauche : pointe col. 0", COL_BIT(0), h[3] & COL_BIT(0));
    h = font_get_g2(0x2E);
    ASSERT_EQ("G2 fleche droite : tige ligne 3 (axe de $60)", 0x3F, h[3]);
    ASSERT_EQ("G2 fleche droite : pas de pointe a gauche", 0x00, h[2] & COL_BIT(0));
}

int main(void)
{
    printf("=== OricTel - Tests glyphes jointifs ===\n\n");
    test_bars();
    test_diagonals_arrows();
    printf("\n=== Resultats: %d/%d passes ===\n", tests_passed, tests_run);
    return (tests_passed == tests_run) ? 0 : 1;
}
