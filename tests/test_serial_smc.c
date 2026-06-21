/**
 * @file test_serial_smc.c
 * @brief Tests de coherence des bases ACIA / offsets registres.
 *
 * Le driver serie (serial_asm.s) patche au runtime les operandes absolues
 * de ses instructions a partir d'une base ACIA choisie au menu
 * (self-modifying code). Ce test host valide ce que le SMC ne peut pas
 * verifier seul : que les deux bases supportees, augmentees des offsets
 * registres du 6551 (DATA=0, STATUS=1, COMMAND=2, CONTROL=3), retombent
 * bien sur les plages materielles attendues :
 *   - $031C-$031F : ACIA emulee Phosphoric/Euphoric
 *   - $0380-$0383 : ACIA emulee par la cartouche LOCI (firmware sodiumlb)
 *
 * NB: l'execution reelle du self-modifying code se valide sur emulateur
 * (--acia-addr) ou sur le vrai materiel Oric+LOCI.
 */

#include "serial.h"
#include <stdio.h>

/* Offsets des 4 registres a partir de la base (ordre 6551) */
#define OFF_DATA    0
#define OFF_STATUS  1
#define OFF_COMMAND 2
#define OFF_CONTROL 3

static int failures = 0;

static void check(int cond, const char* msg)
{
    if (cond) {
        printf("ok   : %s\n", msg);
    } else {
        printf("FAIL : %s\n", msg);
        failures++;
    }
}

int main(void)
{
    unsigned emu  = ACIA_BASE_EMU;
    unsigned loci = ACIA_BASE_LOCI;

    printf("=== OricTel - Tests coherence bases ACIA (SMC) ===\n\n");

    /* Base emulateur -> $031C..$031F */
    check(emu + OFF_DATA    == 0x031C, "EMU  DATA    = $031C");
    check(emu + OFF_STATUS  == 0x031D, "EMU  STATUS  = $031D");
    check(emu + OFF_COMMAND == 0x031E, "EMU  COMMAND = $031E");
    check(emu + OFF_CONTROL == 0x031F, "EMU  CONTROL = $031F");

    /* Base LOCI -> $0380..$0383 (firmware LOCI src/mia/oric/acia.h) */
    check(loci + OFF_DATA    == 0x0380, "LOCI DATA    = $0380");
    check(loci + OFF_STATUS  == 0x0381, "LOCI STATUS  = $0381");
    check(loci + OFF_COMMAND == 0x0382, "LOCI COMMAND = $0382");
    check(loci + OFF_CONTROL == 0x0383, "LOCI CONTROL = $0383");

    /* Les macros ACIA_* historiques == base emulateur + offsets */
    check(ACIA_DATA    == emu + OFF_DATA,    "ACIA_DATA    == EMU+0");
    check(ACIA_STATUS  == emu + OFF_STATUS,  "ACIA_STATUS  == EMU+1");
    check(ACIA_COMMAND == emu + OFF_COMMAND, "ACIA_COMMAND == EMU+2");
    check(ACIA_CONTROL == emu + OFF_CONTROL, "ACIA_CONTROL == EMU+3");

    /* Aucune des deux bases ne deborde son octet de poids faible sur +3
     * (sinon un SMC naif sans propagation de retenue casserait) */
    check(((emu  & 0xFF) + OFF_CONTROL) <= 0xFF, "EMU  low byte +3 sans carry");
    check(((loci & 0xFF) + OFF_CONTROL) <= 0xFF, "LOCI low byte +3 sans carry");

    printf("\n=== Resultats: %d/%d passes ===\n",
           14 - failures, 14);
    return failures ? 1 : 0;
}
