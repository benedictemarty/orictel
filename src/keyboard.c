/**
 * @file keyboard.c
 * @brief Module clavier Oric avec mapping Minitel
 *
 * Compatible Oric-1 ET Atmos.
 *
 * L'Oric-1 n'a PAS de touche FUNCT. Les deux machines ont CTRL.
 * On utilise CTRL+lettre pour les touches fonction Minitel:
 *
 *   CTRL+lettre genere un code controle = lettre & $1F:
 *     CTRL+A = $01, CTRL+C = $03, CTRL+E = $05,
 *     CTRL+G = $07, CTRL+N = $0E, CTRL+R = $12, CTRL+S = $13
 *
 * Sur Atmos, FUNCT+lettre est aussi supporte : la ROM le livre comme
 * UN code, la lettre avec le bit 7 leve (FUNCT+S = $F3), mesure sous
 * Phosphoric (ROM 1.1, v0.3.23). Jusqu'en v0.3.22, OricTel attendait un
 * prefixe $09 : or $09 est la FLECHE DROITE (cc65 atmos.h CH_CURS_RIGHT),
 * qui n'emettait donc rien, et FUNCT+S partait en "SEP s".
 *
 * Fleches (codes ROM, cc65 atmos.h) : gauche $08, droite $09, bas $0A,
 * haut $0B ; RETURN = $0D.
 *
 * Reference: matrice clavier Oric dans keyboard.c de Phosphoric.
 *   LCTRL = row 2, col 4 (Oric-1 et Atmos)
 *   RCTRL = row 0, col 4 (Oric-1 et Atmos)
 *   FUNCT = row 3, col 4 (Atmos uniquement)
 */

#ifdef TEST_HOST
/* En test host, kbhit()/cgetc() sont fournis par le harnais (clavier scripte)
 * a la place de la conio cc65. */
extern int kbhit(void);
extern int cgetc(void);
#else
#include <conio.h>
#endif
#include "keyboard.h"
#include "serial.h"

/* ===================================================================
 *  Variables internes
 * =================================================================== */


/* ===================================================================
 *  Initialisation
 * =================================================================== */

void keyboard_init(void)
{
}

/* ===================================================================
 *  Mapping CTRL+lettre -> touche fonction Minitel
 *
 *  CTRL+lettre genere: code_ascii = lettre & 0x1F
 *  Donc CTRL+A=$01, CTRL+C=$03, CTRL+E=$05, CTRL+G=$07,
 *       CTRL+N=$0E, CTRL+R=$12, CTRL+S=$13
 * =================================================================== */

static unsigned char map_ctrl_to_func(unsigned char ctrl_code)
{
    switch (ctrl_code) {
        case 0x01:  /* CTRL+A = Annulation */
            return KEY_FUNC_FLAG | KEY_ANNULATION;
        case 0x03:  /* CTRL+C = Connexion/Fin */
            return KEY_FUNC_FLAG | KEY_CONNEXION;
        case 0x05:  /* CTRL+E = Repetition (alias historique) */
            return KEY_FUNC_FLAG | KEY_REPETITION;
        case 0x07:  /* CTRL+G = Guide */
            return KEY_FUNC_FLAG | KEY_GUIDE;
        case 0x0E:  /* CTRL+N = Suite (alias historique ; usuel : fleche droite) */
            return KEY_FUNC_FLAG | KEY_SUITE;
        case 0x12:  /* CTRL+R = Repetition (usage Minitel, comme NeoTel 0.8.1 ;
                     * Retour = fleche gauche ou haut). Etait Retour jusqu'en
                     * v0.3.21. */
            return KEY_FUNC_FLAG | KEY_REPETITION;
        case 0x13:  /* CTRL+S = Sommaire */
            return KEY_FUNC_FLAG | KEY_SOMMAIRE;
        default:
            return KEY_NONE;
    }
}

/* ===================================================================
 *  Mapping FUNCT+lettre -> touche fonction (Atmos uniquement)
 * =================================================================== */

