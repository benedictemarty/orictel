/**
 * @file videotex.c
 * @brief Decodeur protocole Videotex Teletel/Antiope
 *
 * Machine a etats complete pour interpreter le flux Videotex Minitel.
 * Reference: emulateur JS miedit (protocol.js, decoder.js, constant.js)
 */

#include <string.h>
#include "videotex.h"
#include "display.h"
#include "serial.h"

/* Mask global Videotex (defini dans main.c, lu par display.c).
 * 1 = cacher cellules ATTR_CONCEALED, 0 = les rendre visibles. */
extern unsigned char g_global_mask;

/* ===================================================================
 *  Identification terminal (ENQ et PRO1 ENQROM)
 *  Reponse STUM 1B: SOH + constructeur + type + version + EOT
 * =================================================================== */

/* Reponse d'identification ENQ/ENQROM - DESACTIVEE.
 *
 * La STUM 1B demande de repondre SOH + constructeur + type + version
 * + EOT, mais les serveurs modernes (MiniPavi/PAVI) ne CONSOMMENT pas
 * cette reponse: ils l'echoient comme une frappe utilisateur, et
 * "{tc" apparait dans le champ de saisie (verifie a la trace serie:
 * le serveur place le curseur, active l'echo, puis echoie nos octets
 * $7B $74 $63 - meme avec une reponse partie en 9 ms).
 * miedit, l'emulateur de reference qui fonctionne avec ces serveurs,
 * ne repond a AUCUNE sequence PRO. On s'aligne.
 * Reactivable ici si un serveur exigeant l'identification apparait. */
static void send_ident(void)
{
    /* serial_send(0x01); SOH
     * serial_send(0x7B); constructeur (Matra)
     * serial_send(0x74); type (Minitel 1B)
     * serial_send(0x63); version
     * serial_send(0x04); EOT
     * serial_tx_flush(); */
}

/* ===================================================================
 *  Dirty spans
 * =================================================================== */

void vtx_touch(vtx_context_t* ctx, unsigned char row,
               unsigned char col_from, unsigned char col_to)
{
    if (row >= VTX_ROWS) {
        return;
    }
    if (!ctx->dirty[row]) {
        ctx->dirty[row] = 1;
        ctx->dirty_min[row] = col_from;
        ctx->dirty_max[row] = col_to;
    } else {
        if (col_from < ctx->dirty_min[row]) ctx->dirty_min[row] = col_from;
        if (col_to   > ctx->dirty_max[row]) ctx->dirty_max[row] = col_to;
    }
}

/* Retablit l'invariant "ligne propre = span plein" sur une plage de
 * lignes: un dirty[row]=1 pose sans vtx_touch rendra la ligne entiere. */
static void reset_spans(vtx_context_t* ctx, unsigned char from_row)
{
    unsigned char r;
    for (r = from_row; r < VTX_ROWS; ++r) {
        ctx->dirty_min[r] = 0;
        ctx->dirty_max[r] = VTX_COLS - 1;
    }
}

/* ===================================================================
 *  Initialisation
 * =================================================================== */

void vtx_init(vtx_context_t* ctx)
{
    memset(ctx, 0, sizeof(vtx_context_t));
    reset_spans(ctx, 0);

    ctx->state = VTX_STATE_NORMAL;
    ctx->cur_x = 0;
    ctx->cur_y = 1;    /* Ligne 1 (ligne 0 = statut) */
    ctx->cur_visible = 1;
    ctx->charset = CHARSET_G0;
    ctx->fg_color = VTX_WHITE;
    ctx->bg_color = VTX_BLACK;
    ctx->attr_flags = 0;
    ctx->attr_size = SIZE_NORMAL;
    ctx->pending_bg = VTX_BLACK;
    ctx->pending_underline = 0;
    ctx->has_pending = 0;
    ctx->rolling_mode = 0;
    ctx->lowercase_mode = 0;
    ctx->terminal_mode = TERM_MODE_VIDEOTEX;
    /* Aiguillages defaut Minitel 1B: MODEM->ECRAN et CLAVIER->MODEM */
    ctx->aiguillages = AIG_MDM_TO_SCR | AIG_KBD_TO_MDM;
    ctx->kbd_extended = 0;
    ctx->kbd_cursor = 0;
    ctx->global_mask = 1;  /* defaut: cellules concealed cachees */
    g_global_mask = 1;     /* garder la copie renderer synchronisee */

    vtx_clear_page(ctx);
    vtx_clear_status(ctx);
    ctx->full_refresh = 1;
}

/* ===================================================================
 *  Gestion ecran
 * =================================================================== */

