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

/* ---- Charte des ecrans locaux (v0.3.23, reprise de NeoTel 0.9.1) --------
 * Bandeau bleu (rangees 1-2, titre blanc en double largeur, texte de droite
 * jaune), filets mosaiques, items "[k] libelle .... valeur" avec l'item
 * courant sur fond bleu, pied de page (filet + texte).
 *
 * Adaptation Oric : le HIRES ne change de couleur que par un attribut serie
 * qui OCCUPE une cellule vide (display.c, rendu hybride). Toute transition
 * de couleur est donc precedee d'un espace (deux pour fond + encre), les
 * filets laissent la colonne 0 libre, et le titre est en double LARGEUR : une
 * double hauteur ferait rendre la rangee en brut (blanc sur noir). */
#define UI_BAND_BG      VTX_BLUE
#define UI_ROW_HEADER   2       /* rangee du titre (rangee 1 : bandeau vide) */
#define UI_ROW_RULE     3       /* filet sous le bandeau */
#define UI_ROW_FOOTRULE 22      /* filet du pied de page */
#define UI_ROW_FOOTER   23      /* texte du pied de page */
#define UI_ITEM_COL     3       /* '[' de l'item (colonnes 1-2 : attributs) */
#define UI_VALUE_COL    22      /* debut de la valeur d'un item */

/* Rangee entiere : espaces G0 de fond bg (encre blanche, sans attribut). */
void ui_fill(vtx_context_t* ctx, unsigned char row, unsigned char bg);

/* Filet mosaique (trait, G1 $60) des colonnes 1 a 39, couleur fg. */
void ui_rule(vtx_context_t* ctx, unsigned char row, unsigned char fg);

/* Bandeau : rangees 1-2 fond bleu, titre double largeur en (2, 2), texte
 * right (peut etre NULL) aligne a droite en jaune ; filet cyan en rangee 3. */
void ui_header(vtx_context_t* ctx, const char* title, const char* right);

/* Pied de page : filet blanc en rangee 22, left (vert) en colonne 2 et
 * right (blanc) aligne a droite en rangee 23. NULL = rien. */
void ui_footer(vtx_context_t* ctx, const char* left, const char* right);

/* Item : "[k] label ....... value" ; sel != 0 -> fond bleu sur les colonnes
 * 1-38, encre blanche. value peut etre NULL. La rangee est d'abord effacee. */
void ui_item(vtx_context_t* ctx, unsigned char row, char key,
             const char* label, const char* value, unsigned char sel);

/* Texte aligne a droite (fin en colonne 37). */
void ui_print_right(vtx_context_t* ctx, unsigned char row, const char* s,
                    unsigned char fg);

/* Navigation d'un menu : met a jour *sel (0..n-1) selon key. Retourne
 *  1 si la selection a change (fleches haut/bas, en boucle), 2 si
 *  validation (ENVOI, SUITE, fleche droite), 0 sinon. */
unsigned char ui_nav(unsigned char key, unsigned char* sel, unsigned char n);

#endif /* UI_H */
