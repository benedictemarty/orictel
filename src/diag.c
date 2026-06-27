/**
 * @file diag.c
 * @brief Programme de diagnostic ACIA 6551 / LOCI + PicoWiFiModemUSB
 *
 * Outil autonome (orictel.tap separe) pour identifier la configuration ACIA
 * qui fait reellement dialoguer OricTel avec le LOCI (l'emulation 6551 du
 * firmware LOCI). Acces DIRECT aux registres $0380-$0383 (pas le driver
 * patche) : on peut donc essayer n'importe quelle valeur Control/Command a
 * chaud et OBSERVER le registre status en temps reel.
 *
 * Touches :
 *   1..6  : choisir une configuration (re-applique Control/Command)
 *   ESPACE: envoyer "ATZ"  + CR
 *   S     : envoyer "AT$SCAN" + CR
 *   I     : envoyer "ATI" + CR
 *   R     : re-appliquer la config courante
 *   C     : effacer la zone de reception
 *
 * Affichage continu : valeur hex du registre STATUS + decodage bit a bit
 * (PE FE OV RDRF TDRE DCD DSR IRQ) et le flot d'octets recus.
 */

#include "videotex.h"
#include "display.h"
#include "keyboard.h"
#include "ui.h"

/* --- Acces direct aux 4 registres ACIA du LOCI ($0380-$0383) -------------- */
#define R_DATA (*(volatile unsigned char*)0x0380)  /* R: RX  W: TX            */
#define R_STAT (*(volatile unsigned char*)0x0381)  /* R: status W: prog reset */
#define R_CMD  (*(volatile unsigned char*)0x0382)  /* R/W: command            */
#define R_CTRL (*(volatile unsigned char*)0x0383)  /* R/W: control            */

static vtx_context_t vtx;

/* Globales referencees par videotex.c/display.c (normalement dans main.c). */
unsigned char g_blink_phase;
unsigned char g_global_mask = 1;

/* --- Configurations a essayer (Control, Command) -------------------------- */
/* Rappel firmware LOCI: baud = (ctrl & 0x0F) -> 0=115200 2=75 E=9600 F=19200 ;
 * bit horloge (0x10) IGNORE ; data = 8-((ctrl>>5)&3) ; parite/DTR via cmd.   */
#define NCFG 6
static const unsigned char cfg_ctrl[NCFG] = { 0x1E, 0x0E, 0x1E, 0x00, 0x22, 0x1E };
static const unsigned char cfg_cmd [NCFG] = { 0x0B, 0x0B, 0x09, 0x03, 0x65, 0x0B };
/* cfg 6 (index 5) = identique a 1 mais SANS programmed reset (no_reset[i]).  */
static const unsigned char cfg_noreset[NCFG] = { 0,0,0,0,0,1 };
static const char* const cfg_lbl[NCFG] = {
    "1 CTRL=1E CMD=0B 9600 8N1 DTR+RTS",
    "2 CTRL=0E CMD=0B 9600 8N1 hEXT",
    "3 CTRL=1E CMD=09 9600 8N1 +rxIRQ",
    "4 CTRL=00 CMD=03 115200 8N1 DTR",
    "5 CTRL=22 CMD=65 75bd 7E1 ORICOMMS",
    "6 CTRL=1E CMD=0B sans prog-reset"
};

static const char HEX[] = "0123456789ABCDEF";

/* Ecrit la valeur hex 2 chiffres de v dans buf[0],buf[1]. */
static void hex2(char* buf, unsigned char v)
{
    buf[0] = HEX[(v >> 4) & 0x0F];
    buf[1] = HEX[v & 0x0F];
}

/* --- Emission directe (attente TDRE bornee) ------------------------------- */
static void acia_putc(unsigned char c)
{
    unsigned int n = 0;
    while (!(R_STAT & 0x10)) {        /* attendre TDRE, borne anti-blocage */
        if (++n == 0) return;        /* TDRE jamais pret : on abandonne */
    }
    R_DATA = c;
}

static void acia_puts(const char* s)
{
    while (*s) { acia_putc((unsigned char)*s); ++s; }
    acia_putc(0x0D);                 /* CR */
}

/* --- Application d'une configuration -------------------------------------- */
static void apply_cfg(unsigned char sel)
{
    if (!cfg_noreset[sel]) {
        R_STAT = 0x00;               /* programmed reset (ecriture status) */
    }
    R_CTRL = cfg_ctrl[sel];          /* Control */
    R_CMD  = cfg_cmd[sel];           /* Command */
    (void)R_STAT;                    /* lire status (efface IRQ pending) */
    (void)R_DATA;                    /* clear RDR */
}

/* --- Zone de reception (fenetre texte) ------------------------------------ */
#define RX_ROW0 14                   /* 1re ligne d'affichage RX */
#define RX_ROWS 9                    /* nb de lignes RX */
#define RX_COL0 1
#define RX_COLN 38                   /* derniere colonne utile */
static unsigned char rx_row;         /* ligne courante (0..RX_ROWS-1) */
static unsigned char rx_col;
static unsigned int  rx_total;       /* compteur total d'octets recus */

static void rx_clear(void)
{
    unsigned char r;
    for (r = 0; r < RX_ROWS; ++r) {
        ui_print(&vtx, RX_ROW0 + r,
                 RX_COL0, "                                      ", VTX_GREEN);
    }
    rx_row = 0;
    rx_col = RX_COL0;
    rx_total = 0;
}

static void rx_newline(void)
{
    rx_col = RX_COL0;
    ++rx_row;
    if (rx_row >= RX_ROWS) {
        rx_row = 0;                  /* repart en haut (anneau simple) */
        rx_clear();
    }
}

