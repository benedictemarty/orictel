/**
 * @file keyboard.h
 * @brief Module clavier Oric avec mapping Minitel
 *
 * Scanne la matrice clavier 8x8 de l'Oric via le VIA 6522 et le PSG AY.
 * Traduit les touches en codes Videotex pour le Minitel.
 *
 * Touches fonction Minitel via FUNCT (Tab):
 *   FUNCT+R = Retour     (SEP $42)
 *   FUNCT+E = Repetition (SEP $43)
 *   FUNCT+G = Guide      (SEP $44)
 *   FUNCT+A = Annulation (SEP $45)
 *   FUNCT+S = Sommaire   (SEP $46)
 *   FUNCT+C = Connexion  (SEP $49)
 * RETURN    = Envoi      (SEP $41)
 * DELETE    = Correction (SEP $47)
 * DOWN      = Suite      (SEP $48)
 */

#ifndef KEYBOARD_H
#define KEYBOARD_H

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
#define KEY_NONE        0x00    /* Aucune touche */
#define KEY_FUNC_FLAG   0x80    /* Bit 7 = touche fonction Minitel */

/**
 * Initialise le module clavier.
 */
void keyboard_init(void);

/**
 * Scanne le clavier Oric et retourne la touche pressee.
 * @return Code ASCII (7 bits) ou KEY_FUNC_FLAG | code_fonction,
 *         ou KEY_NONE si aucune touche.
 */
unsigned char keyboard_scan(void);

/**
 * Traite une touche et envoie les codes Minitel via la serie.
 * @param key Code retourne par keyboard_scan()
 */
void keyboard_process(unsigned char key);

#endif /* KEYBOARD_H */
