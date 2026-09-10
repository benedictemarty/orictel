/**
 * @file serial.h
 * @brief Driver serie ACIA 6551 pour OricTel
 *
 * Interface pour la communication serie via l'ACIA 6551 mappee a la base
 * LOCI ($0380-$0383). Cette base unique sert le materiel LOCI reel comme le
 * PicoWiFiModemUSB (en emulation Phosphoric --loci/--acia-addr 0380 ou sur
 * le vrai materiel). Configuration: 1200 bauds, 8N1, horloge interne,
 * polling pur sans IRQ ACIA (commandes AT du modem et flux TCP).
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

/* Base de l'ACIA 6551 (LOCI). Les 4 registres sont contigus a partir de la
 * base: DATA=+0, STATUS=+1, COMMAND=+2, CONTROL=+3. serial_init(base) patche
 * le driver en consequence (self-modifying code, voir serial_asm.s). */
#define ACIA_BASE_LOCI 0x0380  /* MOS 6551 LOCI : materiel reel ou PicoWiFi
                                * (Phosphoric --loci / --acia-addr 0380) */

/* Adresses des registres pour la base LOCI (reference/documentation) */
#define ACIA_DATA    0x0380  /* R: donnee recue, W: donnee a envoyer */
#define ACIA_STATUS  0x0381  /* R: statut, W: reset programme */
#define ACIA_COMMAND 0x0382  /* R/W: registre commande */
#define ACIA_CONTROL 0x0383  /* R/W: registre controle */

/* Bits du registre Status */
#define ACIA_RDRF    0x08    /* Bit 3: Receiver Data Register Full */
#define ACIA_TDRE    0x10    /* Bit 4: Transmitter Data Register Empty */

/* Config Control/Command appliquee par serial_init pour la base LOCI.
 * v0.3.5 : polling pur, aucune IRQ ACIA.
 *   Control=$18 : 1200 bauds, 8N1, horloge interne (baud code $8, rx-clk b4).
 *   Command=$0B : DTR (b0=1), IRQ RX desactivee (b1=1), TIC bits2-3=10 =
 *   RTS bas SANS IRQ TX, sans parite.
 *   Attention datasheet 6551 : TIC=01 (comme dans $05/$07) = IRQ TX ACTIVEE.
 *   TDRE etant leve en permanence, $05/$07 provoquaient une tempete d'IRQ
 *   (gel v0.3.3/v0.3.4). $0B (v0.3.2) etait correct depuis le debut. */
#define ACIA_CTRL_LOCI 0x18
#define ACIA_CMD_LOCI  0x0B

/**
 * Initialise l'ACIA 6551 a la base donnee: 8N1, polling (pas d'IRQ).
 * Patche les operandes du driver (self-modifying code) puis programme
 * l'ACIA. A rappeler avec la meme base pour un reset.
 * @param acia_base ACIA_BASE_LOCI ($0380)
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

/**
 * Bit 5 du registre de statut du 6551. ATTENTION, DEUX PIEGES :
 *
 *  1. LOGIQUE INVERSEE. Le bit est /DCD : un retour NON NUL signifie
 *     "PAS de porteuse". Le nom de la fonction se lit naturellement a
 *     l'envers - ne pas ecrire `if (serial_dcd())` en pensant "connecte".
 *
 *  2. SUR LOCI, CE BIT NE DIT PAS SI UN APPEL EST EN COURS. Le firmware
 *     (~/loci/firmware/src/mia/oric/acia.c) ne pilote ACIA_STAT_NOT_DCD que
 *     depuis deux choses : le montage du peripherique CDC et l'etat de DTR
 *     (acia_do_cmd). Aucune de ses affectations n'est liee a l'etat de la
 *     communication. Le bit passe donc a "porteuse presente" des que le
 *     dongle est branche et DTR pose, et ne retombe PAS quand le serveur
 *     raccroche.
 *
 * Consequence : ce bit est INUTILISABLE pour detecter une perte de porteuse
 * sur ce montage. La detection passe par la reponse "NO CARRIER" du modem,
 * cf. at_carrier_watch() dans at_modem.h.
 *
 * Conserve pour la symetrie d'API entre drivers ; aucun appelant a ce jour.
 */
unsigned char __fastcall__ serial_dcd(void);

#endif /* SERIAL_H */
