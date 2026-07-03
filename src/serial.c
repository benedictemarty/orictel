/**
 * @file serial.c
 * @brief Interface serie OricTel -> driver ACIA 6551 (serial_asm.s)
 *
 * OricTel pilote une seule puce serie : le MOS 6551 a la base LOCI ($0380),
 * partagee par le materiel LOCI reel et le PicoWiFiModemUSB (emule ou reel).
 * serial_init() retient la base et chaque appel serial_*() est transmis tel
 * quel au driver assembleur acia6551_*. La couche reste fine : un simple
 * renvoi, sans surcout mesurable devant vtx_process() qui domine la boucle
 * de drainage RX.
 */

#include "serial.h"

/* Driver bas niveau (assembleur, serial_asm.s) */
void __fastcall__ acia6551_init(unsigned acia_base);
unsigned char __fastcall__ acia6551_send_raw(unsigned char byte);
unsigned char __fastcall__ acia6551_tx_ready(void);
unsigned char __fastcall__ acia6551_recv(void);
unsigned char __fastcall__ acia6551_poll(void);
unsigned char __fastcall__ acia6551_dcd(void);

void __fastcall__ serial_init(unsigned acia_base)
{
    acia6551_init(acia_base);
}

unsigned char __fastcall__ serial_poll(void)
{
    return acia6551_poll();
}

unsigned char __fastcall__ serial_recv(void)
{
    return acia6551_recv();
}

unsigned char __fastcall__ serial_tx_ready(void)
{
    return acia6551_tx_ready();
}

void __fastcall__ serial_send_raw(unsigned char byte)
{
    acia6551_send_raw(byte);
}

unsigned char __fastcall__ serial_dcd(void)
{
    return acia6551_dcd();
}
