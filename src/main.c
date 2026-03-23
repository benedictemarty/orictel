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

/* Compteur blink: bascule blink_phase toutes les ~25 frames (50Hz -> ~500ms) */
static unsigned char blink_counter;

/* Variable globale blink_phase accessible depuis display.c */
unsigned char g_blink_phase;

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

    /* Message de connexion sur l'ecran HIRES */
    {
        static const char msg1[] = "OricTel v0.2";
        static const char msg2[] = "Connexion en cours...";
        unsigned char i;
        for (i = 0; msg1[i]; ++i)
            vtx.screen[10][14 + i].ch = msg1[i];
        vtx.dirty[10] = 1;
        for (i = 0; msg2[i]; ++i)
            vtx.screen[12][10 + i].ch = msg2[i];
        vtx.dirty[12] = 1;
        display_render(&vtx);
    }

    /* Serial EN DERNIER: DTR active la connexion modem */
    serial_init();

    /* --- Boucle principale ---
     * FIFO 4096 absorbe le burst TCP a pleine vitesse.
     * ACIA 1200 baud delivre a l'ISR a 120 oct/sec (Minitel).
     * Ring buffer 256 ne deborde jamais (max 2 oct entre renders).
     * Affichage progressif authentique Minitel 1200 baud. */
    for (;;) {

        /* 1. Drainer le ring buffer ISR (1-2 octets a 1200 baud) */
        while (serial_poll()) {
            byte = serial_recv();
            if (byte != 0xFF) {
                vtx_process(&vtx, byte);
            }
        }

        /* 2. Rendre les lignes modifiees */
        display_render(&vtx);

        /* 3. Blink: basculer la phase (basse priorite, pas de full_refresh) */
        ++blink_counter;
        if (blink_counter >= 250) {
            blink_counter = 0;
            vtx.blink_phase ^= 1;
            g_blink_phase = vtx.blink_phase;
        }

        /* 4. Clavier */
        key = keyboard_scan();
        if (key != KEY_NONE) {
            keyboard_process(key);

        }
    }

    return 0;
}
