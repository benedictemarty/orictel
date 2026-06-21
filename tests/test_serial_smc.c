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

    /* L'octet de poids faible de la base LOCI sert de discriminant dans
     * serial_asm.s (cmp #$80) pour choisir la config Control/Command */
    check((loci & 0xFF) == 0x80, "LOCI low byte == $80 (discriminant SMC)");
    check((emu  & 0xFF) != 0x80, "EMU  low byte != $80");

    /* Config Control/Command emulateur (instant transfer Phosphoric) */
    check(ACIA_CTRL_EMU == 0x00, "Control EMU = $00 (horloge externe)");
    check(ACIA_CMD_EMU  == 0x03, "Command EMU = $03 (DTR, sans IRQ)");

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

    /* --- Digitelec DTL 2000 : ACIA 6850 (puce distincte) --- */
    check(ACIA_BASE_DTL == 0x03F8,             "Base DTL 2000 = $03F8");
    check(ACIA_BASE_DTL != ACIA_BASE_EMU &&
          ACIA_BASE_DTL != ACIA_BASE_LOCI,     "DTL distincte des bases 6551");

    /* Bits Status 6850 != 6551 (mapping different) */
    check(DTL_SR_RDRF == 0x01 && ACIA_RDRF == 0x08, "RDRF 6850=$01 vs 6551=$08");
    check(DTL_SR_TDRE == 0x02 && ACIA_TDRE == 0x10, "TDRE 6850=$02 vs 6551=$10");

    /* Control 6850 V23 = $09 : decodage Motorola */
    check(DTL_ACIA_V23_EMIT == 0x09,           "Control DTL V23 = $09");
    check((DTL_ACIA_V23_EMIT & 0x03) == 0x01,  "  diviseur = div16 (CDS=01)");
    check(((DTL_ACIA_V23_EMIT >> 2) & 0x07) == 0x02, "  format = 7E1 (WS=010)");
    check(((DTL_ACIA_V23_EMIT >> 5) & 0x03) == 0x00, "  RTS bas = emission (TC=00)");
    check(DTL_ACIA_RESET == 0x03,              "Master reset 6850 = $03");

    /* PIA : ORA de connexion $D0 (asym V23, ligne fermee) */
    check((DTL_PIA_ORA_CONN & 0x04) == 0,      "ORA bit2=0 -> ligne connectee");
    check((DTL_PIA_ORA_CONN & 0x10) != 0,      "ORA bit4=1 -> asym V23 (75/1200)");

    printf("\n=== Resultats: %d/%d passes ===\n", total - failures, total);
    return failures ? 1 : 0;
}