/* Remet une plage de cellules a l'etat "vide visible":
 * ch=' ' et fg=WHITE (fg=BLACK rendrait la cellule invisible,
 * encre noire sur fond noir). */
static void reset_cells(vtx_cell_t* cell, unsigned int count)
{
    while (count-- > 0) {
        cell->ch = ' ';
        cell->charset = CHARSET_G0;
        cell->fg = VTX_WHITE;
        cell->bg = VTX_BLACK;
        cell->flags = 0;
        cell->size = SIZE_NORMAL;
        ++cell;
    }
}

static void clear_row(vtx_context_t* ctx, unsigned char row)
{
    reset_cells(&ctx->screen[row][0], VTX_COLS);
    vtx_touch(ctx, row, 0, VTX_COLS - 1);
}

void vtx_clear_page(vtx_context_t* ctx)
{
    reset_cells(&ctx->screen[1][0], VTX_COLS * (VTX_ROWS - 1));

    /* Effacer le framebuffer HIRES d'un coup (8000 octets = $40)
     * au lieu de marquer dirty et re-rendre 1000 cellules vides */
    display_clear();
    memset(&ctx->dirty[1], 0, VTX_ROWS - 1);
    reset_spans(ctx, 1);

    ctx->cur_x = 0;
    ctx->cur_y = 1;
    ctx->charset = CHARSET_G0;
    ctx->fg_color = VTX_WHITE;
    ctx->bg_color = VTX_BLACK;
    ctx->attr_flags = 0;
    ctx->attr_size = SIZE_NORMAL;
    ctx->pending_bg = VTX_BLACK;
    ctx->has_pending = 0;
}

void vtx_clear_status(vtx_context_t* ctx)
{
    clear_row(ctx, 0);
}

void vtx_set_cursor(vtx_context_t* ctx, unsigned char row, unsigned char col)
{
    if (row < VTX_ROWS) {
        ctx->cur_y = row;
    }
    if (col < VTX_COLS) {
        ctx->cur_x = col;
    }
}

/* ===================================================================
 *  Ecriture d'un caractere a la position curseur
 * =================================================================== */

static void scroll_up(vtx_context_t* ctx);

static void put_char(vtx_context_t* ctx, unsigned char ch, unsigned char cs)
{
    vtx_cell_t* cell;

    if (ctx->cur_y >= VTX_ROWS || ctx->cur_x >= VTX_COLS) {
        return;
    }

    /* Mode majuscule force (defaut Minitel 1B) : 'a'-'z' -> 'A'-'Z'.
     * Ne s'applique qu'au jeu G0 (alphanumerique). G1 mosaique et
     * G2 supplementaire ne sont pas affectes. */
    if (cs == CHARSET_G0 && !ctx->lowercase_mode &&
        ch >= 'a' && ch <= 'z') {
        ch -= 32;
    }

    cell = &ctx->screen[ctx->cur_y][ctx->cur_x];
    cell->ch = ch;
    cell->charset = cs;
    cell->fg = ctx->fg_color;
    cell->bg = ctx->bg_color;
    cell->flags = ctx->attr_flags;
    cell->size = ctx->attr_size;

    /* Appliquer les attributs en attente sur un delimiteur (espace G0) */
    if (ch == 0x20 && cs == CHARSET_G0 && ctx->has_pending) {
        cell->bg = ctx->pending_bg;
        if (ctx->pending_underline) {
            cell->flags |= ATTR_UNDERLINE;
        }
        ctx->bg_color = ctx->pending_bg;
        if (ctx->pending_underline) {
            ctx->attr_flags |= ATTR_UNDERLINE;
        } else {
            ctx->attr_flags &= ~ATTR_UNDERLINE;
        }
        ctx->has_pending = 0;
    }

    /* Marquer la plage modifiee: la cellule, +1 colonne en double
     * largeur/taille (moitie droite du glyphe) */
    {
        unsigned char span_end = ctx->cur_x;
        if ((ctx->attr_size == SIZE_DOUBLE_WIDTH ||
             ctx->attr_size == SIZE_DOUBLE_SIZE) &&
            span_end < VTX_COLS - 1) {
            ++span_end;
        }
        vtx_touch(ctx, ctx->cur_y, ctx->cur_x, span_end);
        /* Double hauteur/taille: la moitie haute du glyphe est rendue
         * dans les lignes pixel de la ligne du dessus. Sans ce dirty,
         * un re-rendu isole de cur_y-1 ecraserait la moitie haute. */
        if ((ctx->attr_size == SIZE_DOUBLE_HEIGHT ||
             ctx->attr_size == SIZE_DOUBLE_SIZE) && ctx->cur_y > 0) {
            vtx_touch(ctx, ctx->cur_y - 1, ctx->cur_x, span_end);
        }
    }
    ctx->last_char = ch;
    ctx->last_charset = cs;

    /* Avancer le curseur (2 colonnes pour double largeur/taille) */
    if (ctx->attr_size == SIZE_DOUBLE_WIDTH ||
        ctx->attr_size == SIZE_DOUBLE_SIZE) {
        ctx->cur_x += 2;
    } else {
        ctx->cur_x++;
    }
    if (ctx->cur_x >= VTX_COLS) {
        ctx->cur_x = 0;
        ctx->cur_y++;
        if (ctx->cur_y >= VTX_ROWS) {
            if (ctx->rolling_mode) {
                scroll_up(ctx);
                ctx->cur_y = VTX_ROWS - 1;
            } else {
                /* Mode page (defaut): retour en ligne 1 (pas 0 = status) */
                ctx->cur_y = 1;
            }
        }
    }
}

