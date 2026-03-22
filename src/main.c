/**
 * @file main.c
 * @brief Programme principal OricTel - Terminal Minitel 1B pour Oric
 *
 * Boucle principale:
 * 1. Polling ACIA pour donnees entrantes (serveur Minitel)
 * 2. Traitement Videotex de chaque octet recu
 * 3. Scan clavier et envoi des commandes utilisateur
 * 4. Rendu incremental des cellules modifiees en HIRES
 *
 * @author bmarty <bmarty@mailo.com>
 * @date 2026-03-22
 */

#include "serial.h"
#include "videotex.h"
#include "display.h"
#include "keyboard.h"

/* Version */
#define VERSION_MAJOR   0
#define VERSION_MINOR   1
#define VERSION_PATCH   0

/* Contexte Videotex global */
static vtx_context_t vtx;

/* Compteur de frames pour le rendu */
static unsigned char frame_counter;

/* ===================================================================
 *  Ecran d'accueil
 * =================================================================== */

static void show_splash(void)
{
    /* Positionner au centre de l'ecran */
    vtx.cur_y = 8;
    vtx.cur_x = 8;
    vtx.fg_color = VTX_CYAN;
    vtx.charset = CHARSET_G0;

    /* Ecrire "OricTel v0.1" caractere par caractere */
    {
        static const char title[] = "OricTel v0.1.0";
        unsigned char i;
        for (i = 0; title[i]; ++i) {
            vtx.screen[8][(unsigned char)(11 + i)].ch = title[i];
            vtx.screen[8][(unsigned char)(11 + i)].charset = CHARSET_G0;
            vtx.screen[8][(unsigned char)(11 + i)].fg = VTX_CYAN;
            vtx.screen[8][(unsigned char)(11 + i)].bg = VTX_BLACK;
            vtx.screen[8][(unsigned char)(11 + i)].flags = 0;
            vtx.screen[8][(unsigned char)(11 + i)].size = SIZE_NORMAL;
        }
        vtx.dirty[8] = 1;
    }

    /* Sous-titre */
    {
        static const char sub[] = "Terminal Minitel 1B";
        unsigned char i;
        for (i = 0; sub[i]; ++i) {
            vtx.screen[10][(unsigned char)(10 + i)].ch = sub[i];
            vtx.screen[10][(unsigned char)(10 + i)].charset = CHARSET_G0;
            vtx.screen[10][(unsigned char)(10 + i)].fg = VTX_WHITE;
            vtx.screen[10][(unsigned char)(10 + i)].bg = VTX_BLACK;
            vtx.screen[10][(unsigned char)(10 + i)].flags = 0;
            vtx.screen[10][(unsigned char)(10 + i)].size = SIZE_NORMAL;
        }
        vtx.dirty[10] = 1;
    }

    /* Message connexion */
    {
        static const char msg[] = "Connexion en cours...";
        unsigned char i;
        for (i = 0; msg[i]; ++i) {
            vtx.screen[14][(unsigned char)(9 + i)].ch = msg[i];
            vtx.screen[14][(unsigned char)(9 + i)].charset = CHARSET_G0;
            vtx.screen[14][(unsigned char)(9 + i)].fg = VTX_YELLOW;
            vtx.screen[14][(unsigned char)(9 + i)].bg = VTX_BLACK;
            vtx.screen[14][(unsigned char)(9 + i)].flags = 0;
            vtx.screen[14][(unsigned char)(9 + i)].size = SIZE_NORMAL;
        }
        vtx.dirty[14] = 1;
    }

    /* Forcer le rendu */
    display_render(&vtx);
}

/* ===================================================================
 *  Boucle principale
 * =================================================================== */

int main(void)
{
    unsigned char byte;
    unsigned char key;
    unsigned char rx_count;

    /* Initialisation des sous-systemes */
    serial_init();
    vtx_init(&vtx);
    display_init();
    keyboard_init();

    /* Barre de statut */
    display_status("OricTel v0.1 | CTRL+S=Sommaire");

    /* Ecran d'accueil */
    show_splash();

    /* --- Boucle principale --- */
    for (;;) {

        /* 1. Recevoir les donnees du serveur Minitel (batch) */
        /*    Traiter jusqu'a 16 octets par iteration pour rester reactif */
        rx_count = 0;
        while (serial_poll() && rx_count < 16) {
            byte = serial_recv();
            if (byte != 0xFF) {
                vtx_process(&vtx, byte);
                ++rx_count;
            }
        }

        /* 2. Scanner le clavier */
        key = keyboard_scan();
        if (key != KEY_NONE) {
            keyboard_process(key);
        }

        /* 3. Rendu incremental (toutes les 4 iterations ~= vsync) */
        ++frame_counter;
        if (frame_counter >= 4 || rx_count > 0) {
            frame_counter = 0;
            display_render(&vtx);

            /* Afficher le curseur */
            if (vtx.cur_visible) {
                display_cursor(1, vtx.cur_x, vtx.cur_y);
            }
        }
    }

    /* Jamais atteint */
    return 0;
}