static unsigned char map_funct_to_func(unsigned char ch)
{
    switch (ch) {
        case 'r': case 'R':
            return KEY_FUNC_FLAG | KEY_RETOUR;
        case 'e': case 'E':
            return KEY_FUNC_FLAG | KEY_REPETITION;
        case 'g': case 'G':
            return KEY_FUNC_FLAG | KEY_GUIDE;
        case 'a': case 'A':
            return KEY_FUNC_FLAG | KEY_ANNULATION;
        case 's': case 'S':
            return KEY_FUNC_FLAG | KEY_SOMMAIRE;
        case 'n': case 'N':
            return KEY_FUNC_FLAG | KEY_SUITE;
        case 'c': case 'C':
            return KEY_FUNC_FLAG | KEY_CONNEXION;
        default:
            return KEY_NONE;
    }
}

/* ===================================================================
 *  Scan clavier
 *
 *  Utilise kbhit()/cgetc() de cc65 pour la compatibilite
 *  Oric-1 et Atmos.
 * =================================================================== */

unsigned char keyboard_pending(void)
{
    return kbhit() ? 1 : 0;
}

/* Fenetre de "silence" (en iterations de polling) au-dela de laquelle on
 * considere la touche physiquement relachee. Doit depasser l'intervalle
 * d'auto-repeat de la ROM Oric. Ajustable si le defilement persiste
 * (augmenter) ou si la transition entre menus traine (diminuer). */
#define KBD_QUIET_MAX 8000u

void keyboard_flush(void)
{
    /* Purge le tampon clavier ROM PUIS attend un relachement stable.
     *
     * Deux problemes resolus:
     *  1) Burst accumule pendant une phase sans lecture clavier (timeouts
     *     AT de plusieurs secondes): sans purge, ces frappes se vident d'un
     *     coup en entrant dans la boucle session et "deroulent" les ecrans.
     *  2) Auto-repeat ROM entre deux menus enchaines: tant que la touche de
     *     selection reste enfoncee, la ROM la repete et l'ecran suivant la
     *     consomme aussitot. On attend donc le relachement effectif.
     *
     * A appeler a l'entree de chaque menu et avant la boucle session. */
    unsigned int quiet = 0;
    unsigned char saw = 0;

    /* 1) Vider ce qui est deja en tampon. */
    while (kbhit()) {
        cgetc();
        saw = 1;
    }

    /* 2) Si rien n'attendait, NE PAS imposer de pause: cas normal d'une
     *    transition d'ecran sans touche maintenue. */
    if (!saw) {
        return;
    }

    /* 3) Des frappes etaient presentes (touche maintenue / burst): attendre
     *    un silence stable = relachement effectif (l'auto-repeat ROM cesse). */
    while (quiet < KBD_QUIET_MAX) {
        if (kbhit()) {
            cgetc();
            quiet = 0;
        } else {
            ++quiet;
        }
    }
}