/* ===================================================================
 *  Deplacement curseur
 * =================================================================== */

static void cursor_left(vtx_context_t* ctx)
{
    if (ctx->cur_x > 0) {
        ctx->cur_x--;
    } else if (ctx->cur_y > 1) {
        ctx->cur_x = VTX_COLS - 1;
        ctx->cur_y--;
    }
}

static void cursor_right(vtx_context_t* ctx)
{
    ctx->cur_x++;
    if (ctx->cur_x >= VTX_COLS) {
        ctx->cur_x = 0;
        ctx->cur_y++;
        if (ctx->cur_y >= VTX_ROWS) {
            ctx->cur_y = VTX_ROWS - 1;
        }
    }
}

static void cursor_up(vtx_context_t* ctx)
{
    if (ctx->cur_y > 1) {
        ctx->cur_y--;
    }
}

static void cursor_down(vtx_context_t* ctx)
{
    if (ctx->cur_y < VTX_ROWS - 1) {
        ctx->cur_y++;
    } else if (ctx->rolling_mode) {
        /* Mode rouleau: scroll d'une ligne, curseur reste en bas */
        scroll_up(ctx);
    }
    /* Mode page: pas de scroll, curseur reste en ligne 24 */
}

static void scroll_up(vtx_context_t* ctx)
{
    /* Decale les lignes 2..VTX_ROWS-1 vers 1..VTX_ROWS-2.
     * La ligne 0 (statut) est preservee. La derniere ligne est effacee.
     * Cout: ~5.5 Ko de memmove + full_refresh (~80ms a 1 MHz). */
    memmove(&ctx->screen[1][0], &ctx->screen[2][0],
            sizeof(vtx_cell_t) * VTX_COLS * (VTX_ROWS - 2));
    clear_row(ctx, VTX_ROWS - 1);
    ctx->full_refresh = 1;
}

/* ===================================================================
 *  Effacement partiel
 * =================================================================== */

static void clear_eol(vtx_context_t* ctx)
{
    reset_cells(&ctx->screen[ctx->cur_y][ctx->cur_x],
                VTX_COLS - ctx->cur_x);
    vtx_touch(ctx, ctx->cur_y, ctx->cur_x, VTX_COLS - 1);
}

static void clear_eos(vtx_context_t* ctx)
{
    unsigned char r;
    clear_eol(ctx);
    for (r = ctx->cur_y + 1; r < VTX_ROWS; ++r) {
        clear_row(ctx, r);
    }
}

/* ===================================================================
 *  Traitement ESC sequences
 * =================================================================== */

