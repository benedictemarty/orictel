/**
 * @file main.c
 * @brief OricTel - Terminal Minitel 1B pour Oric 1/Atmos
 *
 * Supporte deux modes de connexion:
 *
 * 1. Backend Digitelec DTL 2000 (recommande):
 *    ./oric1-emu --serial digitelec:pavi.3617.fr:3617
 *    Connexion TCP directe, buffer 512, V23 auto. Pas de bridge.
 *
 * 2. Backend TCP + bridge WebSocket:
 *    python3 bridge/orictel_bridge.py &
 *    ./oric1-emu --serial tcp:127.0.0.1:3615 --serial-buffer 256 --serial-irq-on-rdrf
 */

#include "serial.h"
#include "videotex.h"
#include "display.h"
#include "keyboard.h"

/* Contexte Videotex global */
static vtx_context_t vtx;

/* Declaration serial_dcd (assembleur) */
unsigned char __fastcall__ serial_dcd(void);

int main(void)
{
    unsigned char byte;
    unsigned char key;

    /* Initialisation: display AVANT serial !
     * La ROM HIRES ($EC33) desactive les IRQ temporairement.
     * Si serial_init est appele avant, le modem se connecte et
     * envoie des octets pendant que les IRQ sont off -> perte. */
    vtx_init(&vtx);
    display_init();
    keyboard_init();
    display_status("OricTel v0.1 | CTRL+S=Sommaire");

    /* Serial EN DERNIER: DTR active la connexion modem */
    serial_init();

    /* Vider le buffer ACIA (donnees arrivees pendant l'init) */
    while (serial_poll()) {
        serial_recv();
    }

    /* Signal ready: declenche la connexion WebSocket du bridge.
     * Avec Digitelec: le modem l'envoie au serveur (inoffensif).
     * Avec bridge: c'est ce qui ouvre la connexion WebSocket. */
    serial_send(0x13);

    /* Attendre la connexion (DCD)
     * Avec Digitelec: DTR est deja set par serial_init, le modem
     * se connecte automatiquement. DCD passe a 0 quand connecte.
     * Avec TCP direct: DCD est toujours actif. */

    /* --- Boucle principale --- */
    for (;;) {

        /* 1. Drainer le buffer ISR */
        while (serial_poll()) {
            byte = serial_recv();
            if (byte != 0xFF) {
                vtx_process(&vtx, byte);
            }
        }

        /* 2. Rendre toutes les lignes modifiees */
        display_render(&vtx);

        /* 3. Clavier */
        key = keyboard_scan();
        if (key != KEY_NONE) {
            keyboard_process(key);
        }
    }

    return 0;
}
