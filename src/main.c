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

/* ===================================================================
 *  Ecriture registre AY-3-8912 via VIA 6522
 *
 *  Hardware Oric:
 *    AY DA0-DA7 = VIA Port A ($030F)
 *    AY BC1     = VIA CA2 (controle via PCR bits 3:1)
 *    AY BDIR    = VIA CB2 (controle via PCR bits 7:5)
 *    AY BC2     = +5V (toujours actif)
 *
 *  PCR ($030C):
 *    CA2 high (BC1=1) = bits 3:1 = 111 = $0E
 *    CA2 low  (BC1=0) = bits 3:1 = 110 = $0C
 *    CB2 high (BDIR=1) = bits 7:5 = 111 = $E0
 *    CB2 low  (BDIR=0) = bits 7:5 = 110 = $C0
 *
 *  Latch addr: BDIR=1, BC1=1 → PCR = $EE
 *  Write data: BDIR=1, BC1=0 → PCR = $EC
 *  Inactive:   BDIR=0, BC1=0 → PCR = $CC
 * =================================================================== */
static unsigned char ay_reg_save;
static unsigned char ay_val_save;

static void ay_write(unsigned char reg, unsigned char val)
{
    ay_reg_save = reg;
    ay_val_save = val;
    __asm__("sei");               /* Desactiver IRQ pendant l'acces VIA */
    __asm__("lda #$FF");
    __asm__("sta $0303");         /* DDRA = all output */
    __asm__("lda %v", ay_reg_save);
    __asm__("sta $030F");         /* Port A = register number */
    __asm__("lda #$EE");          /* Latch: BC1=1, BDIR=1 */
    __asm__("sta $030C");
    __asm__("lda #$CC");          /* Inactive */
    __asm__("sta $030C");
    __asm__("lda %v", ay_val_save);
    __asm__("sta $030F");         /* Port A = data value */
    __asm__("lda #$EC");          /* Write: BC1=0, BDIR=1 */
    __asm__("sta $030C");
    __asm__("lda #$CC");          /* Inactive */
    __asm__("sta $030C");
    __asm__("cli");               /* Reactiver IRQ */
}

/* Delai ~N millisecondes (approximatif a 1MHz) */
static void delay_ms(unsigned int ms)
{
    unsigned int i;
    for (i = 0; i < ms; ++i) {
        __asm__("ldx #$C8");
        __asm__("_dms_lp: dex");
        __asm__("bne _dms_lp");
    }
}

/* Jouer une note sur canal A (period = freq AY, dur = duree en ms) */
static void play_note(unsigned int period, unsigned char vol, unsigned int dur)
{
    ay_write(0, period & 0xFF);     /* Canal A period low */
    ay_write(1, period >> 8);       /* Canal A period high */
    ay_write(8, vol);               /* Volume canal A */
    delay_ms(dur);
}

/* Jingle intro style GameCube:
 * Arpege ascendant rapide sur 3 canaux avec echo
 * Frequences AY: period = 62500 / freq_hz */
static void play_jingle(void)
{
    /* Mixer: tone A+B+C on, noise off */
    ay_write(7, 0x38);  /* 00111000: tone ABC on */

    /* Canal B et C: accords d'accompagnement */
    ay_write(2, 0xEE);  ay_write(3, 0x00);  /* B: E4 (period ~189) */
    ay_write(9, 0x00);                        /* B muet d'abord */
    ay_write(4, 0x9F);  ay_write(5, 0x00);  /* C: G4 (period ~159) */
    ay_write(10, 0x00);                       /* C muet */

    /* Arpege ascendant - GameCube style */
    play_note(238, 12, 80);   /* C4 */
    play_note(189, 13, 80);   /* E4 */
    ay_write(9, 8);            /* B: E4 entre */
    play_note(159, 14, 80);   /* G4 */
    ay_write(10, 7);           /* C: G4 entre */
    play_note(119, 15, 100);  /* C5 */

    /* Accord final lumineux */
    ay_write(0, 0x5F);  ay_write(1, 0x00);  /* A: E5 (95) */
    ay_write(2, 0x77);  ay_write(3, 0x00);  /* B: C5 (119) */
    ay_write(4, 0x50);  ay_write(5, 0x00);  /* C: G5 (80) */
    ay_write(8, 15);  ay_write(9, 12);  ay_write(10, 10);
    delay_ms(200);

    /* Descente douce du volume */
    ay_write(8, 10);  ay_write(9, 8);  ay_write(10, 6);
    delay_ms(150);
    ay_write(8, 6);   ay_write(9, 5);  ay_write(10, 3);
    delay_ms(150);
    ay_write(8, 3);   ay_write(9, 2);  ay_write(10, 1);
    delay_ms(150);

    /* Silence */
    ay_write(8, 0);  ay_write(9, 0);  ay_write(10, 0);
}