static void process_esc(vtx_context_t* ctx, unsigned char byte)
{
    /* Couleur encre: ESC $40-$47 */
    if (byte >= 0x40 && byte <= 0x47) {
        ctx->fg_color = byte - 0x40;
        ctx->state = VTX_STATE_NORMAL;
        return;
    }

    /* Flash on/off: ESC $48/$49 */
    if (byte == 0x48) {
        ctx->attr_flags |= ATTR_FLASH;
        ctx->state = VTX_STATE_NORMAL;
        return;
    }
    if (byte == 0x49) {
        ctx->attr_flags &= ~ATTR_FLASH;
        ctx->state = VTX_STATE_NORMAL;
        return;
    }

    /* Taille: ESC $4C-$4F */
    if (byte >= 0x4C && byte <= 0x4F) {
        ctx->attr_size = byte - 0x4C;
        ctx->state = VTX_STATE_NORMAL;
        return;
    }

    /* Couleur fond: ESC $50-$57 */
    if (byte >= 0x50 && byte <= 0x57) {
        /* Fond = attribut serie, mis en attente */
        ctx->pending_bg = byte - 0x50;
        ctx->has_pending = 1;
        ctx->state = VTX_STATE_NORMAL;
        return;
    }

    /* Masquage: ESC $58 */
    if (byte == 0x58) {
        ctx->attr_flags |= ATTR_CONCEALED;
        ctx->state = VTX_STATE_NORMAL;
        return;
    }

    /* Soulignement (G0) / Mosaique separee (G1): ESC $59=off, $5A=on */
    if (byte == 0x59) {
        if (ctx->charset == CHARSET_G1) {
            /* Clear separated mosaic mode */
            ctx->attr_flags &= ~ATTR_SEPARATED;
        } else {
            ctx->pending_underline = 0;
            ctx->has_pending = 1;
        }
        ctx->state = VTX_STATE_NORMAL;
        return;
    }
    if (byte == 0x5A) {
        if (ctx->charset == CHARSET_G1) {
            /* Set separated mosaic mode */
            ctx->attr_flags |= ATTR_SEPARATED;
        } else {
            ctx->pending_underline = 1;
            ctx->has_pending = 1;
        }
        ctx->state = VTX_STATE_NORMAL;
        return;
    }

    /* Mask global: ESC $23 $20 $58/$5F (set/reset)
     * Reference: miedit (constant.js mask-global) */
    if (byte == 0x23) {
        ctx->state = VTX_STATE_MASK_SP;
        return;
    }

    /* CSI: ESC $5B */
    if (byte == 0x5B) {
        ctx->state = VTX_STATE_CSI;
        ctx->csi_len = 0;
        return;
    }

    /* Inversion: ESC $5C=OFF (fond normal), $5D=ON (fond inverse)
     * Ref: telenet emulateur.js, miedit directStream */
    if (byte == 0x5C) {
        ctx->attr_flags &= ~ATTR_INVERT;
        ctx->state = VTX_STATE_NORMAL;
        return;
    }
    if (byte == 0x5D) {
        ctx->attr_flags |= ATTR_INVERT;
        ctx->state = VTX_STATE_NORMAL;
        return;
    }

    /* PRO1: ESC $39 + 1 octet (commande)
     * PRO2: ESC $3A + 2 octets (commande + parametre)
     * PRO3: ESC $3B + 3 octets (commande + 2 parametres)
     * Reference: STUM 1B (specification technique du Minitel) */
    if (byte >= 0x39 && byte <= 0x3B) {
        ctx->state = VTX_STATE_PRO;
        ctx->pro_kind = byte - 0x38;  /* $39->1, $3A->2, $3B->3 */
        ctx->pro_idx = 0;
        return;
    }

    /* SS2 (G2 single shift): ESC $19 */
    if (byte == 0x19) {
        ctx->state = VTX_STATE_SS2;
        return;
    }

    /* Non reconnu: ignorer et revenir a NORMAL */
    ctx->state = VTX_STATE_NORMAL;
}

/* ===================================================================
 *  Traitement CSI sequences (ESC [ params commande)
 * =================================================================== */

static void process_csi(vtx_context_t* ctx, unsigned char byte)
{
    unsigned char param;
    unsigned char param2;

    /* Accumuler les parametres (chiffres et ;) */
    if ((byte >= '0' && byte <= '9') || byte == ';') {
        if (ctx->csi_len < sizeof(ctx->csi_buf) - 1) {
            ctx->csi_buf[ctx->csi_len++] = byte;
        }
        return;
    }

    /* Terminer: parser param1[;param2] */
    ctx->csi_buf[ctx->csi_len] = 0;
    param = 0;
    param2 = 0;
    {
        unsigned char i;
        for (i = 0; i < ctx->csi_len && ctx->csi_buf[i] != ';'; ++i) {
            param = param * 10 + (ctx->csi_buf[i] - '0');
        }
        if (i < ctx->csi_len && ctx->csi_buf[i] == ';') {
            for (++i; i < ctx->csi_len && ctx->csi_buf[i] != ';'; ++i) {
                param2 = param2 * 10 + (ctx->csi_buf[i] - '0');
            }
        }
    }
    if (param == 0) param = 1;

    switch (byte) {
        case 'A':   /* CUU - curseur haut */
            while (param-- > 0) cursor_up(ctx);
            break;
        case 'B':   /* CUD - curseur bas */
            while (param-- > 0) cursor_down(ctx);
            break;
        case 'C':   /* CUF - curseur droite */
            while (param-- > 0) cursor_right(ctx);
            break;
        case 'D':   /* CUB - curseur gauche */
            while (param-- > 0) cursor_left(ctx);
            break;
        case 'H':   /* CUP - position curseur (row;col, 1-based).
                     * Sans parametre: home (1,0). row mappe direct sur
                     * la grille vtx (ligne 0 = statut, inaccessible:
                     * param=0 est force a 1 plus haut). col 1-based ->
                     * 0-based; vtx_set_cursor clampe row/col invalides. */
            vtx_set_cursor(ctx, param, (param2 > 0) ? param2 - 1 : 0);
            break;
        case 'J':   /* ED - effacer ecran */
            if (param == 2 || ctx->csi_len == 0) {
                vtx_clear_page(ctx);
            } else {
                clear_eos(ctx);
            }
            break;
        case 'K':   /* EL - effacer ligne */
            clear_eol(ctx);
            break;
        case 'h':   /* Mode set (curseur visible, etc.) */
            ctx->cur_visible = 1;
            break;
        case 'l':   /* Mode reset (curseur invisible) */
            ctx->cur_visible = 0;
            break;
        default:
            break;
    }

    ctx->state = VTX_STATE_NORMAL;
}

