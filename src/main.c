/**
 * @file main.c
 * @brief Programme principal OricTel - Terminal Minitel 1B pour Oric
 *
 * Avec le buffer FIFO de l'emulateur (--serial-buffer 256) et
 * l'IRQ-on-RDRF, le programme peut:
 * 1. Lire TOUS les octets du buffer logiciel d'un coup
 * 2. Les traiter via vtx_process
 * 3. Rendre TOUTES les lignes modifiees
 * 4. Scanner le clavier
 * Sans risque de perte de donnees (l'ISR bufferise en arriere-plan).
 */

#include "serial.h"
#include "videotex.h"
#include "display.h"
#include "keyboard.h"

/* Contexte Videotex global */
static vtx_context_t vtx;

int main(void)
{
    unsigned char byte;
    unsigned char key;

    /* Initialisation */
    serial_init();
    vtx_init(&vtx);
    display_init();
    keyboard_init();

    /* Barre de statut */
    display_status("OricTel v0.1 | CTRL+S=Sommaire");

    /* Vider le buffer ACIA */
    while (serial_poll()) {
        serial_recv();
    }

    /* Signal ready (1 octet) */
    serial_send(0x13);

    /* --- Boucle principale --- */
    for (;;) {

        /* 1. Drainer TOUT le buffer logiciel (rempli par l'ISR) */
        while (serial_poll()) {
            byte = serial_recv();
            if (byte != 0xFF) {
                vtx_process(&vtx, byte);
            }
        }

        /* 2. Rendre TOUTES les lignes modifiees d'un coup */
        /*    Avec le FIFO de l'emulateur, les octets entrants sont    */
        /*    bufferises par l'ISR pendant le rendu. Pas d'overrun.    */
        display_render(&vtx);

        /* 3. Clavier */
        key = keyboard_scan();
        if (key != KEY_NONE) {
            keyboard_process(key);
        }
    }

    return 0;
}
