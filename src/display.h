/**
 * @file display.h
 * @brief Moteur d'affichage HIRES pour OricTel (v0.2)
 *
 * Mode HIRES: 240x200 pixels = 40 colonnes x 25 lignes de 6x8 pixels.
 * Chaque octet HIRES: bit 6 = mode pixel (1) ou attribut serial (0).
 * Bits 5-0 = 6 pixels (encre/fond) ou code attribut.
 *
 * Les 3 lignes texte en bas (rows 25-27 a $BF68+) restent en mode texte :
 * c'est la barre de statut (display_status_*). Leur jeu de caracteres est
 * en RAM a $9800 (copie de font_g0 par display_init) - la pile C, qui
 * l'ecrasait, a ete reduite a $9C00-$9FFF (cfg/orictel.cfg).
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
 * Rend les lignes modifiees, avec budget (2 lignes max par appel) pour
 * borner la latence de la boucle principale et garder le clavier
 * reactif. Les lignes restantes partent aux appels suivants.
 */
void display_render(vtx_context_t* ctx);

/**
 * Rend toutes les lignes modifiees en un appel (menus, splash:
 * contextes sans boucle de rendu derriere).
 */
void display_render_all(vtx_context_t* ctx);

/**
 * Indique s'il reste des lignes a rendre (budget adaptatif).
 */
unsigned char display_dirty_pending(vtx_context_t* ctx);

/**
 * Rend une seule ligne.
 */
void display_render_cell_row(vtx_context_t* ctx, unsigned char row);

/**
 * Efface l'ecran HIRES.
 */
void display_clear(void);

/* --- Barre de statut : 3 lignes texte sous la page HIRES -----------------
 * Ligne 0 ($BF68) = etat (indicateur, serveur, chrono, mode de rendu),
 * ligne 1 ($BF90) = message transitoire, ligne 2 ($BFB8) = aide touches.
 * Colonne 0 de chaque ligne = attribut d'encre ; 39 colonnes de texte.
 * ATTENTION : $BFDF (ligne 2, col 39) est l'octet de bascule HIRES ($1E)
 * pose par la ROM, il n'est JAMAIS ecrit. */
#define STATUS_LINES 3
#define STATUS_COLS  39

/**
 * Ecrit s sur la ligne line (0-2) a partir de col (1-39), clippe a 39
 * colonnes. inverse != 0 : video inverse (bit 7).
 */
void display_status_text(unsigned char line, unsigned char col,
                         const char* s, unsigned char inverse);

/**
 * Efface la ligne line (0-2) de la barre de statut.
 */
void display_status_clear(unsigned char line);

/**
 * Message transitoire sur la ligne 1 de la barre (efface le reste de la ligne).
 */
void display_status(const char* msg);

/**
 * Curseur visuel.
 */
void display_cursor(unsigned char visible, unsigned char col, unsigned char row);

/**
 * Beep via PSG AY-3-8912 (~1kHz, ~100ms).
 */
void display_beep(void);

/**
 * Rend une cellule unique (pour compatibilite).
 */
void display_render_cell(const vtx_cell_t* cell, unsigned char col, unsigned char row);

#endif /* DISPLAY_H */
