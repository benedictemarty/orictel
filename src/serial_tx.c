/**
 * @file serial_tx.c
 * @brief File d'emission serie non bloquante
 *
 * Pourquoi: en V23 reel le canal montant est a 75 bauds (~133 ms par
 * octet). Un envoi bloquant sur TDRE - par exemple l'ACK PRO3 de
 * 5 octets, soit ~660 ms - laisserait le flux descendant 1200 bauds
 * remplir l'ACIA 6551, qui n'a qu'UN octet de tampon RX et pas d'IRQ:
 * overrun garanti. Sous emulateur le FIFO 4096 masque le probleme,
 * pas sur vrai materiel.
 *
 * serial_send() empile donc dans une file logicielle, drainee octet
 * par octet (uniquement quand TDRE est pret) par serial_tx_pump()
 * appelee depuis la boucle principale entre deux lectures RX.
 * serial_tx_flush() vide la file en bloquant: reserve aux phases sans
 * reception attendue (commandes AT avant connexion).
 */

#include "serial.h"

#define TX_QUEUE_SIZE 32   /* puissance de 2; largement > la plus longue
                            * reponse protocole (ACK PRO3 = 5 octets) */
#define TX_QUEUE_MASK (TX_QUEUE_SIZE - 1)

static unsigned char tx_queue[TX_QUEUE_SIZE];
static unsigned char tx_head;   /* prochain octet a emettre */
static unsigned char tx_tail;   /* prochaine case libre */

void serial_tx_pump(void)
{
    if (tx_head == tx_tail) {
        return;                 /* file vide */
    }
    if (!serial_tx_ready()) {
        return;                 /* transmetteur occupe: on repassera */
    }
    serial_send_raw(tx_queue[tx_head]);
    tx_head = (tx_head + 1) & TX_QUEUE_MASK;
}

void serial_send(unsigned char byte)
{
    unsigned char next = (tx_tail + 1) & TX_QUEUE_MASK;
    while (next == tx_head) {
        /* File pleine: drainer (cas degrade, equivaut a l'ancien
         * comportement bloquant) */
        serial_tx_pump();
    }
    tx_queue[tx_tail] = byte;
    tx_tail = next;
}

void serial_tx_flush(void)
{
    while (tx_head != tx_tail) {
        serial_tx_pump();
    }
}
