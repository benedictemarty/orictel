/**
 * @file display.h
 * @brief Moteur d'affichage HIRES pour OricTel
 *
 * Gere le rendu des cellules Videotex dans le framebuffer HIRES de l'Oric.
 * Resolution: 240x200 pixels = 40 colonnes x 25 lignes de 6x8 pixels.
 *
 * L'affichage utilise le mode HIRES de l'Oric ($A000-$BF3F) pour le
 * contenu Minitel, et les 3 lignes texte restantes (25-27) pour la
 * barre de statut OricTel.
 */

#ifndef DISPLAY_H
#define DISPLAY_H

#include "videotex.h"

/* Adresses memoire Oric */
#define HIRES_BASE      0xA000  /* Debut framebuffer HIRES */
#define HIRES_END       0xBF3F  /* Fin framebuffer HIRES */
#define TEXT_SCREEN     0xBB80  /* Debut ecran texte */

/* Dimensions */
#define CHAR_W  6               /* Largeur caractere en pixels */
#define CHAR_H  8               /* Hauteur caractere en pixels */
#define SCREEN_COLS 40
#define SCREEN_ROWS 25          /* Lignes Minitel en HIRES */

/**
 * Initialise l'affichage: passe en mode HIRES, efface l'ecran,
 * prepare les tables de couleurs.
 */
void display_init(void);

/**
 * Rend les cellules modifiees du buffer Videotex dans le framebuffer HIRES.
 * Utilise les dirty flags pour ne redessiner que le necessaire.
 * @param ctx Contexte Videotex contenant l'ecran et les dirty flags
 */
void display_render(vtx_context_t* ctx);

/**
 * Rend une cellule unique a la position (col, row) dans le framebuffer.
 * @param cell Cellule a rendre
 * @param col Colonne (0-39)
 * @param row Ligne (0-24)
 */
void display_render_cell(const vtx_cell_t* cell, unsigned char col, unsigned char row);

/**
 * Efface tout le framebuffer HIRES (noir).
 */
void display_clear(void);

/**
 * Affiche un message sur la barre de statut (lignes texte 25-27).
 * @param msg Chaine de caracteres (max 40 car)
 */
void display_status(const char* msg);

/**
 * Active/desactive le curseur visuel.
 * @param visible 1=afficher, 0=masquer
 * @param col Colonne du curseur
 * @param row Ligne du curseur
 */
void display_cursor(unsigned char visible, unsigned char col, unsigned char row);

#endif /* DISPLAY_H */
