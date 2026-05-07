/**
 * @file videotex.h
 * @brief Decodeur protocole Videotex Teletel/Antiope pour Minitel 1B
 *
 * Machine a etats pour interpreter le flux d'octets Videotex recu
 * via la liaison serie. Reference: emulateur JS miedit/telenet.
 *
 * Codes de controle principaux:
 *   $08-$0B : deplacement curseur
 *   $0C     : effacement ecran
 *   $0D     : retour chariot
 *   $0E     : basculer vers G1 (mosaiques)
 *   $0F     : basculer vers G0 (alphanumerique)
 *   $12     : repetition caractere
 *   $1B     : ESC (sequences d'attributs)
 *   $1F     : US (positionnement curseur)
 */

#ifndef VIDEOTEX_H
#define VIDEOTEX_H

/* Dimensions ecran Minitel */
#define VTX_COLS    40
#define VTX_ROWS    25      /* ligne 0 = statut, lignes 1-24 = contenu */

/* Etats de la machine a etats */
#define VTX_STATE_NORMAL    0
#define VTX_STATE_ESC       1
#define VTX_STATE_CSI       2
#define VTX_STATE_US_ROW    3
#define VTX_STATE_US_COL    4
#define VTX_STATE_SS2       5
#define VTX_STATE_REP       6
#define VTX_STATE_PRO       7   /* Sequence PRO en cours (cf. pro_remaining) */
#define VTX_STATE_SS2_ACC   9   /* SS2 accent: attente du caractere base */

/* Jeux de caracteres */
#define CHARSET_G0  0       /* Alphanumerique */
#define CHARSET_G1  1       /* Mosaiques semi-graphiques */
#define CHARSET_G2  2       /* Supplementaire (accents) */

/* Attributs de taille */
#define SIZE_NORMAL         0
#define SIZE_DOUBLE_HEIGHT  1
#define SIZE_DOUBLE_WIDTH   2
#define SIZE_DOUBLE_SIZE    3

/* Drapeaux d'attributs (bitfield) */
#define ATTR_FLASH      0x01
#define ATTR_CONCEALED  0x02
#define ATTR_INVERT     0x04
#define ATTR_UNDERLINE  0x08
#define ATTR_SEPARATED  0x10

/* Couleurs Minitel (identiques aux couleurs Oric, prefixe VTX_) */
#define VTX_BLACK       0
#define VTX_RED         1
#define VTX_GREEN       2
#define VTX_YELLOW      3
#define VTX_BLUE        4
#define VTX_MAGENTA     5
#define VTX_CYAN        6
#define VTX_WHITE       7

/* Structure d'une cellule ecran */
typedef struct {
    unsigned char ch;           /* Code caractere */
    unsigned char charset;      /* CHARSET_G0, G1, G2 */
    unsigned char fg;           /* Couleur encre (0-7) */
    unsigned char bg;           /* Couleur fond (0-7) */
    unsigned char flags;        /* ATTR_FLASH | ATTR_CONCEALED | ... */
    unsigned char size;         /* SIZE_NORMAL, DOUBLE_HEIGHT, etc. */
} vtx_cell_t;

/* Contexte du decodeur Videotex */
typedef struct {
    /* Machine a etats */
    unsigned char state;

    /* Position curseur */
    unsigned char cur_x;        /* Colonne (0-39) */
    unsigned char cur_y;        /* Ligne (0-24) */
    unsigned char cur_visible;

    /* Jeu de caracteres courant */
    unsigned char charset;      /* G0 ou G1 actif */

    /* Attributs courants */
    unsigned char fg_color;
    unsigned char bg_color;
    unsigned char attr_flags;
    unsigned char attr_size;

    /* Attributs en attente (serial attributes Videotex) */
    unsigned char pending_bg;
    unsigned char pending_underline;
    unsigned char has_pending;

    /* Buffer parametres CSI */
    unsigned char csi_buf[8];
    unsigned char csi_len;

    /* Ligne memorisee pour US */
    unsigned char us_row;

    /* Caractere precedent (pour REP) */
    unsigned char last_char;
    unsigned char last_charset;

    /* Code accent SS2 en cours ($41=grave, $42=aigu, $43=circ, $48=trema, $4B=cedille) */
    unsigned char ss2_accent;

    /* Sequence PRO en cours: nombre d'octets restant a consommer.
     * PRO1 (ESC $39) = 1 octet, PRO2 (ESC $3A) = 2 octets, PRO3 (ESC $3B) = 3 */
    unsigned char pro_remaining;

    /* Buffer ecran (40x25 cellules) */
    vtx_cell_t screen[VTX_ROWS][VTX_COLS];

    /* Dirty flags et effacement paresseux */
    unsigned char dirty[VTX_ROWS];      /* 1 = ligne modifiee, a re-rendre */
    unsigned char row_cleared[VTX_ROWS]; /* 1 = ligne logiquement vide (lazy) */
    unsigned char full_refresh;         /* 1 = tout redessiner */
    unsigned char blink_phase;          /* 0 ou 1, bascule toutes les ~500ms */
} vtx_context_t;

/**
 * Initialise le contexte Videotex.
 */
void vtx_init(vtx_context_t* ctx);

/**
 * Traite un octet recu du flux Videotex.
 * @param ctx Contexte du decodeur
 * @param byte Octet Videotex (7 bits)
 */
void vtx_process(vtx_context_t* ctx, unsigned char byte);

/**
 * Efface l'ecran (contenu, lignes 1-24).
 */
void vtx_clear_page(vtx_context_t* ctx);

/**
 * Efface la ligne de statut (ligne 0).
 */
void vtx_clear_status(vtx_context_t* ctx);

/**
 * Positionne le curseur.
 */
void vtx_set_cursor(vtx_context_t* ctx, unsigned char row, unsigned char col);

#endif /* VIDEOTEX_H */
