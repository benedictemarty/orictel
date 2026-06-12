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

/* Compteur blink: bascule blink_phase toutes les ~250 iterations de la
 * boucle principale (duree dependante de la charge, ~0.5-1s en pratique) */
static unsigned char blink_counter;

/* Variable globale blink_phase accessible depuis display.c */
unsigned char g_blink_phase;

/* Mask global Videotex (ESC # $20 $58/$5F). 1 = cacher cellules concealed. */
unsigned char g_global_mask = 1;

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

    /* Silence + restaurer l'AY pour le scan clavier.
     * Register 7 bit 6 = 1 (Port A = input pour clavier Oric).
     * Sans ca, kbhit()/cgetc() ne fonctionnent plus. */
    ay_write(8, 0);  ay_write(9, 0);  ay_write(10, 0);
    ay_write(7, 0x7F);  /* Mixer: tout off, Port A input */
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
    display_render_all(ctx);

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
    display_render_all(ctx);
}

/* ===================================================================
 *  Initialisation modem AT (pour Oricutron --serial modem)
 *
 *  Envoie ATZ puis ATD pour se connecter au serveur.
 *  Si pas de modem (TCP direct Phosphoric), les commandes AT
 *  sont ignorees par le serveur Videotex (pas de reponse "OK").
 *  Timeout rapide: si pas de "OK" en ~2s, on passe en mode direct.
 * =================================================================== */

/* Mode de connexion */
#define MODE_MODEM  0
#define MODE_DIRECT 1

/* Menu selection du mode de connexion.
 * Retourne MODE_MODEM ou MODE_DIRECT. */
static unsigned char select_mode(vtx_context_t* ctx)
{
    unsigned char i;
    const char* p;

    vtx_clear_page(ctx);

    p = "Mode de connexion:";
    for (i = 0; p[i]; ++i) {
        ctx->screen[10][10 + i].ch = p[i];
        ctx->screen[10][10 + i].fg = VTX_WHITE;
    }
    ctx->dirty[10] = 1;

    p = "1 - Modem AT";
    for (i = 0; p[i]; ++i) {
        ctx->screen[13][12 + i].ch = p[i];
        ctx->screen[13][12 + i].fg = VTX_YELLOW;
    }
    ctx->screen[13][12].fg = VTX_CYAN;
    ctx->dirty[13] = 1;

    p = "2 - Direct (TCP/Digitelec)";
    for (i = 0; p[i]; ++i) {
        ctx->screen[15][12 + i].ch = p[i];
        ctx->screen[15][12 + i].fg = VTX_YELLOW;
    }
    ctx->screen[15][12].fg = VTX_CYAN;
    ctx->dirty[15] = 1;

    display_render_all(ctx);

    for (;;) {
        unsigned char key = keyboard_scan();
        if (key == '1') return MODE_MODEM;
        if (key == '2') return MODE_DIRECT;
    }
}

/* Serveurs disponibles */
static const char* servers[] = {
    "pavi.3617.fr:3617",
    "go.minipavi.fr:516",
};
static const char* server_names[] = {
    "PAVI 3617",
    "MiniPavi",
};
#define NUM_SERVERS 2

/* Envoyer une chaine AT via serie (pas de drain echo).
 * Flush bloquant: avant connexion, aucune reception a ne pas rater. */
static void at_send(const char* str)
{
    while (*str) {
        serial_send(*str);
        ++str;
    }
    serial_send(0x0D);  /* CR */
    serial_tx_flush();
}

/* Debug: position ecran pour afficher les octets recus */
static unsigned char dbg_col;
static unsigned char dbg_row;

static void dbg_init(unsigned char row)
{
    dbg_col = 1;
    dbg_row = row;
}

