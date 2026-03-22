/**
 * @file main.c
 * @brief Programme principal OricTel - Terminal Minitel 1B pour Oric
 *
 * Boucle optimisee:
 * - Lire 1 octet ACIA par iteration (evite overrun a 1200 baud)
 * - Rendre au maximum 1 ligne par iteration (~3000 cycles)
 * - A 1200 baud: 8333 cycles entre chaque octet = marge confortable
 */

#include "serial.h"
#include "videotex.h"
#include "display.h"
#include "keyboard.h"

/* Contexte Videotex global */
static vtx_context_t vtx;

/* ===================================================================
 *  Boucle principale
 * =================================================================== */

int main(void)
{
    unsigned char byte;
    unsigned char key;
    unsigned char render_row;

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

    /* Signal ready pour le bridge/serveur */
    serial_send(0x13);
    serial_send(0x49);

    render_row = 0;

    /* --- Boucle principale --- */
    for (;;) {

        /* 1. Lire UN SEUL octet ACIA (evite overrun) */
        /*    A 1200 baud: 8333 cycles entre octets.   */
        /*    Cette boucle + rendu 1 ligne < 5000 cycles = OK */
        if (serial_poll()) {
            byte = serial_recv();
            if (byte != 0xFF) {
                vtx_process(&vtx, byte);
            }
        }

        /* 2. Rendre AU PLUS 1 ligne dirty par iteration */
        /*    Chercher la prochaine ligne dirty a partir de render_row */
        {
            unsigned char tries = 0;
            while (tries < VTX_ROWS) {
                if (vtx.dirty[render_row] || vtx.full_refresh) {
                    display_render_cell_row(&vtx, render_row);
                    vtx.dirty[render_row] = 0;
                    render_row = (render_row + 1) % VTX_ROWS;
                    break;
                }
                render_row = (render_row + 1) % VTX_ROWS;
                ++tries;
            }
            if (tries >= VTX_ROWS) {
                vtx.full_refresh = 0;
            }
        }

        /* 3. Scanner le clavier (rapide, ~100 cycles) */
        key = keyboard_scan();
        if (key != KEY_NONE) {
            keyboard_process(key);
        }
    }

    return 0;
}
