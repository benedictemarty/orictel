/**
 * @file ui.c
 * @brief Helpers d'affichage et de saisie des menus OricTel (cf. ui.h).
 *
 * Centralise le motif d'ecriture ecran jusque-la duplique des dizaines de fois
 * dans les menus (splash, mode, interface, serveur, WiFi, modem) et la saisie
 * de texte. Bornes ecran garanties ici (anti-debordement, revue qualite #2-#5).
 */

#include "ui.h"
#include "keyboard.h"   /* keyboard_scan, KEY_* */
#include "display.h"    /* display_render_all */

void ui_print(vtx_context_t* ctx, unsigned char row,
              unsigned char col, const char* s, unsigned char fg)
{
    unsigned char i;
    /* Clip sur la largeur ecran: une chaine dont col+longueur depasse
     * VTX_COLS deborderait sinon sur la ligne suivante (UB / corruption). */
    for (i = 0; s[i] && (col + i) < VTX_COLS; ++i) {
        ctx->screen[row][col + i].ch = s[i];
        ctx->screen[row][col + i].fg = fg;
    }
    ctx->dirty[row] = 1;
}

void ui_menu_item(vtx_context_t* ctx, unsigned char row, const char* s)
{
    ui_print(ctx, row, 12, s, VTX_YELLOW);
    ctx->screen[row][12].fg = VTX_CYAN;
}

unsigned char ui_text_input(vtx_context_t* ctx, unsigned char row,
                            unsigned char col, char* buf,
                            unsigned char bufsize, unsigned char mask)
{
    unsigned char pos = 0;
    unsigned char maxlen = bufsize - 1;
    if ((unsigned)col + maxlen > VTX_COLS) maxlen = (unsigned char)(VTX_COLS - col);
    for (;;) {
        unsigned char key = keyboard_scan();
        if (key == KEY_NONE) continue;
        if ((key & KEY_FUNC_FLAG) && (key & 0x7F) == KEY_ENVOI) {
            buf[pos] = 0;
            return pos;
        } else if ((key & KEY_FUNC_FLAG) && (key & 0x7F) == KEY_ANNULATION) {
            buf[0] = 0;
            return 0xFF;                          /* annulation */
        } else if (key == 0x7F || key == 0x08 ||
                   ((key & KEY_FUNC_FLAG) && (key & 0x7F) == KEY_CORRECTION)) {
            if (pos > 0) {
                --pos;
                ctx->screen[row][col + pos].ch = ' ';
                ctx->dirty[row] = 1;
                display_render_all(ctx);
            }
        } else if (key >= 0x20 && key < 0x7F && pos < maxlen) {
            buf[pos] = key;
            ctx->screen[row][col + pos].ch = mask ? mask : key;
            ctx->screen[row][col + pos].fg = VTX_GREEN;
            ctx->dirty[row] = 1;
            display_render_all(ctx);
            ++pos;
        }
    }
}
