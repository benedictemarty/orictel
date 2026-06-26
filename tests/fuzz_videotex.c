/**
 * @file fuzz_videotex.c
 * @brief Harnais libFuzzer pour le decodeur Videotex (vtx_process).
 *
 * Le decodeur Videotex est LA surface d'attaque reseau d'OricTel : il
 * consomme des octets arbitraires venant du serveur Minitel distant. Ce
 * harnais injecte des entrees aleatoires dans la machine a etats, sous
 * AddressSanitizer + UndefinedBehaviorSanitizer, pour prouver l'absence
 * de debordement / d'UB sur n'importe quelle sequence d'octets.
 *
 * Build & run (voir cible `make fuzz`) :
 *   clang -DTEST_HOST -Isrc -fsanitize=fuzzer,address,undefined \
 *       tests/fuzz_videotex.c src/videotex.c src/fonts.c -o build/fuzz_videotex
 *   build/fuzz_videotex -max_total_time=30
 */

#include <stddef.h>
#include <stdint.h>
#include "videotex.h"

/* Stubs des dependances HW (memes que tests/test_videotex.c) : en fuzzing on
 * ne s'interesse qu'a la machine a etats, pas a l'E/S reelle. */
unsigned char g_global_mask = 1;
void serial_send(unsigned char byte) { (void)byte; }
void serial_tx_flush(void) {}
void serial_tx_pump(void) {}
unsigned char serial_recv(void) { return 0xFF; }
unsigned char serial_poll(void) { return 0; }
void serial_init(void) {}
void display_clear(void) {}
void display_beep(void) {}
void display_render(vtx_context_t* ctx) { (void)ctx; }

/* Contexte reutilise entre les iterations (re-initialise a chaque entree). */
static vtx_context_t ctx;

int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size)
{
    size_t i;
    vtx_init(&ctx);
    for (i = 0; i < size; ++i) {
        vtx_process(&ctx, data[i]);
    }
    return 0;
}