static void rx_putc(unsigned char b)
{
    ++rx_total;
    if (b == 0x0D) return;           /* CR : ignore (LF gere le saut) */
    if (b == 0x0A) { rx_newline(); return; }
    if (rx_col > RX_COLN) rx_newline();
    /* imprimable -> caractere vert ; sinon code hex rouge sur 2 cases */
    if (b >= 0x20 && b < 0x7F) {
        vtx.screen[RX_ROW0 + rx_row][rx_col].ch = b;
        vtx.screen[RX_ROW0 + rx_row][rx_col].fg = VTX_GREEN;
        ++rx_col;
    } else {
        if (rx_col > RX_COLN - 1) rx_newline();
        vtx.screen[RX_ROW0 + rx_row][rx_col].ch = HEX[(b >> 4) & 0x0F];
        vtx.screen[RX_ROW0 + rx_row][rx_col].fg = VTX_RED;
        vtx.screen[RX_ROW0 + rx_row][rx_col + 1].ch = HEX[b & 0x0F];
        vtx.screen[RX_ROW0 + rx_row][rx_col + 1].fg = VTX_RED;
        rx_col += 2;
    }
    vtx.dirty[RX_ROW0 + rx_row] = 1;
}

/* Draine les octets disponibles (borne par appel). */
static void rx_drain(void)
{
    unsigned char n = 0;
    while ((R_STAT & 0x08) && n < 64) {   /* RDRF set */
        rx_putc(R_DATA);                  /* lire DATA -> efface RDRF */
        ++n;
    }
}

/* --- Affichage de l'etat (config + status decode) ------------------------- */
static void draw_status(unsigned char sel)
{
    static const char* const bitname[8] =
        { "PE","FE","OV","RD","TE","DC","DS","IR" };
    char line[40];
    unsigned char st = R_STAT;
    unsigned char i, col;

    ui_print(&vtx, 0, 1, "DIAG ACIA 6551 / LOCI  (1-6 S I R C)", VTX_YELLOW);
    ui_print(&vtx, 2, 1, "Config:", VTX_WHITE);
    ui_print(&vtx, 2, 9, cfg_lbl[sel], VTX_CYAN);

    /* STATUS=xx */
    line[0]='S';line[1]='T';line[2]='A';line[3]='T';line[4]='=';
    hex2(&line[5], st);
    line[7]=0;
    ui_print(&vtx, 4, 1, line, VTX_WHITE);

    /* decode bits : nom + 0/1, bit set en cyan */
    for (i = 0; i < 8; ++i) {
        col = 10 + i * 3;
        /* bit 7..0 : afficher de gauche (IR) a droite (PE) ? on garde 0..7 */
        vtx.screen[4][col].ch   = bitname[i][0];
        vtx.screen[4][col].fg   = (st & (1 << i)) ? VTX_CYAN : VTX_WHITE;
        vtx.screen[4][col+1].ch = bitname[i][1];
        vtx.screen[4][col+1].fg = (st & (1 << i)) ? VTX_CYAN : VTX_WHITE;
    }
    vtx.dirty[4] = 1;

    /* Indicateurs lisibles */
    ui_print(&vtx, 6, 1, "TDRE(emet):", VTX_WHITE);
    ui_print(&vtx, 6, 13, (st & 0x10) ? "OUI" : "non",
             (st & 0x10) ? VTX_GREEN : VTX_RED);
    ui_print(&vtx, 6, 18, "RDRF(recu):", VTX_WHITE);
    ui_print(&vtx, 6, 30, (st & 0x08) ? "OUI" : "non",
             (st & 0x08) ? VTX_GREEN : VTX_RED);
    /* DCD : bit 0x20 = NOT_DCD ; 0 => porteuse/gate ouvert */
    ui_print(&vtx, 7, 1, "DCD(gate) :", VTX_WHITE);
    ui_print(&vtx, 7, 13, (st & 0x20) ? "NON" : "oui",
             (st & 0x20) ? VTX_RED : VTX_GREEN);

    /* total octets recus */
    line[0]='R';line[1]='X';line[2]='=';
    hex2(&line[3], (unsigned char)(rx_total >> 8));
    hex2(&line[5], (unsigned char)(rx_total & 0xFF));
    line[7]=0;
    ui_print(&vtx, 7, 18, line, VTX_YELLOW);

    ui_print(&vtx, 9, 1, "ESPACE=ATZ  S=SCAN  I=ATI  R=reinit", VTX_WHITE);
    ui_print(&vtx, 12, 1, "--- Reception ---", VTX_YELLOW);
}

int main(void)
{
    unsigned char sel = 0;
    unsigned char key;
    unsigned int  tick;

    vtx_init(&vtx);
    display_init();
    keyboard_init();

    vtx_clear_page(&vtx);
    rx_clear();
    apply_cfg(sel);
    draw_status(sel);
    display_render_all(&vtx);

    for (;;) {
        rx_drain();
        draw_status(sel);
        display_render_all(&vtx);

        key = keyboard_scan();
        if (key >= '1' && key <= '0' + NCFG) {
            sel = key - '1';
            vtx_clear_page(&vtx);
            rx_clear();
            apply_cfg(sel);
            draw_status(sel);
            display_render_all(&vtx);
        } else if (key == ' ' || key == 'Z' || key == 'z') {
            acia_puts("ATZ");
        } else if (key == 'S' || key == 's') {
            acia_puts("AT$SCAN");
        } else if (key == 'I' || key == 'i') {
            acia_puts("ATI");
        } else if (key == 'R' || key == 'r') {
            apply_cfg(sel);
        } else if (key == 'C' || key == 'c') {
            rx_clear();
        }

        /* petite tempo pour ne pas saturer le rendu, mais rester reactif */
        for (tick = 0; tick < 400; ++tick) { __asm__("nop"); }
    }
    /* unreachable */
}