/* Afficher un octet en hex + ASCII sur l'ecran */
static void dbg_byte(vtx_context_t* ctx, unsigned char b)
{
    static const char hex[] = "0123456789ABCDEF";

    if (dbg_col >= 38) {
        dbg_col = 1;
        dbg_row++;
        if (dbg_row >= 24) dbg_row = 20;
    }

    /* Afficher en ASCII si imprimable, sinon en hex */
    if (b >= 0x20 && b < 0x7F) {
        ctx->screen[dbg_row][dbg_col].ch = b;
        ctx->screen[dbg_row][dbg_col].fg = VTX_GREEN;
        dbg_col++;
    } else {
        ctx->screen[dbg_row][dbg_col].ch = hex[b >> 4];
        ctx->screen[dbg_row][dbg_col].fg = VTX_RED;
        dbg_col++;
        ctx->screen[dbg_row][dbg_col].ch = hex[b & 0x0F];
        ctx->screen[dbg_row][dbg_col].fg = VTX_RED;
        dbg_col++;
    }
    ctx->dirty[dbg_row] = 1;
    /* Pas de rendu ici: l'appelant (at_wait_response) rend une fois
     * par rafale drainee, pas une fois par octet. */
}

/* Contexte global pour debug (accessible depuis at_wait_response) */
static vtx_context_t* dbg_ctx;

/* Attendre une reponse contenant un mot-cle dans le flux.
 * Affiche chaque octet recu sur l'ecran pour debug. */
static unsigned char at_wait_response(const char* keyword, unsigned int timeout_ms)
{
    unsigned int elapsed = 0;
    unsigned char ki = 0;

    while (elapsed < timeout_ms) {
        if (serial_poll()) {
            /* Drainer la rafale entiere, puis UN rendu */
            do {
                unsigned char b = serial_recv();
                dbg_byte(dbg_ctx, b);
                if (b == keyword[ki]) {
                    ++ki;
                    if (keyword[ki] == 0) {
                        display_render_all(dbg_ctx);
                        return 1;
                    }
                } else if (b == keyword[0]) {
                    ki = 1;
                } else {
                    ki = 0;
                }
            } while (serial_poll());
            display_render_all(dbg_ctx);
        } else {
            delay_ms(10);
            elapsed += 10;
        }
    }
    return 0;
}

/* Buffer pour saisie libre du serveur */
static char custom_server[40];

/* Menu de selection serveur.
 * Retourne 0-1 pour les predefinis, 255 pour saisie libre. */