/* Ecran splash: titre, auteur, licence, email, jingle */
static void splash_screen(vtx_context_t* ctx)
{
    unsigned char i, c;
    const char* p;

    /* Titre en double hauteur (row 4-5 centré) */
    static const char title[] = "OricTel";
    for (i = 0; title[i]; ++i) {
        c = 16 + i;
        ctx->screen[5][c].ch = title[i];
        ctx->screen[5][c].fg = VTX_CYAN;
        ctx->screen[5][c].size = SIZE_DOUBLE_HEIGHT;
    }
    ctx->dirty[4] = 1;
    ctx->dirty[5] = 1;

    /* Version */
    p = "v0.2";
    for (i = 0; p[i]; ++i) {
        ctx->screen[7][18 + i].ch = p[i];
        ctx->screen[7][18 + i].fg = VTX_WHITE;
    }
    ctx->dirty[7] = 1;

    /* Trait de separation */
    for (c = 5; c < 35; ++c) {
        ctx->screen[9][c].ch = 0x60;
        ctx->screen[9][c].charset = CHARSET_G1;
        ctx->screen[9][c].fg = VTX_YELLOW;
    }
    ctx->dirty[9] = 1;

    /* Auteur */
    p = "par Benedicte Marty";
    for (i = 0; p[i]; ++i) {
        ctx->screen[11][10 + i].ch = p[i];
        ctx->screen[11][10 + i].fg = VTX_WHITE;
    }
    ctx->dirty[11] = 1;

    /* Email */
    p = "bmarty@mailo.com";
    for (i = 0; p[i]; ++i) {
        ctx->screen[13][12 + i].ch = p[i];
        ctx->screen[13][12 + i].fg = VTX_CYAN;
    }
    ctx->dirty[13] = 1;

    /* Licence */
    p = "Licence GPL v3";
    for (i = 0; p[i]; ++i) {
        ctx->screen[15][13 + i].ch = p[i];
        ctx->screen[15][13 + i].fg = VTX_YELLOW;
    }
    ctx->dirty[15] = 1;

    /* Credits */
    p = "Telenet 2026";
    for (i = 0; p[i]; ++i) {
        ctx->screen[17][14 + i].ch = p[i];
        ctx->screen[17][14 + i].fg = VTX_GREEN;
    }
    ctx->dirty[17] = 1;

    /* Cadre bas */
    for (c = 5; c < 35; ++c) {
        ctx->screen[19][c].ch = 0x60;
        ctx->screen[19][c].charset = CHARSET_G1;
        ctx->screen[19][c].fg = VTX_YELLOW;
    }
    ctx->dirty[19] = 1;

    /* Message attente */
    p = "Appuyez sur une touche...";
    for (i = 0; p[i]; ++i) {
        ctx->screen[22][7 + i].ch = p[i];
        ctx->screen[22][7 + i].fg = VTX_WHITE;
        ctx->screen[22][7 + i].flags = ATTR_FLASH;
    }
    ctx->dirty[22] = 1;

    /* Afficher */
    display_render(ctx);

    /* Jouer le jingle */
    play_jingle();

    /* Attendre une touche ou ~5 secondes */
    {
        unsigned int timeout = 0;
        keyboard_init();
        while (timeout < 5000) {
            if (keyboard_scan() != KEY_NONE) break;
            delay_ms(1);
            ++timeout;
        }
    }

    /* Effacer l'ecran pour la connexion */
    vtx_clear_page(ctx);
    display_render(ctx);
}

/* Indicateur connexion sur ligne 0, col 38:
 * 'C' inverse = connecte, 'F' inverse = deconnecte */
static void set_connexion_indicator(vtx_context_t* ctx, unsigned char ch)
{
    ctx->screen[0][38].ch = ch;
    ctx->screen[0][38].charset = CHARSET_G0;
    ctx->screen[0][38].fg = VTX_WHITE;
    ctx->screen[0][38].bg = VTX_BLACK;
    ctx->screen[0][38].flags = ATTR_INVERT;
    ctx->screen[0][38].size = SIZE_NORMAL;
    ctx->dirty[0] = 1;
}

int main(void)
{
    unsigned char byte;
    unsigned char key;
    unsigned char got_data;         /* 1 si donnees recues cette iteration */
    unsigned int  idle_counter;     /* Compteur sans donnees */
    unsigned char connected;        /* 0=deconnecte, 1=connecte */

    vtx_init(&vtx);
    display_init();
    keyboard_init();

    /* Ecran splash avec jingle */
    splash_screen(&vtx);

    /* Message de connexion */
    {
        static const char msg[] = "Connexion en cours...";
        unsigned char i;
        for (i = 0; msg[i]; ++i)
            vtx.screen[12][10 + i].ch = msg[i];
        vtx.dirty[12] = 1;
    }

    /* Indicateur initial: F (pas encore connecte) */
    connected = 0;
    idle_counter = 0;
    set_connexion_indicator(&vtx, 'F');
    display_render(&vtx);

    /* Serial EN DERNIER */
    serial_init();

    for (;;) {

        /* 1. Drainer le ring buffer ISR */
        got_data = 0;
        while (serial_poll()) {
            byte = serial_recv();
            if (byte != 0xFF) {
                vtx_process(&vtx, byte);
                got_data = 1;
            }
        }

        /* 2. Gestion indicateur connexion */
        if (got_data) {
            idle_counter = 0;
            if (!connected) {
                connected = 1;
                set_connexion_indicator(&vtx, 'C');
            }
        } else {
            if (idle_counter < 60000u) {
                ++idle_counter;
            }
            /* ~30000 iterations sans donnees = timeout (~30s) */
            if (connected && idle_counter >= 30000u) {
                connected = 0;
                set_connexion_indicator(&vtx, 'F');
            }
        }

        /* 3. Rendre les lignes modifiees */
        display_render(&vtx);

        /* 4. Blink */
        ++blink_counter;
        if (blink_counter >= 250) {
            blink_counter = 0;
            vtx.blink_phase ^= 1;
            g_blink_phase = vtx.blink_phase;
        }

        /* 5. Clavier */
        key = keyboard_scan();
        if (key != KEY_NONE) {
            keyboard_process(key);
        }
    }

    return 0;
}