unsigned char keyboard_scan(void)
{
    unsigned char ch;
    unsigned char func_key;

    if (!kbhit()) {
        return KEY_NONE;
    }

    ch = cgetc();

    /* --- FUNCT+lettre (Atmos) : lettre | $80 --- */
    if (ch & 0x80) {
        return map_funct_to_func(ch & 0x7F);
    }

    /* --- Touches speciales (AVANT le handler CTRL !) ---
     * $08, $0A, $0B, $0D sont dans la range $01-$1A mais ne sont
     * PAS des CTRL+lettre : ce sont des touches speciales. */
    switch (ch) {
        case 0x0D:  /* RETURN (CR) = Envoi */
            return KEY_FUNC_FLAG | KEY_ENVOI;

        case 0x0A:  /* Fleche BAS : navigation des menus ; en session
                     * ENVOI (ou CSI B en mode curseur), keyboard_process */
            return KEY_ARROW_DOWN;

        case 0x0B:  /* Fleche HAUT : navigation des menus ; en session
                     * RETOUR (ou CSI A en mode curseur) */
            return KEY_ARROW_UP;

        case 0x08:  /* Fleche GAUCHE (BS) */
            return KEY_ARROW_LEFT;

        case 0x09:  /* Fleche DROITE (code ROM, cc65 CH_CURS_RIGHT) */
        case 0x15:  /* ancien code suppose de la fleche droite, garde en
                     * alias. Traites ICI, avant le handler CTRL+lettre
                     * ($01-$1A), sinon ils y seraient avales. */
            return KEY_ARROW_RIGHT;

        case 0x7F:  /* DELETE = Correction */
            return KEY_FUNC_FLAG | KEY_CORRECTION;

        case 0x1B:  /* ESC = sortie locale (quitter la session, retour dans
                     * les menus). N'est PLUS l'ANNULATION Minitel, qui reste
                     * sur CTRL+A : une touche de secours doit toujours faire
                     * la meme chose, quel que soit l'ecran. */
            return KEY_LOCAL_ESCAPE;

        default:
            break;
    }

    /* --- CTRL+lettre (Oric-1 ET Atmos) ---
     * APRES les touches speciales pour ne pas les avaler. */
    if (ch >= 0x01 && ch <= 0x1A) {
        if (ch == 0x04) return KEY_TOGGLE_RENDER;  /* CTRL+D */
        if (ch == 0x0C) return KEY_LOCAL_CLEAR;    /* CTRL+L */
        if (ch == 0x06) return KEY_LOCAL_RESET;    /* CTRL+F */
        func_key = map_ctrl_to_func(ch);
        if (func_key != KEY_NONE) {
            return func_key;
        }
        return KEY_NONE;
    }

    /* --- Caractere ASCII normal --- */
    return ch;
}

/* ===================================================================
 *  Emission des codes Minitel selon les aiguillages PRO3
 * =================================================================== */

/* Route un octet clavier selon les aiguillages du Minitel:
 * CLAVIER->MODEM (defaut ON): envoi serie.
 * CLAVIER->ECRAN (defaut OFF): echo local via le decodeur Videotex. */
static void kbd_emit(vtx_context_t* ctx, unsigned char byte)
{
    if (ctx->aiguillages & AIG_KBD_TO_MDM) {
        serial_send(byte);
    }
    if (ctx->aiguillages & AIG_KBD_TO_SCR) {
        vtx_process(ctx, byte);
    }
}

void keyboard_process(vtx_context_t* ctx, unsigned char key)
{
    if (key == KEY_NONE) {
        return;
    }

    /* Fleches : en mode curseur (PRO3 START $59 $43) elles emettent
     * CSI A/B/C/D, comme sur un Minitel 1B reel ; hors mode curseur,
     * gauche = RETOUR et droite = SUITE (raccourcis de NeoTel 0.8.1,
     * v0.3.22). Codes KEY_ARROW_DOWN..KEY_ARROW_LEFT contigus ($F7-$FB,
     * ESC $F9 au milieu, exclu). */
    if (key >= KEY_ARROW_DOWN && key <= KEY_ARROW_LEFT && key != KEY_LOCAL_ESCAPE) {
        static const unsigned char csi_code[5] = { 0x42, 0x41, 0, 0x43, 0x44 };
        static const unsigned char func_code[5] = {
            KEY_ENVOI, KEY_RETOUR, 0, KEY_SUITE, KEY_RETOUR
        };
        unsigned char i = key - KEY_ARROW_DOWN;
        if (ctx->kbd_cursor) {
            kbd_emit(ctx, 0x1B);
            kbd_emit(ctx, 0x5B);
            kbd_emit(ctx, csi_code[i]);
            return;
        }
        /* Hors mode curseur : bas = ENVOI et haut = RETOUR (comportement
         * historique d'OricTel), gauche = RETOUR, droite = SUITE. */
        key = KEY_FUNC_FLAG | func_code[i];
    }

    /* Touche fonction Minitel */
    if (key & KEY_FUNC_FLAG) {
        unsigned char func_code = key & 0x7F;
        kbd_emit(ctx, SEP);          /* Separateur $13 */
        kbd_emit(ctx, func_code);    /* Code fonction ($41-$49) */
        return;
    }

    /* Caractere ASCII normal - emettre tel quel (7 bits) */
    kbd_emit(ctx, key & 0x7F);
}