/* ===================================================================
 *  Dispatch d'une sequence PRO complete (PRO1/PRO2/PRO3)
 *
 *  Appele quand pro_idx == pro_kind. pro_buf[0..pro_kind-1] contient
 *  les octets de la sequence. Reagit aux commandes connues, ignore
 *  les autres en preservant le sync.
 * =================================================================== */

static void dispatch_pro(vtx_context_t* ctx)
{
    /* PRO1: 1 octet de commande */
    if (ctx->pro_kind == 1) {
        switch (ctx->pro_buf[0]) {
            case 0x7B:  /* ENQROM - identification du Minitel.
                         * Meme reponse que ENQ ($05). */
                send_ident();
                break;
            default:
                /* SEP fonction et autres PRO1 inconnus: ignore */
                break;
        }
        return;
    }
    /* PRO2: 2 octets = action ($69=START / $6A=STOP) + cible.
     * Cibles connues:
     *   $43 = mode rouleau (scroll en bas d'ecran)
     *   $45 = mode minuscules (autoriser 'a'-'z' au lieu de forcer majuscule)
     * Reference: STUM 1B + miedit (constant.js) + eMinitel (Functionalities.cpp) */
    if (ctx->pro_kind == 2) {
        unsigned char on;

        /* PRO2 + $72 + module = demande de status d'un module.
         * Format reponse: ESC ($1B) + $3B + $73 + module + status byte
         * (= PRO3 + $73 + ...). Reference: STUM 1B + eMinitel.
         * Les modules courants sont $59 (KEYBOARD_IN) et $51 (KEYBOARD_OUT).
         * Le status byte reflete l'etat du clavier: bits 6-7 fixes par
         * convention, plus quelques flags optionnels (minuscules etc.). */
        if (ctx->pro_buf[0] == 0x72) {
            unsigned char target = ctx->pro_buf[1];
            if (target == 0x59 || target == 0x51) {
                unsigned char status = 0xC0;  /* bits 6-7 fixes (cf. eMinitel) */
                if (ctx->lowercase_mode) status |= 0x02;
                if (ctx->rolling_mode)   status |= 0x04;
                serial_send(0x1B);
                serial_send(0x3B);
                serial_send(0x73);
                serial_send(target);
                serial_send(status);
                serial_tx_flush();  /* reponse protocole: partir d'un bloc */
            }
            return;
        }

        /* PRO2 + $32 = changement de mode protocole (VIDEOTEX/MIXED).
         * Reference: STUM 1B, eMinitel PRO2_MODE_VIDEOTEX/MIXED. */
        if (ctx->pro_buf[0] == 0x32) {
            if (ctx->pro_buf[1] == 0x7E) {       /* MODE VIDEOTEX */
                ctx->terminal_mode = TERM_MODE_VIDEOTEX;
                /* ACK: SEP ($13) + $71 (videotex confirme) */
                serial_send(0x13);
                serial_send(0x71);
                serial_tx_flush();
            } else if (ctx->pro_buf[1] == 0x7D) { /* MODE MIXED */
                /* OricTel ne supporte pas le mode MIXED (telé-informatique).
                 * On ignore silencieusement, le serveur saura par l'absence
                 * d'ACK que la bascule a echoue. */
            }
            return;
        }

        /* PRO2 + $69/$6A = START/STOP d'un mode (rolling, lowercase, ...) */
        if (ctx->pro_buf[0] == 0x69)      on = 1;  /* START */
        else if (ctx->pro_buf[0] == 0x6A) on = 0;  /* STOP */
        else return;
        switch (ctx->pro_buf[1]) {
            case 0x43:  /* ROLLING */
                ctx->rolling_mode = on;
                break;
            case 0x45:  /* LOWERCASE */
                ctx->lowercase_mode = on;
                break;
            default:
                /* Autres cibles PRO2 (PCE, etc.) : TODO */
                break;
        }
        return;
    }
    /* PRO3: 3 octets = action + cible/source.
     *   $60 SWITCH OFF + dest + source: rompt le lien source -> dest
     *   $61 SWITCH ON  + dest + source: etablit le lien source -> dest
     *   $69 START + xx + yy: active un module (TODO)
     *   $6A STOP + xx + yy: desactive un module (TODO)
     *
     * Codes module STUM 1B:
     *   $50 = PRISE peripherique
     *   $51 = CLAVIER
     *   $58 = ECRAN
     *   $59 = MODEM
     *
     * Reference: STUM 1B + miedit (constant.js pro3SwitchOn/Off)
     *            + eMinitel (Functionalities.cpp __func_PRO3) */
    if (ctx->pro_kind == 3) {
        unsigned char on;
        unsigned char dest, src, mask;
        /* PRO3 START/STOP module: $69/$6A + $59 (KEYBOARD_IN) + sub-cmd
         *   sub-cmd $41 = clavier etendu (touches alt)
         *   sub-cmd $43 = clavier curseur (fleches actives)
         * Reference: miedit pro3Start/Stop -> startKeyboardFunction. */
        if ((ctx->pro_buf[0] == 0x69 || ctx->pro_buf[0] == 0x6A)
            && ctx->pro_buf[1] == 0x59) {
            unsigned char on2 = (ctx->pro_buf[0] == 0x69);
            switch (ctx->pro_buf[2]) {
                case 0x41: ctx->kbd_extended = on2; break;
                case 0x43: ctx->kbd_cursor   = on2; break;
                default: break;
            }
            return;
        }

        if (ctx->pro_buf[0] == 0x61)      on = 1;  /* SWITCH ON */
        else if (ctx->pro_buf[0] == 0x60) on = 0;  /* SWITCH OFF */
        else return;  /* autres START/STOP: ignore */

        dest = ctx->pro_buf[1];
        src  = ctx->pro_buf[2];
        mask = 0;
        if (dest == 0x58 && src == 0x51) mask = AIG_KBD_TO_SCR;
        else if (dest == 0x58 && src == 0x59) mask = AIG_MDM_TO_SCR;
        else if (dest == 0x59 && src == 0x51) mask = AIG_KBD_TO_MDM;
        else if (dest == 0x59 && src == 0x58) mask = AIG_SCR_TO_MDM;
        else return;  /* couple non gere */

        if (on) ctx->aiguillages |= mask;
        else    ctx->aiguillages &= ~mask;

        /* ACK PRO3 SWITCH: ESC $3B $63 + dest + status_byte (5 octets).
         * Le status byte resume tous les liens "X -> dest" connus, avec
         * convention bit (cf. eMinitel __func_PRO3):
         *   bit 0 = ECRAN  -> dest
         *   bit 1 = CLAVIER -> dest
         *   bit 2 = MODEM  -> dest
         *   bit 3 = PRISE/DIN -> dest (non gere, toujours 0)
         *   bit 6 = $40 fixe (convention) */
        {
            unsigned char status = 0x40;
            if (dest == 0x58) {  /* dest = ECRAN */
                if (ctx->aiguillages & AIG_KBD_TO_SCR) status |= 0x02;
                if (ctx->aiguillages & AIG_MDM_TO_SCR) status |= 0x04;
            } else if (dest == 0x59) {  /* dest = MODEM */
                if (ctx->aiguillages & AIG_SCR_TO_MDM) status |= 0x01;
                if (ctx->aiguillages & AIG_KBD_TO_MDM) status |= 0x02;
            }
            serial_send(0x1B);
            serial_send(0x3B);
            serial_send(0x63);
            serial_send(dest);
            serial_send(status);
            serial_tx_flush();  /* reponse protocole: partir d'un bloc */
        }
    }
}

