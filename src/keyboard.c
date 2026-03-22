/**
 * @file keyboard.c
 * @brief Module clavier Oric avec mapping Minitel
 *
 * Scanne la matrice clavier 8x8 de l'Oric via le VIA 6522 et PSG AY.
 * Utilise la routine ROM de scan ($EB78 Atmos / $E905 Oric-1).
 *
 * Reference: keyboard.h et keyboard.c de l'emulateur Phosphoric.
 */

#include <conio.h>
#include "keyboard.h"
#include "serial.h"

/* ===================================================================
 *  Variables internes
 * =================================================================== */

static unsigned char funct_pressed;    /* Etat touche FUNCT */

/* ===================================================================
 *  Initialisation
 * =================================================================== */

void keyboard_init(void)
{
    funct_pressed = 0;
}

/* ===================================================================
 *  Scan clavier via cc65 conio
 *
 *  On utilise kbhit() et cgetc() de cc65 pour la compatibilite.
 *  La touche FUNCT (mapped sur Tab dans l'emulateur) est detectee
 *  separement.
 * =================================================================== */

unsigned char keyboard_scan(void)
{
    unsigned char ch;

    if (!kbhit()) {
        return KEY_NONE;
    }

    ch = cgetc();

    /* Detection de la touche FUNCT via flag interne */
    /* Dans cc65/Oric, Tab = $09 est utilise pour FUNCT */
    if (ch == 0x09) {
        funct_pressed = 1;
        return KEY_NONE;  /* Attendre la touche suivante */
    }

    /* Si FUNCT etait presse, mapper vers touche fonction Minitel */
    if (funct_pressed) {
        funct_pressed = 0;
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
            case 'c': case 'C':
                return KEY_FUNC_FLAG | KEY_CONNEXION;
            default:
                return ch;  /* Pas une combinaison reconnue */
        }
    }

    /* Touches speciales */
    switch (ch) {
        case 0x0D:  /* RETURN = Envoi */
            return KEY_FUNC_FLAG | KEY_ENVOI;
        case 0x7F:  /* DELETE = Correction */
        case 0x08:  /* BACKSPACE */
            return KEY_FUNC_FLAG | KEY_CORRECTION;
        case 0x0A:  /* Fleche BAS (LF sur Oric) = Suite */
            return KEY_FUNC_FLAG | KEY_SUITE;
        case 0x0B:  /* Fleche HAUT (VT sur Oric) */
            /* Envoyer comme fleche haut Minitel: ESC [ A */
            serial_send(0x1B);
            serial_send(0x5B);
            serial_send(0x41);
            return KEY_NONE;  /* Deja envoye */
        default:
            break;
    }

    /* Fleches gauche/droite */
    if (ch == 0x08) {  /* Gauche */
        serial_send(0x1B);
        serial_send(0x5B);
        serial_send(0x44);
        return KEY_NONE;
    }
    if (ch == 0x15) {  /* Droite (NAK / Ctrl-U sur Oric) */
        serial_send(0x1B);
        serial_send(0x5B);
        serial_send(0x43);
        return KEY_NONE;
    }

    /* Caractere ASCII normal */
    return ch;
}

/* ===================================================================
 *  Envoi des codes Minitel correspondants via serie
 * =================================================================== */

void keyboard_process(unsigned char key)
{
    if (key == KEY_NONE) {
        return;
    }

    /* Touche fonction Minitel */
    if (key & KEY_FUNC_FLAG) {
        unsigned char func_code = key & 0x7F;
        serial_send(SEP);           /* Separateur $13 */
        serial_send(func_code);     /* Code fonction ($41-$49) */
        return;
    }

    /* Caractere ASCII normal - envoyer tel quel (7 bits) */
    serial_send(key & 0x7F);
}
