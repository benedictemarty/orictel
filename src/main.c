/**
 * @file main.c
 * @brief OricTel - Terminal Minitel 1B pour Oric 1/Atmos
 *
 * Montage materiel unique: LOCI + PicoWiFiModemUSB (ACIA 6551 @ $0380). Le
 * Pico est un modem Hayes; la connexion passe TOUJOURS par les commandes AT
 * (ATZ/ATDT). Pas de mode "Direct"/V23 brut.
 *
 * Transports possibles cote hote (tous via le modem AT du Pico):
 *
 * 1. PicoWiFiModemUSB reel via LOCI, ou son emulation Phosphoric:
 *    ./oric1-emu --serial picowifi:OricTel --acia-addr 0380
 *    ou --loci --serial com:9600,8,N,1,/dev/ttyACM0 (Pico physique).
 *
 * 2. Backend TCP + bridge WebSocket (ws://3617.fr):
 *    python3 bridge/orictel_bridge.py &
 *    ./oric1-emu --serial tcp:127.0.0.1:3615 --serial-buffer 256 --serial-irq-on-rdrf
 */

#include <string.h>
#include "serial.h"
#include "videotex.h"
#include "display.h"
#include "keyboard.h"
#include "at_modem.h"
#include "ui.h"

/* Version OricTel affichee au splash. A garder synchronisee avec CHANGELOG /
 * VERSION_TRACKING a chaque release. */
#define ORICTEL_VERSION "v0.3.19"

/* Silence exige, en MILLISECONDES, pour CONFIRMER une presomption de perte de
 * porteuse (un vrai NO CARRIER n'est suivi de RIEN, une page qui citerait ces
 * mots continuerait de defiler).
 *
 * Historique : la constante etait un NOMBRE D'ITERATIONS de la boucle de
 * session (CARRIER_CONFIRM_IDLE = 500), mesure sur materiel a 3,6-5,6 s ; a
 * 2000 elle valait 13 a 21 s. Toute optimisation du rendu ou du decodeur la
 * raccourcissait mecaniquement. Elle est desormais comptee sur une base de
 * temps reelle (via_tick_10ms, Timer 2 du VIA), independante du cout de la
 * boucle : 4000 ms, au milieu de la plage mesuree.
 *
 * La marge anti-faux-positif reste large : 4 s de silence, c'est ~480 octets
 * a 1200 bauds, une pause qu'un serveur en pleine emission de page ne fait
 * pas. */
#define CARRIER_CONFIRM_MS   4000u
#define CARRIER_CONFIRM_TICKS (CARRIER_CONFIRM_MS / 10u)

/* Silence apres lequel l'indicateur de connexion repasse a 'F' (meme base de
 * temps). Etait "30000 iterations (~30 s)" : a ~8,5 ms l'iteration a vide,
 * c'etait en realite plus de 4 minutes. */
#define LINK_IDLE_MS         30000u
#define LINK_IDLE_TICKS      (LINK_IDLE_MS / 10u)

/* --- Base de temps : Timer 2 du VIA 6522 ----------------------------------
 * T2 n'est utilise par la ROM (1.0 comme 1.1) que pendant la lecture
 * cassette ; hors de la, en mode "one-shot" (ACR bit 5 = 0), il DECOMPTE en
 * continu a 1 MHz et boucle a $FFFF une fois arme : un chronometre 16 bits
 * gratuit, sans ISR. Son drapeau IFR (bit 5) n'est pas source d'IRQ (IER ROM = T1 seul), le
 * lire ne vole donc rien a la ROM - contrairement a T1, dont la lecture de
 * l'octet bas ACQUITTE l'interruption 100 Hz a la place de la ROM.
 *
 * On accumule les cycles ecoules entre deux appels (soustraction modulo
 * 65536) et on en tire des tics de 10 ms. Deux lectures non atomiques : une
 * retenue entre l'octet bas et l'octet haut fausse UN echantillon de 256
 * cycles, que le suivant corrige (somme telescopique : seule la valeur
 * absolue compte). Un appel doit revenir en moins de 65 ms, sinon des cycles
 * sont perdus, ce qui ALLONGE le delai (jamais ne le raccourcit : sens sur
 * pour une confirmation).
 *
 * Premiere version (v0.3.18, abandonnee) : detection de la remontee de
 * l'octet haut de T1, recharge toutes les 10 ms par la ROM. Mesure sur
 * emulateur a 6,1 s pour 4 s demandees : une iteration de plus de 10 ms
 * masque la remontee, et la boucle de session au repos en est proche. */
#define VIA_T2_LO  (*(volatile unsigned char*)0x0308)
#define VIA_T2_HI  (*(volatile unsigned char*)0x0309)
#define VIA_IER    (*(volatile unsigned char*)0x030E)
#define CYCLES_PER_TICK 10000u

static unsigned int s_t2_prev;   /* T2 au dernier echantillon */
static unsigned int s_t2_acc;    /* cycles accumules, < CYCLES_PER_TICK */

static unsigned int via_t2_read(void)
{
    unsigned char lo = VIA_T2_LO;
    unsigned char hi = VIA_T2_HI;
    return ((unsigned int)hi << 8) | lo;
}