static unsigned char select_server(vtx_context_t* ctx)
{
    unsigned char i, j, sel;
    const char* title = "Serveur:";
    const char* p;

    for (i = 0; title[i]; ++i) {
        ctx->screen[10][5 + i].ch = title[i];
        ctx->screen[10][5 + i].fg = VTX_WHITE;
    }
    ctx->dirty[10] = 1;

    for (sel = 0; sel < NUM_SERVERS; ++sel) {
        ctx->screen[12 + sel * 2][5].ch = '1' + sel;
        ctx->screen[12 + sel * 2][5].fg = VTX_CYAN;
        ctx->screen[12 + sel * 2][7].ch = '-';
        ctx->screen[12 + sel * 2][7].fg = VTX_WHITE;
        p = server_names[sel];
        for (j = 0; p[j]; ++j) {
            ctx->screen[12 + sel * 2][9 + j].ch = p[j];
            ctx->screen[12 + sel * 2][9 + j].fg = VTX_YELLOW;
        }
        ctx->dirty[12 + sel * 2] = 1;
    }

    /* Option 3: saisie libre */
    {
        unsigned char row = 12 + NUM_SERVERS * 2;
        ctx->screen[row][5].ch = '3';
        ctx->screen[row][5].fg = VTX_CYAN;
        ctx->screen[row][7].ch = '-';
        ctx->screen[row][7].fg = VTX_WHITE;
        p = "Autre (host:port)";
        for (j = 0; p[j]; ++j) {
            ctx->screen[row][9 + j].ch = p[j];
            ctx->screen[row][9 + j].fg = VTX_YELLOW;
        }
        ctx->dirty[row] = 1;
    }
    display_render_all(ctx);

    /* Attendre touche 1, 2 ou 3 */
    for (;;) {
        unsigned char key = keyboard_scan();
        if (key >= '1' && key < '1' + NUM_SERVERS) {
            return key - '1';
        }
        if (key == '3') {
            /* Saisie libre du serveur */
            unsigned char row = 12 + NUM_SERVERS * 2 + 2;
            unsigned char pos = 0;
            p = "host:port> ";
            for (j = 0; p[j]; ++j) {
                ctx->screen[row][3 + j].ch = p[j];
                ctx->screen[row][3 + j].fg = VTX_WHITE;
            }
            ctx->dirty[row] = 1;
            display_render_all(ctx);

            /* Boucle de saisie */
            for (;;) {
                key = keyboard_scan();
                if (key == KEY_NONE) continue;
                if ((key & KEY_FUNC_FLAG) && (key & 0x7F) == KEY_ENVOI) {
                    /* RETURN = valider */
                    custom_server[pos] = 0;
                    if (pos > 0) return 255;
                } else if (key == 0x7F || key == 0x08) {
                    /* DELETE/BS = effacer */
                    if (pos > 0) {
                        --pos;
                        ctx->screen[row][14 + pos].ch = ' ';
                        ctx->dirty[row] = 1;
                        display_render_all(ctx);
                    }
                } else if (key >= 0x20 && key < 0x7F && pos < 38) {
                    /* Caractere normal */
                    custom_server[pos] = key;
                    ctx->screen[row][14 + pos].ch = key;
                    ctx->screen[row][14 + pos].fg = VTX_GREEN;
                    ctx->dirty[row] = 1;
                    display_render_all(ctx);
                    ++pos;
                }
            }
        }
    }
}

