/**
 * @file serial.c
 * @brief Aiguillage entre les drivers ACIA (6551 et 6850/DTL 2000)
 *
 * OricTel sait piloter deux puces serie incompatibles :
 *   - MOS 6551 ($031C emulateur / $0380 LOCI), driver serial_asm.s
 *   - Motorola 6850 du Digitelec DTL 2000 ($03FC), driver serial6850_asm.s
 *
 * L'interface est choisie une seule fois au menu de demarrage. serial_init()
 * memorise la puce et chaque appel serial_*() est aiguille vers le bon
 * driver. Le surcout (un test + saut) est negligeable devant vtx_process()
 * qui domine la boucle de drainage RX.
 */

#include "serial.h"

/* Drivers bas niveau (assembleur) */
void __fastcall__ acia6551_init(unsigned acia_base);
unsigned char __fastcall__ acia6551_send_raw(unsigned char byte);
unsigned char __fastcall__ acia6551_tx_ready(void);
unsigned char __fastcall__ acia6551_recv(void);
unsigned char __fastcall__ acia6551_poll(void);
unsigned char __fastcall__ acia6551_dcd(void);

void __fastcall__ acia6850_init(void);
unsigned char __fastcall__ acia6850_send_raw(unsigned char byte);
unsigned char __fastcall__ acia6850_tx_ready(void);
unsigned char __fastcall__ acia6850_recv(void);
unsigned char __fastcall__ acia6850_poll(void);
unsigned char __fastcall__ acia6850_dcd(void);

/* 0 = MOS 6551, 1 = Motorola 6850 (DTL 2000) */
static unsigned char chip_is_6850;

void __fastcall__ serial_init(unsigned acia_base)
{
    chip_is_6850 = (acia_base == ACIA_BASE_DTL);
    if (chip_is_6850) {
        acia6850_init();
    } else {
        acia6551_init(acia_base);
    }
}

unsigned char __fastcall__ serial_poll(void)
{
    return chip_is_6850 ? acia6850_poll() : acia6551_poll();
}

unsigned char __fastcall__ serial_recv(void)
{
    return chip_is_6850 ? acia6850_recv() : acia6551_recv();
}

unsigned char __fastcall__ serial_tx_ready(void)
{
    return chip_is_6850 ? acia6850_tx_ready() : acia6551_tx_ready();
}

void __fastcall__ serial_send_raw(unsigned char byte)
{
    if (chip_is_6850) {
        acia6850_send_raw(byte);
    } else {
        acia6551_send_raw(byte);
    }
}

unsigned char __fastcall__ serial_dcd(void)
{
    return chip_is_6850 ? acia6850_dcd() : acia6551_dcd();
}