static void via_tick_reset(void)
{
    /* Armer T2 nous-memes : un 6522 emule (Phosphoric) laisse le compteur
     * FIGE tant que T2C-H n'a jamais ete ecrit, la ou la puce reelle
     * decompte des la mise sous tension. Et interdire son IRQ (IER bit 7 = 0
     * : les bits a 1 sont EFFACES) : le handler ROM n'acquitte que T1, un
     * drapeau T2 laisse actif serait une tempete d'IRQ (cf. serial_asm.s). */
    VIA_IER   = 0x20;
    VIA_T2_LO = 0xFF;
    VIA_T2_HI = 0xFF;
    s_t2_prev = via_t2_read();
    s_t2_acc = 0;
}

/* Nombre de tics de 10 ms ecoules depuis l'appel precedent. */
static unsigned char via_tick_10ms(void)
{
    unsigned int now = via_t2_read();
    unsigned char ticks = 0;

    s_t2_acc += (unsigned int)(s_t2_prev - now);    /* T2 decompte */
    s_t2_prev = now;
    while (s_t2_acc >= CYCLES_PER_TICK) {
        s_t2_acc -= CYCLES_PER_TICK;
        ++ticks;
    }
    return ticks;
}

/* Contexte Videotex global */
static vtx_context_t vtx;

/* Compteur blink: bascule blink_phase toutes les ~250 iterations de la
 * boucle principale (duree dependante de la charge, ~0.5-1s en pratique) */
static unsigned char blink_counter;

/* Variable globale blink_phase accessible depuis display.c */
unsigned char g_blink_phase;

/* Mask global Videotex (ESC # $20 $58/$5F). 1 = cacher cellules concealed. */
unsigned char g_global_mask = 1;

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

/* Les helpers d'affichage/saisie des menus (ui_print, ui_menu_item,
 * ui_text_input) vivent desormais dans ui.c (testables host: tests/test_ui.c). */

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
    ui_print(ctx, 7, 17, ORICTEL_VERSION, VTX_WHITE);

    /* Trait de separation */
    for (c = 5; c < 35; ++c) {
        ctx->screen[9][c].ch = 0x60;
        ctx->screen[9][c].charset = CHARSET_G1;
        ctx->screen[9][c].fg = VTX_YELLOW;
    }
    ctx->dirty[9] = 1;

    /* Auteur / email / licence */
    ui_print(ctx, 11, 10, "par Benedicte Marty", VTX_WHITE);
    ui_print(ctx, 13, 12, "bmarty@mailo.com", VTX_CYAN);
    ui_print(ctx, 15, 12, "Licence EUPL 1.2", VTX_YELLOW);

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
 *  Initialisation modem AT (Phosphoric --serial modem, ou PicoWiFiModemUSB
 *  reel via LOCI).
 *
 *  Envoie ATZ puis ATDT pour se connecter au serveur.
 *  Si aucun modem ne repond "OK" en ~3s (timeout rapide), on abandonne
 *  le handshake et on entre quand meme en session (indicateur "F").
 * =================================================================== */

/* Mode de connexion. Le PicoWiFiModemUSB est un modem AT : la connexion
 * passe OBLIGATOIREMENT par les commandes Hayes (ATD...). L'ancien mode
 * "Direct (TCP)" (ligne V23 brute sans AT) ne s'applique pas a ce montage
 * et a ete retire. */
#define MODE_MODEM  0
#define MODE_WIFI   2   /* page de configuration WiFi du PicoWiFiModemUSB */
#define MODE_QUIT   3   /* ESC : sortir d'OricTel (redemarrage a froid ROM) */

/* Sortie d'OricTel : saut par le VECTEUR DE RESET ($FFFC), donc identique
 * sur Oric-1 (BASIC 1.0) et Atmos (1.1) sans connaitre l'adresse ROM. Le
 * demarrage a froid reinitialise VIA, mode TEXT et pointeurs BASIC : la zone
 * programme ecrasee par OricTel ne compte plus, on retombe sur "Ready".
 * Un simple RTS vers la ROM ne le ferait pas (programme BASIC detruit). */
static void oric_cold_reset(void)
{
    __asm__("sei");
    __asm__("jmp ($FFFC)");
}

/* Menu selection du mode de connexion.
 * Retourne MODE_MODEM, MODE_WIFI ou MODE_QUIT (ESC). */
static unsigned char select_mode(vtx_context_t* ctx)
{
    vtx_clear_page(ctx);

    ui_print(ctx, 10, 10, "Mode de connexion:", VTX_WHITE);
    ui_menu_item(ctx, 13, "1 - Modem AT");
    ui_menu_item(ctx, 15, "2 - Config WiFi");
    ui_print(ctx, 19, 12, "ESC Quitter (BASIC)", VTX_WHITE);

    display_render_all(ctx);

    keyboard_flush();           /* anti-rebond: attendre relachement avant lecture */
    for (;;) {
        unsigned char key = keyboard_scan();
        if (key == '1') return MODE_MODEM;
        if (key == '2') return MODE_WIFI;
        if (key == KEY_LOCAL_ESCAPE) return MODE_QUIT;
    }
}

/* Ecran d'interface serie. OricTel ne connait qu'un seul montage materiel :
 * LOCI + PicoWiFiModemUSB sur l'ACIA 6551 a la base LOCI ($0380). Les 3
 * variantes d'exploitation (tout physique ; Phosphoric tout emule ; Phosphoric
 * LOCI emule + Pico physique sur USB de l'hote) sont identiques cote firmware,
 * d'ou un simple ecran de rappel : aucune selection, une touche pour continuer.
 * Retourne toujours ACIA_BASE_LOCI. */
