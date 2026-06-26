/**
 * @file ui.h
 * @brief Helpers d'affichage et de saisie des menus OricTel.
 *
 * Extraits de main.c pour etre testables host (tests/test_ui.c) : ces fonctions
 * ne dependent que du contexte Videotex (buffer ecran) et des primitives
 * clavier/rendu, pas du materiel.
 */

#ifndef UI_H
#define UI_H

#include "videotex.h"   /* vtx_context_t, VTX_COLS, couleurs VTX_* */

/* Ecrit une chaine nul-terminee a (row, col) avec la couleur fg et marque la
 * ligne dirty. Clippe sur VTX_COLS : une chaine trop longue ne deborde JAMAIS
 * sur la ligne suivante. */
void ui_print(vtx_context_t* ctx, unsigned char row,
              unsigned char col, const char* s, unsigned char fg);

/* Item de menu standard a la colonne 12 : texte jaune, 1er caractere
 * (le chiffre de selection) en cyan. */
void ui_menu_item(vtx_context_t* ctx, unsigned char row, const char* s);

/* Saisie de texte bornee a partir de (row, col), avec echo a l'ecran.
 *  - buf/bufsize : tampon de sortie (terminaison incluse).
 *  - mask        : si != 0, caractere d'echo (ex. '*') ; 0 => echo du caractere.
 *  - retour      : longueur saisie (0..maxlen) sur ENVOI ; 0xFF sur ANNULATION.
 *
 * La longueur est bornee A LA FOIS par le tampon (bufsize-1) ET par la largeur
 * ecran restante (VTX_COLS - col) : ni 'buf' ni 'screen' ne sont jamais ecrits
 * hors limites (col suppose < VTX_COLS). */
unsigned char ui_text_input(vtx_context_t* ctx, unsigned char row,
                            unsigned char col, char* buf,
                            unsigned char bufsize, unsigned char mask);

#endif /* UI_H */
