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
        } else if (key == KEY_LOCAL_ESCAPE ||
                   ((key & KEY_FUNC_FLAG) && (key & 0x7F) == KEY_ANNULATION)) {
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

/* ===================================================================
 *  Charte des ecrans locaux (v0.3.23, reprise de NeoTel 0.9.1 ; voir ui.h
 *  pour l'adaptation aux attributs serie du HIRES Oric)
 * =================================================================== */

static unsigned char ui_len(const char* s)
{
    unsigned char n = 0;
    while (s[n]) ++n;
    return n;
}

void ui_fill(vtx_context_t* ctx, unsigned char row, unsigned char bg)
{
    vtx_cell_t* c = &ctx->screen[row][0];
    unsigned char i;
    for (i = 0; i < VTX_COLS; ++i, ++c) {
        c->ch = ' ';
        c->charset = CHARSET_G0;
        c->fg = VTX_WHITE;
        c->bg = bg;
        c->flags = 0;
        c->size = SIZE_NORMAL;
    }
    ctx->dirty[row] = 1;
}

void ui_rule(vtx_context_t* ctx, unsigned char row, unsigned char fg)
{
    vtx_cell_t* c;
    unsigned char i;
    ui_fill(ctx, row, VTX_BLACK);
    c = &ctx->screen[row][1];           /* colonne 0 : attribut d'encre */
    for (i = 1; i < VTX_COLS; ++i, ++c) {
        c->ch = 0x60;                   /* trait (mosaique) */
        c->charset = CHARSET_G1;
        c->fg = fg;
    }
}

void ui_print_right(vtx_context_t* ctx, unsigned char row, const char* s,
                    unsigned char fg)
{
    unsigned char n = ui_len(s);
    ui_print(ctx, row, (unsigned char)(n < 38 ? 38 - n : 0), s, fg);
}

void ui_header(vtx_context_t* ctx, const char* title, const char* right)
{
    unsigned char i;
    vtx_cell_t* c = &ctx->screen[UI_ROW_HEADER][2];
    ui_fill(ctx, UI_ROW_HEADER - 1, UI_BAND_BG);
    ui_fill(ctx, UI_ROW_HEADER, UI_BAND_BG);
    for (i = 0; title[i] && i < 12; ++i, c += 2) {
        c->ch = title[i];
        c->size = SIZE_DOUBLE_WIDTH;
    }
    if (right) ui_print_right(ctx, UI_ROW_HEADER, right, VTX_YELLOW);
    ui_rule(ctx, UI_ROW_RULE, VTX_CYAN);
}

void ui_footer(vtx_context_t* ctx, const char* left, const char* right)
{
    ui_rule(ctx, UI_ROW_FOOTRULE, VTX_WHITE);
    ui_fill(ctx, UI_ROW_FOOTER, VTX_BLACK);
    if (left) ui_print(ctx, UI_ROW_FOOTER, 2, left, VTX_GREEN);
    if (right) ui_print_right(ctx, UI_ROW_FOOTER, right, VTX_WHITE);
}

void ui_item(vtx_context_t* ctx, unsigned char row, char key,
             const char* label, const char* value, unsigned char sel)
{
    vtx_cell_t* c = &ctx->screen[row][0];
    unsigned char i, n;

    ui_fill(ctx, row, VTX_BLACK);
    if (sel) {
        for (i = 1; i < VTX_COLS - 1; ++i) c[i].bg = UI_BAND_BG;
    }
    c[UI_ITEM_COL].ch = '[';
    c[UI_ITEM_COL + 1].ch = key;
    c[UI_ITEM_COL + 2].ch = ']';
    for (i = UI_ITEM_COL; i < UI_ITEM_COL + 3; ++i) {
        c[i].fg = sel ? VTX_WHITE : VTX_CYAN;
    }
    ui_print(ctx, row, UI_ITEM_COL + 4, label, sel ? VTX_WHITE : VTX_YELLOW);
    if (value) {
        /* n = premiere colonne apres l'espace qui suit le libelle. Libelle
         * long (SSID) : la valeur le suit apres un espace, au lieu de
         * l'ecraser en colonne 22 ; ui_print la coupe a la colonne 39. */
        n = (unsigned char)(UI_ITEM_COL + 4 + ui_len(label) + 1);
        for (i = n; i < UI_VALUE_COL - 1; ++i) {
            c[i].ch = '.';
            c[i].fg = sel ? VTX_CYAN : VTX_BLUE;
        }
        ui_print(ctx, row, n > UI_VALUE_COL ? n : UI_VALUE_COL, value,
                 VTX_WHITE);
    }
}

unsigned char ui_nav(unsigned char key, unsigned char* sel, unsigned char n)
{
    if (key == KEY_ARROW_UP) {
        *sel = (unsigned char)(*sel ? *sel - 1 : n - 1);
        return 1;
    }
    if (key == KEY_ARROW_DOWN) {
        *sel = (unsigned char)(*sel + 1 < n ? *sel + 1 : 0);
        return 1;
    }
    if (key == KEY_ARROW_RIGHT) return 2;
    if (key & KEY_FUNC_FLAG) {
        key &= 0x7F;
        if (key == KEY_ENVOI || key == KEY_SUITE) return 2;
    }
    return 0;
}
