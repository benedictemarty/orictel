; ===========================================================================
; serial_asm.s - Driver ACIA 6551 pour OricTel (polling simple)
;
; Mode polling: pas d'IRQ, le programme principal lit les octets.
; Le serveur/bridge pace les donnees pour eviter l'overrun.
;
; Fonctions exportees:
;   _serial_init  - Initialise ACIA
;   _serial_send  - Envoie un octet (delai fixe)
;   _serial_recv  - Recoit un octet (non-bloquant)
;   _serial_poll  - Verifie donnee dispo
; ===========================================================================

        .export _serial_init
        .export _serial_send
        .export _serial_recv
        .export _serial_poll

        .segment "CODE"

; --- Adresses ACIA 6551 ---
ACIA_DATA    = $031C
ACIA_STATUS  = $031D
ACIA_COMMAND = $031E
ACIA_CONTROL = $031F

; --- Bits Status ---
RDRF         = $08          ; Bit 3: donnee recue disponible
TDRE         = $10          ; Bit 4: transmetteur pret

; ===========================================================================
; serial_init - Initialise l'ACIA (polling, pas d'IRQ)
;
; Control: 8 bits, 1 stop, 1200 baud, horloge interne = $18
; Command: DTR on, RX IRQ off, RTS low, TX IRQ off, no parity = $0B
; ===========================================================================
_serial_init:
        ; Reset programme
        sta     ACIA_STATUS

        ; Control: 8 bits, 1 stop, 300 baud, horloge interne
        ; 300 baud = 33333 cycles/octet. Le serveur pace les donnees.
        lda     #$16
        sta     ACIA_CONTROL

        ; Command: DTR on, IRQ off, RTS low, no parity
        lda     #$0B
        sta     ACIA_COMMAND

        ; Vider le buffer de reception
        lda     ACIA_DATA
        rts

; ===========================================================================
; serial_send - Envoie un octet puis attend la fin de transmission
; Entree: A = octet
; Ne lit PAS ACIA_STATUS (pour ne pas interférer avec la reception)
; ===========================================================================
_serial_send:
        sta     ACIA_DATA

        ; Delai ~8500 cycles pour 1200 baud (10 bits/octet)
        ldy     #7
@outer: ldx     #0
@inner: dex
        bne     @inner
        dey
        bne     @outer
        rts

; ===========================================================================
; serial_recv - Recoit un octet (non-bloquant)
; Sortie: A = octet recu, ou $FF si aucune donnee
; ===========================================================================
_serial_recv:
        lda     ACIA_STATUS
        and     #RDRF
        beq     @no_data
        lda     ACIA_DATA
        rts
@no_data:
        lda     #$FF
        rts

; ===========================================================================
; serial_poll - Verifie si une donnee est disponible
; Sortie: A = non-zero si donnee dispo
; ===========================================================================
_serial_poll:
        lda     ACIA_STATUS
        and     #RDRF
        rts
