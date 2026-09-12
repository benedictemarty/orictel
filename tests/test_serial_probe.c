/**
 * @file test_serial_probe.c
 * @brief Sonde de presence du 6551 (serial_probe) et /DSR (serial_modem_absent)
 *        sur un faux bus hote.
 *
 * Contexte : sur un Oric sans ACIA en $0380 (LOCI absent ou hors contexte
 * disque, Phosphoric --loci-emu sans --loci-cdc, Oric nu), $0380-$0383 est
 * le MIROIR DU VIA ($0300 decode sur toute la page $03xx). serial_init y
 * ecrivait $18/$0B dans DDRA/DDRB : clavier mort, OricTel "gele" sur son
 * menu. serial_probe doit le detecter SANS toucher aux DDR et en restaurant
 * ORA ; sur un vrai 6551 (LOCI, Phosphoric), elle doit repondre "present".
 *
 * Le bus est simule ici : serial.c est compile avec -DTEST_HOST, ses acces
 * REG_RD/REG_WR passent par test_bus_read/test_bus_write ci-dessous.
 */

#include "serial.h"
#include <stdio.h>
#include <string.h>

/* --- Stubs du driver assembleur (non exerces ici) ------------------------ */
void acia6551_init(unsigned acia_base) { (void)acia_base; }
unsigned char acia6551_send_raw(unsigned char b) { (void)b; return 0; }
unsigned char acia6551_tx_ready(void) { return 1; }
unsigned char acia6551_recv(void) { return 0xFF; }
unsigned char acia6551_poll(void) { return 0; }
unsigned char acia6551_dcd(void) { return 0; }

/* --- Faux bus ------------------------------------------------------------- */
enum { DEV_VIA, DEV_6551, DEV_6551_LOST };
static int dev;

/* Miroir VIA : ORB, ORA, DDRB, DDRA memorisants (DDRA=$FF : ORA relit son
 * latch). Les DDR sont surveilles : la sonde ne doit JAMAIS les modifier. */
static unsigned char via[4];
static int via_ddr_writes;

/* 6551 : STATUS en lecture seule, une ecriture = reset programme. Etat de
 * base du firmware LOCI sans modem : TDRE | /DSR | /DCD = $70. */
static unsigned char st_base = ACIA_TDRE | ACIA_NOT_DSR | ACIA_NOT_DCD;
static int resets;
static int lost_left;       /* lectures "perdues" (open-bus) restantes */

static int wr_log_n;
static unsigned char wr_log[8];

unsigned char test_bus_read(unsigned addr)
{
    unsigned r = addr & 3;
    if (dev == DEV_VIA) return via[r];
    if (dev == DEV_6551_LOST && lost_left > 0 && r == 1) {
        --lost_left;
        return 0x55;        /* open-bus : la valeur qu'on vient d'ecrire */
    }
    if (r == 1) return st_base;
    return 0;
}

void test_bus_write(unsigned addr, unsigned char v)
{
    unsigned r = addr & 3;
    if (wr_log_n < 8) wr_log[wr_log_n++] = (unsigned char)r;
    if (dev == DEV_VIA) {
        if (r >= 2) ++via_ddr_writes;
        via[r] = v;
        return;
    }
    if (r == 1) ++resets;   /* reset programme, valeur non retenue */
}

static void reset_bus(int d)
{
    dev = d;
    via[0] = 0xF7; via[1] = 0x0E; via[2] = 0xF7; via[3] = 0xFF;
    via_ddr_writes = 0; resets = 0; lost_left = 0; wr_log_n = 0;
}

/* --- Micro-framework ------------------------------------------------------ */
static int failures = 0, total = 0;
static void check(int cond, const char* msg)
{
    total++;
    printf("%s : %s\n", cond ? "ok  " : "FAIL", msg);
    if (!cond) failures++;
}

int main(void)
{
    printf("=== OricTel - Sonde de presence du 6551 ($0380) ===\n\n");

    /* 1. Miroir VIA : pas de 6551, DDR intacts, ORA restaure. */
    reset_bus(DEV_VIA);
    check(serial_probe(ACIA_BASE_LOCI) == 0, "miroir VIA -> 'aucun 6551'");
    check(via_ddr_writes == 0,              "miroir VIA : DDRA/DDRB jamais ecrits");
    check(via[1] == 0x0E,                   "miroir VIA : ORA restaure a sa valeur");
    check(via[0] == 0xF7,                   "miroir VIA : ORB (rangee clavier) intact");

    /* 2. 6551 LOCI sans modem (status $70) : present, via resets programmes. */
    reset_bus(DEV_6551);
    check(serial_probe(ACIA_BASE_LOCI) == 1, "6551 (status $70) -> 'present'");
    check(resets == 3,                      "6551 : 3 ecritures STATUS = resets programmes");
    {
        int only_status = 1, i;
        for (i = 0; i < wr_log_n; i++) if (wr_log[i] != 1) only_status = 0;
        check(only_status, "6551 : la sonde n'ecrit que STATUS (+1)");
    }

    /* 3. 6551 Phosphoric (DSR/DCD actifs, status $10). */
    reset_bus(DEV_6551); st_base = ACIA_TDRE;
    check(serial_probe(ACIA_BASE_LOCI) == 1, "6551 (status $10) -> 'present'");

    /* 4. Course PHI2 perdue sur LOCI : la 1re relecture renvoie l'open-bus
     *    ($55) ; la 2e ($AA) ne peut pas suivre -> toujours 'present'. */
    reset_bus(DEV_6551_LOST); lost_left = 1;
    check(serial_probe(ACIA_BASE_LOCI) == 1, "lecture perdue (open-bus $55) -> 'present' quand meme");

    /* 5. /DSR : firmware LOCI sans Pico = absent ; monte = present. */
    reset_bus(DEV_6551); st_base = ACIA_TDRE | ACIA_NOT_DSR | ACIA_NOT_DCD;
    serial_init(ACIA_BASE_LOCI);
    check(serial_modem_absent() != 0,       "/DSR haut -> modem absent");
    st_base = ACIA_TDRE;
    check(serial_modem_absent() == 0,       "/DSR bas -> modem present");

    printf("\n=== Resultats: %d/%d passes ===\n", total - failures, total);
    return failures ? 1 : 0;
}
