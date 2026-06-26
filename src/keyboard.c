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
 * Sur Atmos, FUNCT+lettre est aussi supporte (Tab = $09 suivi
 * de la lettre), pour les utilisateurs habitues a cette convention.
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

static unsigned char funct_pressed;    /* Etat touche FUNCT (Atmos) */

/* ===================================================================
 *  Initialisation
 * =================================================================== */

void keyboard_init(void)
{
    funct_pressed = 0;
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
        case 0x05:  /* CTRL+E = Repetition */
            return KEY_FUNC_FLAG | KEY_REPETITION;
        case 0x07:  /* CTRL+G = Guide */
            return KEY_FUNC_FLAG | KEY_GUIDE;
        case 0x0E:  /* CTRL+N = Suite (Next) */
            return KEY_FUNC_FLAG | KEY_SUITE;
        case 0x12:  /* CTRL+R = Retour */
            return KEY_FUNC_FLAG | KEY_RETOUR;
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
        funct_pressed = 0;
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
    funct_pressed = 0;
}

unsigned char keyboard_scan(void)
{
    unsigned char ch;
    unsigned char func_key;

    if (!kbhit()) {
        return KEY_NONE;
    }

    ch = cgetc();

    /* --- FUNCT (Atmos uniquement, Tab=$09) --- */
    if (ch == 0x09) {
        funct_pressed = 1;
        return KEY_NONE;  /* Attendre la touche suivante */
    }

    /* Si FUNCT etait presse (Atmos), mapper la lettre suivante */
    if (funct_pressed) {
        funct_pressed = 0;
        func_key = map_funct_to_func(ch);
        if (func_key != KEY_NONE) {
            return func_key;
        }
        /* Pas une combinaison reconnue: traiter normalement */
    }

    /* --- Touches speciales (AVANT le handler CTRL !) ---
     * $08, $0A, $0B, $0D sont dans la range $01-$1A mais ne sont
     * PAS des CTRL+lettre : ce sont des touches speciales. */
    switch (ch) {
        case 0x0D:  /* RETURN (CR) = Envoi */
        case 0x0A:  /* RETURN (LF) ou Fleche BAS = Envoi */
            return KEY_FUNC_FLAG | KEY_ENVOI;

        case 0x0B:  /* Fleche HAUT (VT) = Retour */
            return KEY_FUNC_FLAG | KEY_RETOUR;

        case 0x08:  /* Fleche GAUCHE (BS) */
            return KEY_ARROW_LEFT;

        case 0x15:  /* Fleche DROITE (NAK). DOIT etre traitee ICI, avant le
                     * handler CTRL+lettre: $15 est dans la plage $01-$1A, sinon
                     * elle y serait avalee (KEY_NONE) et la fleche droite ne
                     * fonctionnerait jamais. */
            return KEY_ARROW_RIGHT;

        case 0x7F:  /* DELETE = Correction */
            return KEY_FUNC_FLAG | KEY_CORRECTION;

        case 0x1B:  /* ESC = Annulation */
            return KEY_FUNC_FLAG | KEY_ANNULATION;

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

    /* Fleches gauche/droite: actives uniquement en mode curseur
     * (PRO3 START $59 $43), comme sur un Minitel 1B reel. */
    if (key == KEY_ARROW_LEFT || key == KEY_ARROW_RIGHT) {
        if (ctx->kbd_cursor) {
            kbd_emit(ctx, 0x1B);
            kbd_emit(ctx, 0x5B);
            kbd_emit(ctx, (key == KEY_ARROW_LEFT) ? 0x44 : 0x43);
        }
        return;
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
