/**
 * @file display.h
 * @brief Moteur d'affichage HIRES pour OricTel (v0.2)
 *
 * Mode HIRES: 240x200 pixels = 40 colonnes x 25 lignes de 6x8 pixels.
 * Chaque octet HIRES: bit 6 = mode pixel (1) ou attribut serial (0).
 * Bits 5-0 = 6 pixels (encre/fond) ou code attribut.
 *
 * Les 3 lignes texte en bas (rows 25-27 a $BF68+) restent en mode texte
 * pour la barre de statut.
 */

#ifndef DISPLAY_H
#define DISPLAY_H

#include "videotex.h"

/* Adresses memoire HIRES Oric */
#define HIRES_BASE  0xA000      /* Debut framebuffer HIRES */
#define TEXT_STATUS  0xBF68     /* Lignes texte 25-27 en mode HIRES */

/* Dimensions */
#define SCREEN_COLS 40
#define SCREEN_ROWS 25
#define CHAR_W      6           /* Pixels par caractere */
#define CHAR_H      8           /* Lignes par caractere */
#define STATUS_ROW  26

/**
 * Initialise l'affichage en mode HIRES.
 */
void display_init(void);

/**
 * Rend toutes les cellules modifiees.
 */
void display_render(vtx_context_t* ctx);

/**
 * Rend une seule ligne.
 */
void display_render_cell_row(vtx_context_t* ctx, unsigned char row);

/**
 * Efface l'ecran HIRES.
 */
void display_clear(void);

/**
 * Affiche un message sur la barre de statut.
 */
void display_status(const char* msg);

/**
 * Curseur visuel.
 */
void display_cursor(unsigned char visible, unsigned char col, unsigned char row);

/**
 * Rend une cellule unique (pour compatibilite).
 */
void display_render_cell(const vtx_cell_t* cell, unsigned char col, unsigned char row);

#endif /* DISPLAY_H */