/* Tenter la connexion modem AT. Retourne 1 si connecte. */
static unsigned char modem_connect(vtx_context_t* ctx, unsigned char server_idx)
{
    unsigned char i;
    const char* msg;

    /* Afficher "Connexion..." */
    vtx_clear_page(ctx);
    msg = "ATZ...";
    for (i = 0; msg[i]; ++i) {
        ctx->screen[10][17 + i].ch = msg[i];
        ctx->screen[10][17 + i].fg = VTX_WHITE;
    }
    ctx->dirty[10] = 1;
    display_render_all(ctx);

    /* ATZ - reset modem */
    dbg_ctx = ctx;
    dbg_init(12);
    at_send("ATZ");
    if (!at_wait_response("OK", 3000)) {
        return 0;  /* Pas de modem (mode TCP direct) */
    }

    /* Determiner le serveur */
    {
        const char* srv;
        unsigned char j;

        if (server_idx == 255) {
            srv = custom_server;
        } else {
            srv = servers[server_idx];
        }

        /* Afficher "ATD serveur..." */
        vtx_clear_page(ctx);
        msg = "ATD ";
        for (i = 0; msg[i]; ++i) {
            ctx->screen[10][5 + i].ch = msg[i];
            ctx->screen[10][5 + i].fg = VTX_WHITE;
        }
        for (j = 0; srv[j]; ++j) {
            ctx->screen[10][9 + j].ch = srv[j];
            ctx->screen[10][9 + j].fg = VTX_CYAN;
        }
        ctx->dirty[10] = 1;
        display_render_all(ctx);

        /* ATD serveur:port */
        dbg_init(12);
        serial_send('A'); serial_send('T'); serial_send('D');
        while (*srv) { serial_send(*srv); ++srv; }
        serial_send(0x0D);
        serial_tx_flush();
    }

    /* Attendre CONNECT (~10s timeout) */
    if (at_wait_response("CONNECT", 10000)) {
        /* Drainer UNIQUEMENT le \r\n apres CONNECT (pas les donnees Videotex) */
        {
            unsigned char drain = 0;
            while (drain < 5) {
                if (serial_poll()) {
                    unsigned char b = serial_recv();
                    if (b == 0x0D || b == 0x0A) {
                        ++drain;
                        continue;
                    }
                    /* Premier octet non-CRLF = debut Videotex: le
                     * passer au decodeur, pas le jeter (un ESC mange
                     * ici desynchronisait la 1ere sequence serveur) */
                    vtx_process(ctx, b);
                    break;
                }
                delay_ms(10);
                ++drain;
            }
        }
        return 1;
    }
    return 0;
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
    /* vtx_touch (pas dirty[0]=1): la ligne 0 peut deja porter un span
     * retreci par le decodeur, il faut l'etendre jusqu'a la col 38 */
    vtx_touch(ctx, 0, 38, 38);
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

    {
        unsigned char mode = select_mode(&vtx);
        unsigned char srv_idx;

        vtx_clear_page(&vtx);
        srv_idx = select_server(&vtx);
        vtx_clear_page(&vtx);

        if (mode == MODE_MODEM) {
            /* Mode modem AT (le backend emule repond immediatement,
             * 100 ms suffisent pour la stabilisation) */
            serial_init();
            delay_ms(100);
            modem_connect(&vtx, srv_idx);
            vtx_clear_page(&vtx);
        } else {
            /* Mode direct (TCP + V23): connexion immediate.
             * Drainer les eventuelles donnees arrivees pendant les menus. */
            serial_init();
            while (serial_poll()) serial_recv();
            vtx_clear_page(&vtx);
        }
    }

    /* Indicateur initial: F */
    connected = 0;
    idle_counter = 0;
    set_connexion_indicator(&vtx, 'F');
    display_render_all(&vtx);

    for (;;) {

        /* 0. Emettre un octet en attente si le transmetteur est pret.
         * Les reponses protocole (ACK PRO, ENQROM...) sont empilees
         * par vtx_process et partent ici, sans jamais bloquer la
         * lecture RX sur l'attente TDRE. */
        serial_tx_pump();

        /* 1. Drainer le FIFO de reception */
        got_data = 0;
        while (serial_poll()) {
            byte = serial_recv();
            vtx_process(&vtx, byte);
            got_data = 1;
            serial_tx_pump();
        }

        /* 2. Clavier AVANT le rendu: le latch clavier de la ROM ne
         * retient que la derniere touche pressee; le lire en premier
         * (et un rendu budgete en 4.) borne la fenetre pendant
         * laquelle une frappe peut etre ecrasee par la suivante. */
        key = keyboard_scan();
        if (key == KEY_TOGGLE_RENDER) {
            extern unsigned char g_render_mode;
            g_render_mode++;
            if (g_render_mode > 2) g_render_mode = 0;
            vtx.full_refresh = 1;
        } else if (key == KEY_LOCAL_CLEAR) {
            vtx_clear_page(&vtx);
            vtx.full_refresh = 1;
        } else if (key == KEY_LOCAL_RESET) {
            serial_init();
            display_status("ACIA reset");
        } else if (key != KEY_NONE) {
            keyboard_process(&vtx, key);
            serial_tx_pump();   /* faire partir la frappe sans attendre */
        }

        /* 3. Gestion indicateur connexion */
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

        /* 4. Rendu adaptatif: 2 lignes par passe, puis continuer tant
         * que rien d'autre n'attend (ni octet serie, ni touche). Une
         * page se peint a pleine vitesse moteur quand la machine est
         * libre, et la main revient au clavier des qu'il le faut. */
        display_render(&vtx);
        while (display_dirty_pending(&vtx) &&
               !serial_poll() && !keyboard_pending()) {
            display_render(&vtx);
            serial_tx_pump();
        }

        /* 5. Blink */
        ++blink_counter;
        if (blink_counter >= 250) {
            blink_counter = 0;
            vtx.blink_phase ^= 1;
            g_blink_phase = vtx.blink_phase;
        }
    }

    return 0;
}
