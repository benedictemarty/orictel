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

/* ===================================================================
 *  Initialisation
 * =================================================================== */

void vtx_init(vtx_context_t* ctx)
{
    memset(ctx, 0, sizeof(vtx_context_t));

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

    vtx_clear_page(ctx);
    vtx_clear_status(ctx);
    ctx->full_refresh = 1;
}

/* ===================================================================
 *  Gestion ecran
 * =================================================================== */

static void clear_row(vtx_context_t* ctx, unsigned char row)
{
    unsigned char i;
    unsigned char* p;
    memset(&ctx->screen[row][0], 0, sizeof(vtx_cell_t) * VTX_COLS);
    /* Corriger fg=WHITE (sinon encre noire = invisible) */
    p = (unsigned char*)&ctx->screen[row][0];
    for (i = 0; i < VTX_COLS; ++i) {
        p[2] = VTX_WHITE;
        p += sizeof(vtx_cell_t);
    }
    ctx->dirty[row] = 1;
}

void vtx_clear_page(vtx_context_t* ctx)
{
    /* Clear rapide: memset a zero puis corriger fg=WHITE.
     * memset(0) met fg=VTX_BLACK (0) ce qui rend les cellules
     * invisibles (encre noire sur fond noir). Il FAUT mettre
     * fg=VTX_WHITE pour que le renderer utilise l'encre blanche. */
    memset(&ctx->screen[1][0], 0,
           sizeof(vtx_cell_t) * VTX_COLS * (VTX_ROWS - 1));

    /* Corriger fg=WHITE pour toutes les cellules effacees.
     * vtx_cell_t = {ch, charset, fg, bg, flags, size} → fg a offset 2 */
    {
        unsigned char* p = (unsigned char*)&ctx->screen[1][0];
        unsigned int i;
        for (i = 0; i < VTX_COLS * (VTX_ROWS - 1); ++i) {
            p[2] = VTX_WHITE;  /* fg = blanc */
            p += sizeof(vtx_cell_t);
        }
    }

    /* Effacer le framebuffer HIRES d'un coup (8000 octets = $40)
     * au lieu de marquer dirty et re-rendre 1000 cellules vides */
    display_clear();
    memset(&ctx->dirty[1], 0, VTX_ROWS - 1);

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

static void put_char(vtx_context_t* ctx, unsigned char ch, unsigned char cs)
{
    vtx_cell_t* cell;

    if (ctx->cur_y >= VTX_ROWS || ctx->cur_x >= VTX_COLS) {
        return;
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

    ctx->dirty[ctx->cur_y] = 1;
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
            /* Mode page (defaut): retour en ligne 1 (pas 0 = status) */
            ctx->cur_y = 1;
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
    }
    /* Mode page: pas de scroll, curseur reste en ligne 24 */
}

/* ===================================================================
 *  Effacement partiel
 * =================================================================== */

static void clear_eol(vtx_context_t* ctx)
{
    unsigned char i;
    for (i = ctx->cur_x; i < VTX_COLS; ++i) {
        ctx->screen[ctx->cur_y][i].ch = ' ';
        ctx->screen[ctx->cur_y][i].charset = CHARSET_G0;
        ctx->screen[ctx->cur_y][i].fg = VTX_WHITE;
        ctx->screen[ctx->cur_y][i].bg = VTX_BLACK;
        ctx->screen[ctx->cur_y][i].flags = 0;
        ctx->screen[ctx->cur_y][i].size = SIZE_NORMAL;
    }
    ctx->dirty[ctx->cur_y] = 1;
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

    /* PRO1: ESC $39 */
    if (byte == 0x39) {
        ctx->state = VTX_STATE_PRO1;
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

    /* Accumuler les parametres (chiffres et ;) */
    if ((byte >= '0' && byte <= '9') || byte == ';') {
        if (ctx->csi_len < sizeof(ctx->csi_buf) - 1) {
            ctx->csi_buf[ctx->csi_len++] = byte;
        }
        return;
    }

    /* Terminer: parser le parametre */
    ctx->csi_buf[ctx->csi_len] = 0;
    param = 0;
    if (ctx->csi_len > 0) {
        unsigned char i;
        for (i = 0; i < ctx->csi_len && ctx->csi_buf[i] != ';'; ++i) {
            param = param * 10 + (ctx->csi_buf[i] - '0');
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
        case 'H':   /* CUP - position curseur (row;col) */
            vtx_set_cursor(ctx, 1, 0);  /* Home par defaut */
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
                    if (byte == 0x65) acc_ch = 0x80; /* e' */
                    break;
                case 0x41: /* grave */
                    if (byte == 0x65) acc_ch = 0x81; /* e` */
                    else if (byte == 0x61) acc_ch = 0x83; /* a` */
                    else if (byte == 0x75) acc_ch = 0x84; /* u` */
                    break;
                case 0x43: /* circonflexe */
                    if (byte == 0x65) acc_ch = 0x82; /* e^ */
                    else if (byte == 0x61) acc_ch = 0x86; /* a^ */
                    else if (byte == 0x69) acc_ch = 0x87; /* i^ */
                    else if (byte == 0x6F) acc_ch = 0x88; /* o^ */
                    else if (byte == 0x75) acc_ch = 0x89; /* u^ */
                    break;
                case 0x48: /* trema */
                    if (byte == 0x65) acc_ch = 0x8A; /* e" */
                    else if (byte == 0x69) acc_ch = 0x8B; /* i" */
                    else if (byte == 0x75) acc_ch = 0x8C; /* u" */
                    break;
                case 0x4B: /* cedille */
                    if (byte == 0x63) acc_ch = 0x85; /* c, */
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

    case VTX_STATE_PRO1:
        /* PRO1: on ignore pour l'instant, attendre 1 octet */
        ctx->state = VTX_STATE_PRO2;
        return;

    case VTX_STATE_PRO2:
        /* PRO2: terminer la sequence PRO */
        ctx->state = VTX_STATE_NORMAL;
        return;

    default:
        break;
    }

    /* --- Etat NORMAL --- */

    /* Codes de controle C0 ($00-$1F) */
    if (byte < 0x20) {
        switch (byte) {
            case 0x05:  /* ENQ - identification terminal */
                serial_send(0x01);  /* SOH */
                serial_send(0x7B);  /* Constructeur (Matra) */
                serial_send(0x74);  /* Type (Minitel 1B) */
                serial_send(0x63);  /* Version */
                serial_send(0x04);  /* EOT */
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
                /* Le prochain octet est le code fonction ($41-$49) */
                /* En reception, on l'ignore (c'est le serveur qui envoie) */
                /* En emission, c'est keyboard_process qui l'envoie */
                ctx->state = VTX_STATE_PRO1;  /* Consommer 1 octet */
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
