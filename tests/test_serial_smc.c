/**
 * @file test_serial_smc.c
 * @brief Tests de coherence de la base ACIA / offsets registres.
 *
 * Le driver serie (serial_asm.s) patche au runtime les operandes absolues
 * de ses instructions a partir de la base ACIA LOCI ($0380) choisie au menu
 * (self-modifying code). Ce test host valide ce que le SMC ne peut pas
 * verifier seul : que la base supportee, augmentee des offsets registres du
 * 6551 (DATA=0, STATUS=1, COMMAND=2, CONTROL=3), retombe bien sur la plage
 * materielle attendue :
 *   - $0380-$0383 : ACIA 6551 LOCI (materiel reel ou PicoWiFiModemUSB)
 *
 * NB: l'execution reelle du self-modifying code se valide sur emulateur
 * (--acia-addr / --loci) ou sur le vrai materiel Oric+LOCI.
 */

#include "serial.h"
#include <stdio.h>

/* Offsets des 4 registres a partir de la base (ordre 6551) */
#define OFF_DATA    0
#define OFF_STATUS  1
#define OFF_COMMAND 2
#define OFF_CONTROL 3

static int failures = 0;
static int total = 0;

static void check(int cond, const char* msg)
{
    total++;
    if (cond) {
        printf("ok   : %s\n", msg);
    } else {
        printf("FAIL : %s\n", msg);
        failures++;
    }
}

int main(void)
{
    unsigned loci = ACIA_BASE_LOCI;

    printf("=== OricTel - Tests coherence base ACIA LOCI (SMC) ===\n\n");

    /* Base LOCI -> $0380..$0383 (firmware LOCI src/mia/oric/acia.h) */
    check(loci + OFF_DATA    == 0x0380, "LOCI DATA    = $0380");
    check(loci + OFF_STATUS  == 0x0381, "LOCI STATUS  = $0381");
    check(loci + OFF_COMMAND == 0x0382, "LOCI COMMAND = $0382");
    check(loci + OFF_CONTROL == 0x0383, "LOCI CONTROL = $0383");

    /* Les macros ACIA_* historiques == base LOCI + offsets */
    check(ACIA_DATA    == loci + OFF_DATA,    "ACIA_DATA    == LOCI+0");
    check(ACIA_STATUS  == loci + OFF_STATUS,  "ACIA_STATUS  == LOCI+1");
    check(ACIA_COMMAND == loci + OFF_COMMAND, "ACIA_COMMAND == LOCI+2");
    check(ACIA_CONTROL == loci + OFF_CONTROL, "ACIA_CONTROL == LOCI+3");

    /* La base ne deborde pas son octet de poids faible sur +3 (sinon un SMC
     * naif sans propagation de retenue casserait) */
    check(((loci & 0xFF) + OFF_CONTROL) <= 0xFF, "LOCI low byte +3 sans carry");

    /* L'octet de poids faible de la base LOCI sert de discriminant dans
     * serial_asm.s (cmp #$80) pour choisir la config Control/Command */
    check((loci & 0xFF) == 0x80, "LOCI low byte == $80 (discriminant SMC)");

    /* Bits Status 6551 utilises par le polling */
    check(ACIA_RDRF == 0x08, "RDRF 6551 = $08 (bit3)");
    check(ACIA_TDRE == 0x10, "TDRE 6551 = $10 (bit4)");

    /* Config Control LOCI = $1E : decodage 6551 */
    check(ACIA_CTRL_LOCI == 0x1E,              "Control LOCI = $1E");
    check((ACIA_CTRL_LOCI & 0x0F) == 0x0E,     "  baud selector = $E (9600)");
    check((ACIA_CTRL_LOCI & 0x10) != 0,        "  horloge interne (bit4=1)");
    check((ACIA_CTRL_LOCI & 0x60) == 0,        "  longueur mot = 8 bits");
    check((ACIA_CTRL_LOCI & 0x80) == 0,        "  1 bit de stop");

    /* Config Command LOCI = $0B : decodage 6551 */
    check(ACIA_CMD_LOCI == 0x0B,               "Command LOCI = $0B");
    check((ACIA_CMD_LOCI & 0x01) != 0,         "  DTR actif (bit0=1)");
    check((ACIA_CMD_LOCI & 0x02) != 0,         "  IRQ RX desactivee (bit1=1)");
    check((ACIA_CMD_LOCI & 0xE0) == 0,         "  sans parite (bits5-7=0)");

    printf("\n=== Resultats: %d/%d passes ===\n", total - failures, total);
    return failures ? 1 : 0;
}