static unsigned select_interface(vtx_context_t* ctx)
{
    vtx_clear_page(ctx);

    ui_print(ctx, 8, 10, "Interface serie:", VTX_WHITE);
    ui_print(ctx, 11, 12, "LOCI + PicoWiFiModemUSB", VTX_YELLOW);
    ui_print(ctx, 12, 12, "($0380)", VTX_CYAN);
    ui_print(ctx, 16, 8, "[une touche] pour continuer", VTX_WHITE);

    display_render_all(ctx);

    keyboard_flush();           /* anti-rebond: attendre relachement avant lecture */
    while (keyboard_scan() == KEY_NONE) {
        /* attente d'un appui */
    }
    return ACIA_BASE_LOCI;
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

/* ===================================================================
 *  Trace de debug de la phase AT (compilee uniquement si DEBUG)
 *
 *  Les primitives AT (at_send, at_wait_response, at_wait_ip) vivent
 *  desormais dans at_modem.c, decouplees du rendu. L'affichage des
 *  octets recus est branche ici via les hooks at_set_trace() et n'est
 *  compile qu'en build DEBUG. En production, aucune trace n'est posee:
 *  la phase AT est muette et ne declenche aucun rendu (donc aucun risque
 *  d'overrun du 6551 reel).
 *
 *  Macros utilisees par modem_connect / wifi_config_page :
 *    DBG_AT_BEGIN(ctx, row) : arme la trace a partir de la ligne 'row'
 *    (no-op hors DEBUG).
 * =================================================================== */
#ifdef DEBUG
static unsigned char dbg_col;
static unsigned char dbg_row;
static vtx_context_t* dbg_ctx;

/* Afficher un octet recu en hex + ASCII (hook at_trace_byte). */
static void dbg_byte(unsigned char b)
{
    static const char hex[] = "0123456789ABCDEF";

    if (dbg_col >= 38) {
        dbg_col = 1;
        dbg_row++;
        if (dbg_row >= 24) dbg_row = 20;
    }
    if (b >= 0x20 && b < 0x7F) {
        dbg_ctx->screen[dbg_row][dbg_col].ch = b;
        dbg_ctx->screen[dbg_row][dbg_col].fg = VTX_GREEN;
        dbg_col++;
    } else {
        dbg_ctx->screen[dbg_row][dbg_col].ch = hex[b >> 4];
        dbg_ctx->screen[dbg_row][dbg_col].fg = VTX_RED;
        dbg_col++;
        dbg_ctx->screen[dbg_row][dbg_col].ch = hex[b & 0x0F];
        dbg_ctx->screen[dbg_row][dbg_col].fg = VTX_RED;
        dbg_col++;
    }
    dbg_ctx->dirty[dbg_row] = 1;
}

/* Rendu differe au creux de reception (hook at_idle): seul moment ou un
 * rendu est sans danger pour l'overrun. */
static void dbg_idle(void)
{
    display_render_all(dbg_ctx);
}

#define DBG_AT_BEGIN(ctx, row)                       \
    do {                                             \
        dbg_ctx = (ctx);                             \
        dbg_col = 1;                                  \
        dbg_row = (row);                             \
        at_set_trace(dbg_byte, dbg_idle);            \
    } while (0)
#else
#define DBG_AT_BEGIN(ctx, row) ((void)0)
#endif

/* ===================================================================
 *  Page de configuration WiFi du PicoWiFiModemUSB
 *
 *  Scan (AT$SCAN) -> selection -> mot de passe -> connexion (ATC1)
 *  -> attente IP (at_wait_ip) -> sauvegarde (AT&W).
 * =================================================================== */

#define WIFI_MAX 8                  /* reseaux affichables a l'ecran */
static char wifi_ssid[WIFI_MAX][33];/* SSID (<=32 car + nul) */
static char wifi_sec[WIFI_MAX];     /* 'S'=securise, 'O'=ouvert */
static unsigned char wifi_count;    /* reseaux trouves */
static char wifi_pass[40];          /* mot de passe saisi */

/* Scanner les reseaux WiFi via AT$SCAN. Le firmware liste un point
 * d'acces par ligne sous la forme "<index> <ssid><TAB><sec>". On lit
 * ligne par ligne jusqu'au "OK" final (ou timeout: le scan radio prend
 * plusieurs secondes). Remplit wifi_ssid/wifi_sec, retourne le nombre. */
static unsigned char wifi_scan(void)
{
    char line[40];
    unsigned char lp = 0;
    unsigned int  elapsed = 0;

    wifi_count = 0;
    at_send("AT$SCAN");

    while (elapsed < 9000) {
        if (serial_poll()) {
            unsigned char b = serial_recv();
            if (b == 0x0D || b == 0x0A) {
                line[lp] = 0;
                if (lp >= 2 && line[0] == 'O' && line[1] == 'K') {
                    return wifi_count;          /* fin du scan */
                }
                if (lp > 0 && line[0] >= '0' && line[0] <= '9'
                    && wifi_count < WIFI_MAX) {
                    unsigned char k = 0;
                    unsigned char d = 0;
                    while (line[k] >= '0' && line[k] <= '9') ++k;  /* index */
                    while (line[k] == ' ') ++k;                    /* espaces */
                    while (line[k] && line[k] != 0x09 && d < 32) { /* ssid */
                        wifi_ssid[wifi_count][d++] = line[k++];
                    }
                    wifi_ssid[wifi_count][d] = 0;
                    if (line[k] == 0x09) ++k;                      /* TAB */
                    wifi_sec[wifi_count] = (line[k] == 'S') ? 'S' : 'O';
                    if (d > 0) ++wifi_count;
                }
                lp = 0;
            } else if (lp < 39) {
                line[lp++] = b;
            }
        } else {
            delay_ms(10);
            elapsed += 10;
        }
    }
    return wifi_count;
}

/* Afficher un message centre-ish sur une ligne et attendre une touche. */
static void wifi_msg_wait(vtx_context_t* ctx, unsigned char row,
                          unsigned char col, const char* msg)
{
    ui_print(ctx, row, col, msg, VTX_WHITE);
    display_render_all(ctx);
    while (keyboard_scan() == KEY_NONE) { /* attente touche */ }
}

/* Page complete de configuration WiFi. Suppose serial_init() deja fait. */
static void wifi_config_page(vtx_context_t* ctx)
{
    unsigned char i, sel;

    for (;;) {                                  /* boucle scan/rescan */
        vtx_clear_page(ctx);
        ui_print(ctx, 10, 9, "Scan WiFi en cours...", VTX_WHITE);
        display_render_all(ctx);

        if (wifi_scan() == 0) {
            vtx_clear_page(ctx);
            wifi_msg_wait(ctx, 10, 6, "Aucun reseau. Touche=retour");
            return;
        }

        /* Liste des reseaux */
        vtx_clear_page(ctx);
        ui_print(ctx, 2, 3, "Reseaux WiFi:", VTX_WHITE);
        for (i = 0; i < wifi_count; ++i) {
            unsigned char row = 4 + i;
            unsigned char d;
            ctx->screen[row][3].ch = '1' + i;
            ctx->screen[row][3].fg = VTX_CYAN;
            ctx->screen[row][5].ch = '-';
            ctx->screen[row][5].fg = VTX_WHITE;
            for (d = 0; wifi_ssid[i][d] && (7 + d) < VTX_COLS; ++d) {
                ctx->screen[row][7 + d].ch = wifi_ssid[i][d];
                ctx->screen[row][7 + d].fg = VTX_YELLOW;
            }
            if (wifi_sec[i] == 'S' && (7 + d + 1) < VTX_COLS) {  /* cadenas */
                ctx->screen[row][7 + d + 1].ch = '*';
                ctx->screen[row][7 + d + 1].fg = VTX_RED;
            }
            ctx->dirty[row] = 1;
        }
        ui_print(ctx, 22, 0, "Chiffre=choix REPET=rescan ESC=retour",
                 VTX_GREEN);
        display_render_all(ctx);

        /* Selection */
        sel = 0xFF;
        keyboard_flush();       /* anti-rebond avant la selection reseau */
        for (;;) {
            unsigned char key = keyboard_scan();
            if (key == KEY_NONE) continue;
            if (key >= '1' && key < '1' + wifi_count) {
                sel = key - '1';
                break;
            }
            if ((key & KEY_FUNC_FLAG) &&
                (key & 0x7F) == KEY_REPETITION) {
                break;                          /* rescan */
            }
            if (key == KEY_LOCAL_ESCAPE ||
                ((key & KEY_FUNC_FLAG) &&
                 (key & 0x7F) == KEY_ANNULATION)) {
                return;                         /* annuler */
            }
        }
        if (sel == 0xFF) continue;              /* rescan demande */

        /* Mot de passe si reseau securise */
        wifi_pass[0] = 0;
        if (wifi_sec[sel] == 'S') {
            vtx_clear_page(ctx);
            ui_print(ctx, 8, 3, "Mot de passe WiFi:", VTX_WHITE);
            display_render_all(ctx);
            /* col 3, masque '*' ; longueur bornee par le buffer et l'ecran */
            ui_text_input(ctx, 10, 3, wifi_pass, sizeof(wifi_pass), '*');
        }

        /* Configuration + connexion */
        vtx_clear_page(ctx);
        ui_print(ctx, 10, 11, "Connexion WiFi...", VTX_WHITE);
        display_render_all(ctx);
        DBG_AT_BEGIN(ctx, 12);

        at_send_kv("AT$SSID=", wifi_ssid[sel]);
        at_wait_response("OK", 3000);
        at_send_kv("AT$PASS=", wifi_pass);
        at_wait_response("OK", 3000);
        at_send("ATC1");
        at_wait_response("OK", 8000);

        if (at_wait_ip(20000)) {
            at_send("AT&W");                    /* sauver en NVRAM */
            at_wait_response("OK", 3000);
            wifi_msg_wait(ctx, 18, 5, "Connecte! Config sauvee.");
        } else {
            wifi_msg_wait(ctx, 18, 3, "Echec IP. Verifier mot de passe.");
        }
        return;
    }
}

/* Buffer pour saisie libre du serveur */
static char custom_server[40];

/* Menu de selection serveur.
 * Retourne 0-1 pour les predefinis, 255 pour saisie libre. */
static unsigned char select_server(vtx_context_t* ctx)
{
    unsigned char sel, n;

    ui_print(ctx, 10, 5, "Serveur:", VTX_WHITE);

    for (sel = 0; sel < NUM_SERVERS; ++sel) {
        unsigned char row = 12 + sel * 2;
        ctx->screen[row][5].ch = '1' + sel;
        ctx->screen[row][5].fg = VTX_CYAN;
        ctx->screen[row][7].ch = '-';
        ctx->screen[row][7].fg = VTX_WHITE;
        ui_print(ctx, row, 9, server_names[sel], VTX_YELLOW);
    }

    /* Option 3: saisie libre */
    {
        unsigned char row = 12 + NUM_SERVERS * 2;
        ctx->screen[row][5].ch = '3';
        ctx->screen[row][5].fg = VTX_CYAN;
        ctx->screen[row][7].ch = '-';
        ctx->screen[row][7].fg = VTX_WHITE;
        ui_print(ctx, row, 9, "Autre (host:port)", VTX_YELLOW);
    }
    display_render_all(ctx);

    /* Attendre touche 1, 2 ou 3 */
    keyboard_flush();           /* anti-rebond: attendre relachement avant lecture */
    for (;;) {
        unsigned char key = keyboard_scan();
        if (key >= '1' && key < '1' + NUM_SERVERS) {
            return key - '1';
        }
        if (key == '3') {
            /* Saisie libre du serveur : invite "host:port> " (col 3..13),
             * saisie bornee a partir de la col 14 via ui_text_input. */
            unsigned char row = 12 + NUM_SERVERS * 2 + 2;
            ui_print(ctx, row, 3, "host:port> ", VTX_WHITE);
            display_render_all(ctx);
            n = ui_text_input(ctx, row, 14, custom_server,
                              sizeof(custom_server), 0);
            if (n != 0xFF && n > 0) return 255;   /* valide -> serveur perso */
            /* ANNULATION ou saisie vide: nettoyer la ligne, revenir au menu */
            {
                unsigned char c;
                for (c = 0; c < VTX_COLS; ++c) ctx->screen[row][c].ch = ' ';
                ctx->dirty[row] = 1;
            }
            display_render_all(ctx);
        }
    }
}

/* Tenter la connexion modem AT. Retourne 1 si connecte. */
static unsigned char modem_connect(vtx_context_t* ctx, unsigned char server_idx)
{
    /* Afficher "Connexion..." */
    vtx_clear_page(ctx);
    ui_print(ctx, 10, 17, "ATZ...", VTX_WHITE);
    display_render_all(ctx);

    /* ATZ - reset modem */
    DBG_AT_BEGIN(ctx, 12);
    at_send("ATZ");
    if (!at_wait_response("OK", 3000)) {
        /* Pas de "OK" : deux causes possibles.
         *  a) aucun modem branche -> rien a faire ;
         *  b) le modem est reste EN LIGNE d'une session precedente (Oric
         *     resette, ou OricTel relance sans raccrocher). Il transmet
         *     alors les donnees du serveur et n'interprete plus les
         *     commandes : le ATZ tombe dans le flux Videotex, le "OK"
         *     n'arrive jamais, on n'appelle jamais, et la session decode un
         *     flux commence en cours de page (premiere page illisible).
         * On tente donc l'echappement Hayes + ATH, puis on rejoue ATZ. Ce
         * chemin ne coute rien dans le cas nominal (le 1er ATZ a repondu). */
        vtx_clear_page(ctx);
        ui_print(ctx, 10, 15, "Modem en ligne: ATH...", VTX_WHITE);
        display_render_all(ctx);
        DBG_AT_BEGIN(ctx, 12);
        at_hangup();

        vtx_clear_page(ctx);
        ui_print(ctx, 10, 17, "ATZ...", VTX_WHITE);
        display_render_all(ctx);
        DBG_AT_BEGIN(ctx, 12);
        at_send("ATZ");
        if (!at_wait_response("OK", 3000)) {
            return 0;  /* Aucun modem ne repond -> session sans connexion */
        }
    }

    /* ATZ a pu relancer l'association WiFi du PicoWiFiModemUSB: patienter
     * jusqu'a "CONNECTED TO WIFI" (IP prete) avant de composer, sinon ATD
     * echoue par NO CARRIER. Quasi immediat si le modem est deja connecte,
     * et immediat aussi sur un modem SANS WiFi (backend --serial modem):
     * at_wait_ip detecte l'absence de sous-systeme WiFi et sort tout de
     * suite au lieu d'attendre les 15 s. */
    vtx_clear_page(ctx);
    ui_print(ctx, 10, 11, "Attente IP WiFi...", VTX_WHITE);
    display_render_all(ctx);
    DBG_AT_BEGIN(ctx, 12);
    at_wait_ip(15000);

    /* Determiner le serveur */
    {
        const char* srv;

        if (server_idx == 255) {
            srv = custom_server;
        } else {
            srv = servers[server_idx];
        }

        /* Afficher "ATDT serveur..." */
        vtx_clear_page(ctx);
        ui_print(ctx, 10, 5, "ATDT ", VTX_WHITE);
        ui_print(ctx, 10, 10, srv, VTX_CYAN);
        display_render_all(ctx);

        /* ATDT serveur:port : le 'T' (tonalite) est INDISPENSABLE. Sans lui,
         * le PicoWiFiModemUSB prend le 1er caractere de l'hote pour un
         * modificateur Hayes (ex: "ATDpavi..." -> 'p' = pulse, l'hote devient
         * "avi.3617.fr" -> NO CARRIER 00:00:00). "ATDT" + hote se connecte. */
        DBG_AT_BEGIN(ctx, 12);
        serial_send('A'); serial_send('T'); serial_send('D'); serial_send('T');
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

/* Ecran d'echec de connexion.
 *
 * Sans "CONNECT", la ligne ne porte AUCUN flux Videotex exploitable : soit
 * rien du tout (modem absent/muet), soit - si le raccrochage a echoue - un
 * flux commence EN COURS DE PAGE, que le decodeur affiche en bouillie
 * (premiere page illisible). Entrer en session dans ces conditions ne peut
 * rien donner de bon, donc on ne le fait plus en silence : l'utilisateur
 * decide.
 *
 * Retour : 1 = reessayer, 0 = entrer en session malgre tout (ancien
 * comportement, desormais un choix explicite), 2 = ESC, abandonner et
 * revenir au menu principal. Peut modifier *srv_idx.
 */
static unsigned char connect_failed_page(vtx_context_t* ctx,
                                         unsigned char* srv_idx)
{
    unsigned char key;

    vtx_clear_page(ctx);
    ui_print(ctx, 6, 11, "ECHEC DE CONNEXION", VTX_RED);
    ui_print(ctx, 8,  3, "Pas de CONNECT: la ligne ne porte", VTX_WHITE);
    ui_print(ctx, 9,  3, "aucun flux Videotex exploitable.", VTX_WHITE);
    ui_menu_item(ctx, 12, "1 Reessayer");
    ui_menu_item(ctx, 14, "2 Choisir un autre serveur");
    /* Libelles bornes a 28 caracteres : ui_menu_item ecrit en colonne 12, et
     * ui_print clippe a 40 colonnes. "3 Entrer en session quand meme" (30 car.)
     * s'affichait tronque en "...quand me". */
    ui_menu_item(ctx, 16, "3 Entrer quand meme");
    ui_print(ctx, 20, 12, "ESC Retour au menu", VTX_WHITE);
    display_render_all(ctx);

    for (;;) {
        key = keyboard_scan();
        if (key == '1') {
            return 1;
        }
        if (key == KEY_LOCAL_ESCAPE) {
            return 2;
        }
        if (key == '2') {
            vtx_clear_page(ctx);
            *srv_idx = select_server(ctx);
            vtx_clear_page(ctx);
            return 1;
        }
        if (key == '3') {
            return 0;
        }
    }
}

/* Ecran de perte de porteuse. Retour : 1 = recomposer, 0 = rester en local,
 * 2 = ESC, revenir au menu principal.
 *
 * NB: cet ecran efface la page (vtx_clear_page) pour s'afficher ; le choix
 * "rester" ne la restaure donc PAS - la communication etant terminee, plus
 * rien ne la reenverra. Il rend simplement la main sans recomposer. */
static unsigned char carrier_lost_page(vtx_context_t* ctx)
{
    unsigned char key;

    vtx_clear_page(ctx);
    ui_print(ctx, 6, 11, "PERTE DE PORTEUSE", VTX_RED);
    ui_print(ctx, 8,  3, "Le modem a signale NO CARRIER:", VTX_WHITE);
    ui_print(ctx, 9,  3, "la communication est terminee.", VTX_WHITE);
    ui_menu_item(ctx, 12, "1 Reconnecter");
    ui_menu_item(ctx, 14, "2 Rester en local");
    ui_print(ctx, 18, 12, "ESC Retour au menu", VTX_WHITE);
    display_render_all(ctx);

    for (;;) {
        key = keyboard_scan();
        if (key == '1') return 1;
        if (key == '2') return 0;
        if (key == KEY_LOCAL_ESCAPE) return 2;
    }
}

/* --- Barre de statut (3 lignes texte sous la page) ---------------------
 * Ligne 0 : [C] serveur  mm:ss  MODE  ESC   (etat, redessine sur changement
 *           et une fois par seconde)
 * Ligne 1 : message transitoire (display_status)
 * Ligne 2 : aide des touches Minitel
 * L'indicateur de connexion vivait jusqu'ici en (0,38) de la page Videotex,
 * ligne qui appartient au serveur : il a rejoint la barre. */
static const char*  status_server = "";
static unsigned char status_connected;
static unsigned int  status_secs;       /* chrono de session (secondes) */
static unsigned char status_sec_ticks;  /* tics de 10 ms vers la seconde */

static const char* const render_mode_names[3] = { "AUTO", "TRAME", "BRUT" };

static void status_bar_draw(void)
{
    extern unsigned char g_render_mode;
    char clock[6];
    unsigned char m = (unsigned char)(status_secs / 60u);
    unsigned char sec = (unsigned char)(status_secs % 60u);

    if (m > 99) m = 99;
    clock[0] = '0' + m / 10;  clock[1] = '0' + m % 10;  clock[2] = ':';
    clock[3] = '0' + sec / 10; clock[4] = '0' + sec % 10; clock[5] = 0;

    display_status_clear(0);
    display_status_text(0, 1, status_connected ? " C " : " F ", 1);
    display_status_text(0, 5, status_server, 0);
    display_status_text(0, 22, " ", 0);         /* borne un nom trop long */
    display_status_text(0, 23, clock, 0);
    display_status_text(0, 30, render_mode_names[g_render_mode], 0);
    display_status_text(0, 36, "ESC", 0);
}

static void status_bar_init(void)
{
    status_server = "OricTel " ORICTEL_VERSION;
    status_connected = 0;
    status_secs = 0;
    status_sec_ticks = 0;
    status_bar_draw();
    display_status_clear(1);
    display_status_text(2, 1, "^A Annul ^R Retour ^S Somm ^N Suite", 0);
}

static void status_set_connected(unsigned char on)
{
    if (status_connected != on) {
        status_connected = on;
        status_bar_draw();
    }
}

/* Avance le chrono de ticks x 10 ms ; redessine a chaque seconde. */
static void status_tick(unsigned char ticks)
{
    status_sec_ticks += ticks;
    if (status_sec_ticks >= 100) {
        status_sec_ticks -= 100;
        ++status_secs;
        status_bar_draw();
    }
}

/* ESC en session : question posee sur la barre de statut, la page reste
 * intacte pour que "reprendre" ne coute rien (les autres ecrans effacent la
 * page, ce qui serait ici une perte : le serveur ne la renverra pas).
 *
 * Retour : 1 = quitter (raccrocher, retour au menu), 0 = reprendre.
 *
 * Pendant l'attente, le flux serie continue d'etre draine vers le decodeur
 * pour ne pas perdre d'octets (RX de 1 octet sur le 6551 reel) ; il n'est
 * pas rendu, la page se repeint au retour. */
static unsigned char session_escape_page(vtx_context_t* ctx)
{
    unsigned char key;

    display_status("ESC: quitter? ESC=menu autre=reprendre");

    keyboard_flush();       /* anti-rebond : l'ESC initial ne compte pas deux fois */
    for (;;) {
        while (serial_poll()) {
            vtx_process(ctx, serial_recv());
        }
        key = keyboard_scan();
        if (key == KEY_LOCAL_ESCAPE) return 1;
        if (key != KEY_NONE) break;
    }
    display_status_clear(1);
    ctx->full_refresh = 1;
    return 0;
}

int main(void)
{
    unsigned char byte;
    unsigned char key;
    unsigned char got_data;         /* 1 si donnees recues cette iteration */
    unsigned int  idle_counter;     /* Silence depuis le dernier octet (tics de 10 ms) */
    unsigned char ticks;            /* Tics de 10 ms ecoules cette iteration */
    unsigned char connected;        /* 0=deconnecte, 1=connecte */
    unsigned      acia_base;        /* Base ACIA choisie (LOCI $0380) */
    unsigned char srv_idx;          /* Serveur retenu (recompose sur perte de porteuse) */
    unsigned char carrier_pending;  /* "NO CARRIER" vu, en attente de confirmation */
    unsigned int  carrier_idle;     /* Silence accumule depuis cette presomption (tics de 10 ms) */
    unsigned char r;                /* retour des ecrans de choix */

    vtx_init(&vtx);
    display_init();
    keyboard_init();

    /* Ecran splash avec jingle */
    splash_screen(&vtx);

    /* Ecran interface serie (rappel): unique montage LOCI + PicoWiFiModemUSB
     * sur l'ACIA 6551 a la base LOCI ($0380). La base est reutilisee pour tout
     * reset ulterieur (KEY_LOCAL_RESET). */
    acia_base = select_interface(&vtx);

    /* ACIA montee avant les menus: la page Config WiFi (AT$SCAN...)
     * dialogue avec le PicoWiFiModemUSB des le menu. */
    serial_init(acia_base);

  /* Cycle complet : menus -> connexion -> session. ESC (confirme) quitte la
   * session, raccroche, et revient ICI, au menu Mode de connexion, avec un
   * decodeur remis a neuf : c'est la "reinitialisation" d'OricTel, sans
   * repasser par le splash ni recharger la cassette. */
  for (;;) {
    vtx_init(&vtx);
    status_bar_init();
    {
        unsigned char mode;

        /* La page Config WiFi (mode 3) revient au menu une fois terminee. */
        for (;;) {
            mode = select_mode(&vtx);
            if (mode == MODE_WIFI) {
                wifi_config_page(&vtx);
                continue;
            }
            if (mode == MODE_QUIT) {
                /* Fin de la chaine ESC : session -> menu -> BASIC. Le modem
                 * a deja ete raccroche en quittant la session. */
                oric_cold_reset();
            }
            break;
        }

        vtx_clear_page(&vtx);
        srv_idx = select_server(&vtx);
        vtx_clear_page(&vtx);
        status_server = (srv_idx == 255) ? custom_server : server_names[srv_idx];
        status_bar_draw();

        /* Mode modem AT uniquement (PicoWiFiModemUSB = modem Hayes) : a la
         * sortie de la boucle ci-dessus, mode vaut toujours MODE_MODEM (la
         * page Config WiFi reboucle). Le backend emule repond immediatement,
         * 100 ms suffisent pour la stabilisation. */
        (void)mode;
        delay_ms(100);

        /* Le retour de modem_connect n'est PLUS ignore. Il l'etait, et une
         * connexion echouee faisait quand meme entrer en session : OricTel
         * decodait alors un flux inexistant ou commence en cours de page,
         * d'ou la "premiere page illisible". On boucle tant que
         * l'utilisateur veut reessayer. */
        r = 1;
        while (!modem_connect(&vtx, srv_idx)) {
            r = connect_failed_page(&vtx, &srv_idx);
            if (r != 1) {
                break;      /* 0: session forcee, 2: ESC -> menu */
            }
        }
        vtx_clear_page(&vtx);
        if (r == 2) {
            at_hangup();    /* une tentative a pu laisser le modem en ligne */
            continue;
        }
    }

    /* Indicateur initial: F */
    connected = 0;
    idle_counter = 0;
    carrier_pending = 0;
    carrier_idle = 0;
    at_carrier_reset();
    via_tick_reset();
    status_secs = 0;
    status_sec_ticks = 0;
    status_set_connected(0);
    status_bar_draw();
    display_render_all(&vtx);

    /* Purger le burst clavier accumule pendant la connexion (timeouts AT
     * de plusieurs secondes sans lecture clavier): sinon ces frappes se
     * vident d'un coup et "deroulent" les ecrans en entrant en session. */
    keyboard_flush();

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
            /* Surveillance de la porteuse AVANT le decodeur : on observe le
             * flux brut. Un "NO CARRIER" n'est qu'une presomption (une page
             * Videotex pourrait contenir ces mots) ; tout octet de contenu
             * qui suit l'infirme, et seul le silence la confirme plus bas. */
            if (at_carrier_watch(byte)) {
                carrier_pending = 1;
                carrier_idle = 0;
            } else if (carrier_pending && byte != 0x0D && byte != 0x0A) {
                carrier_pending = 0;    /* la page continue: fausse alerte */
            }
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
            status_bar_draw();
        } else if (key == KEY_LOCAL_CLEAR) {
            vtx_clear_page(&vtx);
            vtx.full_refresh = 1;
        } else if (key == KEY_LOCAL_RESET) {
            serial_init(acia_base);
            display_status("ACIA reset");
        } else if (key == KEY_LOCAL_ESCAPE) {
            if (session_escape_page(&vtx)) {
                break;      /* quitter la session -> raccrocher, menu */
            }
        } else if (key != KEY_NONE) {
            keyboard_process(&vtx, key);
            serial_tx_pump();   /* faire partir la frappe sans attendre */
        }

        /* 3. Gestion indicateur connexion. La base de temps est lue a
         * CHAQUE iteration (les cycles s'accumulent dans le chronometre, ne
         * pas la lire pendant une rafale les ferait tous tomber dans la
         * premiere iteration silencieuse). */
        ticks = via_tick_10ms();
        status_tick(ticks);
        if (got_data) {
            idle_counter = 0;
            if (!connected) {
                connected = 1;
                status_set_connected(1);
            }
        } else {
            /* Confirmation de la perte de porteuse par le SILENCE : apres un
             * vrai NO CARRIER le modem est repasse en mode commande et plus
             * rien n'arrive. */
            if (carrier_pending &&
                (carrier_idle += ticks) >= CARRIER_CONFIRM_TICKS) {
                carrier_pending = 0;
                carrier_idle = 0;
                connected = 0;
                at_carrier_reset();
                r = carrier_lost_page(&vtx);
                if (r == 2) {
                    break;      /* ESC -> menu (la ligne est deja coupee) */
                }
                if (r == 1) {
                    vtx_clear_page(&vtx);
                    r = 1;
                    while (!modem_connect(&vtx, srv_idx)) {
                        r = connect_failed_page(&vtx, &srv_idx);
                        if (r != 1) break;
                    }
                    vtx_clear_page(&vtx);
                    keyboard_flush();
                    if (r == 2) {
                        at_hangup();
                        break;  /* ESC -> menu */
                    }
                }
                status_set_connected(0);
                vtx.full_refresh = 1;
                idle_counter = 0;
                continue;
            }
            if (idle_counter < 60000u) {
                idle_counter += ticks;
            }
            if (connected && idle_counter >= LINK_IDLE_TICKS) {
                connected = 0;
                status_set_connected(0);
            }
        }

        /* 4. Rendu adaptatif: 2 lignes par passe, puis continuer tant
         * que rien d'autre n'attend (ni octet serie, ni touche). Une
         * page se peint a pleine vitesse moteur quand la machine est
         * libre, et la main revient au clavier des qu'il le faut.
         *
         * Anti-overrun: la boucle CESSE de rendre des qu'un octet arrive
         * (serial_poll), pour repasser en 1. et drainer entierement le 6551
         * avant le prochain rendu. La fenetre pendant laquelle la puce (1
         * octet RX, pas de FIFO sur le materiel reel) peut deborder est
         * ainsi bornee a UNE passe de rendu (2 lignes). L'elimination
         * complete demanderait un controle de flux RTS ou une RX par IRQ
         * (le polling seul ne peut pas recuperer un octet deja ecrase dans
         * la puce) - voir ROADMAP. */
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

    /* Sortie de session par ESC : raccrocher proprement (le modem serait
     * sinon encore en ligne au prochain ATD, cause de la "premiere page
     * illisible" corrigee par at_hangup), puis retour au menu. */
    at_hangup();
    keyboard_flush();
  }

    return 0;
}
