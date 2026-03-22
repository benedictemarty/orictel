/**
 * @file serial.h
 * @brief Driver serie ACIA 6551 pour OricTel
 *
 * Interface haut niveau pour la communication serie via l'ACIA 6551
 * mappee a $031C-$031F. Utilisee pour la liaison Minitel V23
 * (1200 baud RX / 75 baud TX, 7 bits, parite paire).
 */

#ifndef SERIAL_H
#define SERIAL_H

/* Adresses des registres ACIA 6551 */
#define ACIA_DATA    0x031C  /* R: donnee recue, W: donnee a envoyer */
#define ACIA_STATUS  0x031D  /* R: statut, W: reset programme */
#define ACIA_COMMAND 0x031E  /* R/W: registre commande */
#define ACIA_CONTROL 0x031F  /* R/W: registre controle */

/* Bits du registre Status */
#define ACIA_RDRF    0x08    /* Bit 3: Receiver Data Register Full */
#define ACIA_TDRE    0x10    /* Bit 4: Transmitter Data Register Empty */

/**
 * Initialise l'ACIA 6551 pour le mode Minitel V23.
 * Configuration: 1200 baud, 7 bits, parite paire, 1 stop.
 */
void __fastcall__ serial_init(void);

/**
 * Envoie un octet via l'ACIA. Attend que le transmetteur soit pret.
 * @param byte Octet a envoyer (7 bits utiles)
 */
void __fastcall__ serial_send(unsigned char byte);

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

/**
 * Envoie un buffer d'octets via l'ACIA.
 * @param buf Pointeur vers les donnees
 * @param len Nombre d'octets a envoyer
 */
void serial_send_buf(const unsigned char* buf, unsigned char len);

#endif /* SERIAL_H */