/* ===================================================================
 *  Traitement principal - point d'entree par octet
 * =================================================================== */

void vtx_process(vtx_context_t* ctx, unsigned char byte)
{
    /* Masquer bit 7 (7 bits Videotex) */
    byte &= 0x7F;

    /* Re-sync: un ESC ($1B) recu en milieu de sequence multi-octets
     * abandonne l'etat courant et redemarre une nouvelle sequence ESC.
     * $1B n'est jamais une valeur legitime dans les payloads US/SS2/PRO/CSI
     * (US: $40+ligne, SS2: $20-$7F, CSI param: '0'-'9'/';'), donc le voir
     * signifie qu'on a perdu le sync et qu'une nouvelle commande arrive. */
    if (byte == 0x1B && ctx->state != VTX_STATE_NORMAL
                    && ctx->state != VTX_STATE_ESC) {
        ctx->state = VTX_STATE_ESC;
        return;
    }

    switch (ctx->state) {

    case VTX_STATE_ESC:
        process_esc(ctx, byte);
        return;

    case VTX_STATE_CSI:
        process_csi(ctx, byte);
        return;

    case VTX_STATE_US_ROW:
        ctx->us_row = byte - 0x40;
        ctx->state = VTX_STATE_US_COL;
        return;

    case VTX_STATE_US_COL:
        /* Col = byte - $41. Si byte=$40, col=0 (pas -1).
         * Le Minitel utilise $40 pour col 0. */
        vtx_set_cursor(ctx, ctx->us_row,
                        (byte >= 0x41) ? (byte - 0x41) : 0);
        /* US reset tous les attributs (norme STUM p.91)
         * Ref: telenet emulateur.js lignes 785-795 */
        ctx->charset = CHARSET_G0;      /* modeG1 = false */
        ctx->fg_color = VTX_WHITE;      /* fgColor = 7 */
        ctx->bg_color = VTX_BLACK;      /* bgColor = 0 */
        ctx->attr_flags = 0;            /* souligne, inversion, clignotement = false */
        ctx->attr_size = SIZE_NORMAL;   /* taille = 0 */
        ctx->has_pending = 0;
        ctx->state = VTX_STATE_NORMAL;
        return;

    case VTX_STATE_SS2:
        /* Single shift G2: diacritiques ou caractere G2 standalone */
        if (byte >= 0x41 && byte <= 0x4F) {
            /* Code accent: sauver et attendre le caractere base */
            ctx->ss2_accent = byte;
            ctx->state = VTX_STATE_SS2_ACC;
            return;
        }
        /* Caractere G2 standalone (ex: $23=livre, $30=degre) */
        put_char(ctx, byte, CHARSET_G2);
        ctx->state = VTX_STATE_NORMAL;
        return;

    case VTX_STATE_SS2_ACC:
        /* Caractere base apres un code accent.
         * Combiner accent + base pour un glyphe accentue.
         * Les glyphes sont dans font_g2_extra[9..20].
         * On utilise CHARSET_G2 avec un code interne $80+. */
        {
            unsigned char acc_ch = 0;
            /* Mapper (accent, base) -> code interne G2 accentue */
            switch (ctx->ss2_accent) {
                case 0x42: /* aigu */
                    if (byte == 0x65)      acc_ch = 0x80; /* e' */
                    else if (byte == 0x45) acc_ch = 0x8E; /* E' */
                    break;
                case 0x41: /* grave */
                    if (byte == 0x65)      acc_ch = 0x81; /* e` */
                    else if (byte == 0x61) acc_ch = 0x83; /* a` */
                    else if (byte == 0x75) acc_ch = 0x84; /* u` */
                    else if (byte == 0x41) acc_ch = 0x8D; /* A` */
                    else if (byte == 0x45) acc_ch = 0x8F; /* E` */
                    break;
                case 0x43: /* circonflexe */
                    if (byte == 0x65)      acc_ch = 0x82; /* e^ */
                    else if (byte == 0x61) acc_ch = 0x86; /* a^ */
                    else if (byte == 0x69) acc_ch = 0x87; /* i^ */
                    else if (byte == 0x6F) acc_ch = 0x88; /* o^ */
                    else if (byte == 0x75) acc_ch = 0x89; /* u^ */
                    else if (byte == 0x41) acc_ch = 0x93; /* A^ */
                    else if (byte == 0x45) acc_ch = 0x90; /* E^ */
                    break;
                case 0x48: /* trema */
                    if (byte == 0x65)      acc_ch = 0x8A; /* e" */
                    else if (byte == 0x69) acc_ch = 0x8B; /* i" */
                    else if (byte == 0x75) acc_ch = 0x8C; /* u" */
                    else if (byte == 0x45) acc_ch = 0x91; /* E" */
                    break;
                case 0x4B: /* cedille */
                    if (byte == 0x63)      acc_ch = 0x85; /* c, */
                    else if (byte == 0x43) acc_ch = 0x92; /* C, */
                    break;
            }
            if (acc_ch) {
                put_char(ctx, acc_ch, CHARSET_G2);
            } else {
                /* Combinaison inconnue: afficher la lettre de base */
                put_char(ctx, byte, CHARSET_G0);
            }
        }
        ctx->state = VTX_STATE_NORMAL;
        return;

    case VTX_STATE_REP:
        /* Repeter le dernier caractere N fois */
        {
            unsigned char count = byte - 0x40;
            if (count > 40) count = 40;
            while (count-- > 0) {
                put_char(ctx, ctx->last_char, ctx->last_charset);
            }
        }
        ctx->state = VTX_STATE_NORMAL;
        return;

    case VTX_STATE_MASK_SP:
        /* Mask global: attend $20 (espace) apres ESC #. */
        ctx->state = (byte == 0x20) ? VTX_STATE_MASK_END : VTX_STATE_NORMAL;
        return;

    case VTX_STATE_MASK_END:
        /* Mask global: $58 = masquer (cacher concealed),
         *              $5F = demasquer (rendre concealed visible). */
        if (byte == 0x58) {
            ctx->global_mask = 1;
            g_global_mask = 1;
        } else if (byte == 0x5F) {
            ctx->global_mask = 0;
            g_global_mask = 0;
        }
        ctx->full_refresh = 1;
        ctx->state = VTX_STATE_NORMAL;
        return;

    case VTX_STATE_SEP:
        /* Code fonction apres SEP: consomme, aucune action. */
        ctx->state = VTX_STATE_NORMAL;
        return;

    case VTX_STATE_PRO:
        /* Accumule un octet de payload PRO. */
        if (ctx->pro_idx < 3) {
            ctx->pro_buf[ctx->pro_idx++] = byte;
        }
        if (ctx->pro_idx >= ctx->pro_kind) {
            dispatch_pro(ctx);
            ctx->state = VTX_STATE_NORMAL;
        }
        return;

    default:
        break;
    }

    /* --- Etat NORMAL --- */

    /* Codes de controle C0 ($00-$1F) */
    if (byte < 0x20) {
        switch (byte) {
            case 0x05:  /* ENQ - identification terminal */
                send_ident();
                break;
            case 0x07:  /* BEL - bip */
                display_beep();
                break;
            case 0x08:  /* BS - curseur gauche */
                cursor_left(ctx);
                break;
            case 0x09:  /* HT - curseur droite */
                cursor_right(ctx);
                break;
            case 0x0A:  /* LF - curseur bas */
                cursor_down(ctx);
                break;
            case 0x0B:  /* VT - curseur haut */
                cursor_up(ctx);
                break;
            case 0x0C:  /* FF - effacer ecran + home */
                vtx_clear_page(ctx);
                break;
            case 0x0D:  /* CR - retour chariot */
                ctx->cur_x = 0;
                break;
            case 0x0E:  /* SO - basculer G1 (mosaiques) */
                ctx->charset = CHARSET_G1;
                break;
            case 0x0F:  /* SI - basculer G0 (alphanumerique) */
                ctx->charset = CHARSET_G0;
                break;
            case 0x11:  /* DC1/CON - curseur visible */
                ctx->cur_visible = 1;
                break;
            case 0x12:  /* REP - repetition */
                ctx->state = VTX_STATE_REP;
                break;
            case 0x13:  /* SEP - separateur (touches fonction Minitel) */
                /* Le prochain octet est le code fonction ($41-$49).
                 * En reception, on le consomme sans agir (c'est le
                 * serveur qui envoie). Etat dedie: passer par le
                 * mecanisme PRO declencherait dispatch_pro (un SEP
                 * suivi de $7B repondrait ENQROM a tort). */
                ctx->state = VTX_STATE_SEP;
                break;
            case 0x14:  /* DC4/COFF - curseur invisible */
                ctx->cur_visible = 0;
                break;
            case 0x16:  /* SS2 - single shift G2 (accents) */
            case 0x19:  /* SS2 - single shift G2 (variante) */
                ctx->state = VTX_STATE_SS2;
                break;
            case 0x18:  /* CAN - effacer jusqu'a fin de ligne */
                clear_eol(ctx);
                break;
            case 0x1A:  /* SUB - substitution (affiche espace) */
                put_char(ctx, ' ', ctx->charset);
                break;
            case 0x1B:  /* ESC */
                ctx->state = VTX_STATE_ESC;
                break;
            case 0x1E:  /* RS - home (curseur en 1,0) */
                ctx->cur_x = 0;
                ctx->cur_y = 1;
                break;
            case 0x1F:  /* US - positionnement curseur */
                ctx->state = VTX_STATE_US_ROW;
                break;
            default:
                break;
        }
        return;
    }

    /* Caracteres affichables ($20-$7F) */
    put_char(ctx, byte, ctx->charset);
}
