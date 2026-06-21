/**
 * @file serial.h
 * @brief Driver serie ACIA 6551 pour OricTel
 *
 * Interface pour la communication serie via l'ACIA 6551 mappee a
 * $031C-$031F. Configuration actuelle: 19200 baud, 8N1, horloge
 * interne (commandes AT du modem emule et flux TCP emulateur).
 * Le V23 reel (1200 RX / 75 TX, 7E1) demanderait Control=$18 et la
 * parite dans Command - voir serial_asm.s.
 *
 * L'emission passe par une file logicielle non bloquante (serial_tx.c)
 * drainee depuis la boucle principale, pour ne jamais bloquer la
 * reception pendant l'attente TDRE (l'ACIA n'a qu'un octet de tampon
 * RX et aucune IRQ n'est utilisee).
 */

#ifndef SERIAL_H
#define SERIAL_H

#ifdef TEST_HOST
#define __fastcall__
#endif

/* Base de l'ACIA 6551 selon le materiel (choisie au runtime, menu Interface).
 * Les 4 registres sont contigus a partir de la base: DATA=+0, STATUS=+1,
 * COMMAND=+2, CONTROL=+3. serial_init(base) patche le driver en consequence
 * (self-modifying code, voir serial_asm.s). */
#define ACIA_BASE_EMU  0x031C  /* ACIA emulee Phosphoric/Euphoric (defaut) */
#define ACIA_BASE_LOCI 0x0380  /* ACIA emulee par la cartouche LOCI reelle */

/* Adresses des registres pour la base emulateur (reference/documentation) */
#define ACIA_DATA    0x031C  /* R: donnee recue, W: donnee a envoyer */
#define ACIA_STATUS  0x031D  /* R: statut, W: reset programme */
#define ACIA_COMMAND 0x031E  /* R/W: registre commande */
#define ACIA_CONTROL 0x031F  /* R/W: registre controle */

/* Bits du registre Status */
#define ACIA_RDRF    0x08    /* Bit 3: Receiver Data Register Full */
#define ACIA_TDRE    0x10    /* Bit 4: Transmitter Data Register Empty */

/**
 * Initialise l'ACIA 6551 a la base donnee: 8N1, polling (pas d'IRQ).
 * Patche les operandes du driver (self-modifying code) puis programme
 * l'ACIA. A rappeler avec la meme base pour un reset.
 * @param acia_base ACIA_BASE_EMU ($031C) ou ACIA_BASE_LOCI ($0380)
 */
void __fastcall__ serial_init(unsigned acia_base);

/**
 * Empile un octet dans la file d'emission (non bloquant tant que la
 * file n'est pas pleine). L'octet part via serial_tx_pump().
 * @param byte Octet a envoyer
 */
void serial_send(unsigned char byte);

/**
 * Emet au plus un octet de la file si le transmetteur est pret.
 * A appeler regulierement depuis la boucle principale.
 */
void serial_tx_pump(void);

/**
 * Vide la file d'emission (bloquant). Reserve aux phases sans
 * reception attendue (ex: commandes AT avant connexion).
 */
void serial_tx_flush(void);

/**
 * Ecriture directe ACIA, bloque sur TDRE (assembleur).
 * Ne pas utiliser depuis le code applicatif: passer par serial_send().
 */
void __fastcall__ serial_send_raw(unsigned char byte);

/**
 * Etat du transmetteur (non bloquant).
 * @return Non-zero si TDRE (transmetteur pret)
 */
unsigned char __fastcall__ serial_tx_ready(void);

/**
 * Recoit un octet de l'ACIA (non-bloquant).
 * @return Octet recu, ou 0xFF si aucune donnee disponible
 */
unsigned char __fastcall__ serial_recv(void);

/**
 * Verifie si une donnee est disponible en reception.
 * @return Non-zero si donnee disponible
 */
unsigned char __fastcall__ serial_poll(void);

#endif /* SERIAL_H */
