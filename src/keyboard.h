/**
 * @file keyboard.h
 * @brief Module clavier Oric avec mapping Minitel
 *
 * Scanne la matrice clavier 8x8 de l'Oric via le VIA 6522 et le PSG AY.
 * Traduit les touches en codes Videotex pour le Minitel.
 *
 * Compatible Oric-1 ET Atmos : utilise CTRL+lettre (pas FUNCT).
 * L'Oric-1 n'a pas de touche FUNCT, mais possede LCTRL et RCTRL.
 *
 * Mapping touches fonction Minitel:
 *   CTRL+R = Retour     (SEP $42)
 *   CTRL+E = Repetition (SEP $43)
 *   CTRL+G = Guide      (SEP $44)
 *   CTRL+A = Annulation (SEP $45)
 *   CTRL+S = Sommaire   (SEP $46)
 *   CTRL+N = Suite      (SEP $48) (N = Next)
 *   CTRL+C = Connexion  (SEP $49)
 *   RETURN = Envoi      (SEP $41)
 *   DELETE = Correction (SEP $47)
 *
 * Sur Atmos, FUNCT+lettre est aussi supporte en plus de CTRL+lettre.
 */

#ifndef KEYBOARD_H
#define KEYBOARD_H

#include "videotex.h"

/* Adresses VIA 6522 */
#define VIA_PORTB   0x0300
#define VIA_PORTA   0x030F  /* PSG Port A via VIA */

/* Codes separateur Minitel */
#define SEP         0x13    /* Separateur (prefixe touches fonction) */

/* Codes touches fonction Minitel */
#define KEY_ENVOI       0x41
#define KEY_RETOUR      0x42
#define KEY_REPETITION  0x43
#define KEY_GUIDE       0x44
#define KEY_ANNULATION  0x45
#define KEY_SOMMAIRE    0x46
#define KEY_CORRECTION  0x47
#define KEY_SUITE       0x48
#define KEY_CONNEXION   0x49

/* Resultat du scan clavier */
#define KEY_NONE          0x00  /* Aucune touche */
#define KEY_FUNC_FLAG     0x80  /* Bit 7 = touche fonction Minitel */
#define KEY_TOGGLE_RENDER 0xFE  /* CTRL+D = basculer mode rendu */
#define KEY_LOCAL_CLEAR   0xFD  /* CTRL+L = effacer ecran local */
#define KEY_LOCAL_RESET   0xFC  /* CTRL+F = reset ACIA local */
#define KEY_ARROW_LEFT    0xFB  /* Fleche gauche (mode curseur PRO3) */
#define KEY_ARROW_RIGHT   0xFA  /* Fleche droite (mode curseur PRO3) */

/**
 * Initialise le module clavier.
 */
void keyboard_init(void);

/**
 * Indique si une touche attend d'etre lue (sans la consommer).
 */
unsigned char keyboard_pending(void);

/**
 * Purge le tampon clavier ROM puis attend le relachement effectif de la
 * touche (anti-rebond / anti-auto-repeat). A appeler a l'entree de chaque
 * menu et avant la boucle session pour eviter le defilement intempestif
 * des ecrans (frappes accumulees pendant les timeouts AT, auto-repeat ROM).
 */
void keyboard_flush(void);

/**
 * Scanne le clavier Oric et retourne la touche pressee.
 * @return Code ASCII (7 bits) ou KEY_FUNC_FLAG | code_fonction,
 *         ou KEY_NONE si aucune touche.
 */
unsigned char keyboard_scan(void);

/**
 * Traite une touche et emet les codes Minitel selon les aiguillages
 * PRO3 du contexte: vers le modem si CLAVIER->MODEM est actif (defaut),
 * en echo local (decodeur Videotex) si CLAVIER->ECRAN est actif.
 * Les fleches gauche/droite ne sont emises qu'en mode curseur
 * (PRO3 START $59 $43).
 * @param ctx Contexte Videotex (aiguillages, kbd_cursor)
 * @param key Code retourne par keyboard_scan()
 */
void keyboard_process(vtx_context_t* ctx, unsigned char key);

#endif /* KEYBOARD_H */
