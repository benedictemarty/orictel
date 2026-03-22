/**
 * @file display.h
 * @brief Moteur d'affichage TEXTE pour OricTel (v0.1)
 *
 * Utilise le mode texte standard de l'Oric (40x28) via conio cc65.
 * L'ecran Minitel (40x25) utilise les lignes 0-24.
 * Les lignes 25-27 sont pour la barre de statut OricTel.
 *
 * Compatible Oric-1 et Atmos sans manipulation hardware directe.
 * Le mode HIRES sera ajoute en v0.2 pour les mosaiques G1.
 */

#ifndef DISPLAY_H
#define DISPLAY_H

#include "videotex.h"

/* Dimensions ecran texte Oric */
#define SCREEN_COLS 40
#define SCREEN_ROWS 25          /* Lignes Minitel (0-24) */
#define STATUS_ROW  26          /* Ligne de statut */

/* Adresse ecran texte Oric */
#define TEXT_SCREEN     0xBB80

/**
 * Initialise l'affichage: efface l'ecran, configure les couleurs.
 */
void display_init(void);

/**
 * Rend les cellules modifiees du buffer Videotex a l'ecran texte.
 * @param ctx Contexte Videotex
 */
void display_render(vtx_context_t* ctx);

/**
 * Rend une cellule unique.
 */
void display_render_cell(const vtx_cell_t* cell, unsigned char col, unsigned char row);

/**
 * Efface l'ecran.
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
 * Rend une seule ligne du buffer Videotex (optimise pour eviter overrun ACIA).
 * @param ctx Contexte Videotex
 * @param row Numero de ligne (0-24)
 */
void display_render_cell_row(vtx_context_t* ctx, unsigned char row);

#endif /* DISPLAY_H */
