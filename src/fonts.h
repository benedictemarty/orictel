/**
 * @file fonts.h
 * @brief Acces aux jeux de caracteres Minitel pour OricTel
 *
 * Trois jeux de caracteres de 96 glyphes (codes $20-$7F), 8 octets/glyphe:
 *   G0: alphanumerique (ASCII Minitel avec accents francais)
 *   G1: mosaiques semi-graphiques (2x3 blocs)
 *   G2: caracteres supplementaires (diacritiques)
 */

#ifndef FONTS_H
#define FONTS_H

/**
 * Retourne un pointeur vers les 8 octets du glyphe G0.
 * Chaque octet = 1 ligne de 6 pixels (bits 5-0).
 * @param ch Code caractere ($20-$7F)
 */
const unsigned char* font_get_g0(unsigned char ch);

/**
 * Retourne un pointeur vers les 8 octets du glyphe G1 (mosaique).
 * Les mosaiques G1 sont calculees a partir du code 2x3 blocs.
 * @param ch Code caractere ($20-$7F)
 */
const unsigned char* font_get_g1(unsigned char ch);

/**
 * Retourne un pointeur vers les 8 octets du glyphe G2.
 * @param ch Code caractere ($20-$7F)
 */
const unsigned char* font_get_g2(unsigned char ch);

#endif /* FONTS_H */
