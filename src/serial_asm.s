; ===========================================================================
; serial_asm.s - Driver ACIA 6551 polling pur
;
; Avec le FIFO 4096 de l'emulateur (--serial-buffer 4096):
; - Le FIFO absorbe le burst TCP
; - L'ACIA delivre a 1200 baud depuis le FIFO
; - RDRF reste a 1 tant que le FIFO n'est pas vide
; - Pas besoin d'IRQ ni de ring buffer logiciel
; - Le main loop lit directement via poll/recv
;
; Simple, fiable, pas de buffer overflow possible.
; ===========================================================================

        .export _serial_init
        .export _serial_send
        .export _serial_recv
        .export _serial_poll
        .export _serial_dcd

        .segment "CODE"

ACIA_DATA    = $031C
ACIA_STATUS  = $031D
ACIA_COMMAND = $031E
ACIA_CONTROL = $031F

RDRF         = $08
TDRE         = $10
DCD_BIT      = $20

; ===========================================================================
; serial_init - Polling, pas d'IRQ
;
; Control: 8 bits, 1 stop, 1200 baud, horloge interne = $18
; Command: DTR on, RX IRQ OFF, RTS low, no parity = $0B
; ===========================================================================
_serial_init:
        lda     #$00
        sta     ACIA_STATUS     ; Programmed reset (efface OVRN, TDRE=1)

        lda     #$1F            ; 19200 baud, 8N1, clock interne
        sta     ACIA_CONTROL

        lda     #$03            ; DTR on, IRQ RX off
        sta     ACIA_COMMAND

        lda     ACIA_STATUS     ; Lire status pour effacer IRQ pending
        lda     ACIA_DATA       ; Clear RDR
        rts

; ===========================================================================
; serial_send - Polling TDRE
; ===========================================================================
_serial_send:
        pha
@wait:  lda     ACIA_STATUS
        and     #TDRE
        beq     @wait
        pla
        sta     ACIA_DATA
        rts

; ===========================================================================
; serial_recv - Lecture directe ACIA (FIFO transparent)
; Avec --serial-buffer, RDRF=1 tant que FIFO non vide.
; Chaque lecture de DATA pop le prochain octet du FIFO.
; ===========================================================================
_serial_recv:
        lda     ACIA_STATUS
        and     #RDRF
        beq     @empty
        lda     ACIA_DATA
        rts
@empty:
        lda     #$FF
        rts

; ===========================================================================
; serial_poll - RDRF set = donnees dans le FIFO
; ===========================================================================
_serial_poll:
        lda     ACIA_STATUS
        and     #RDRF
        rts

; ===========================================================================
; serial_dcd - Etat DCD
; ===========================================================================
_serial_dcd:
        lda     ACIA_STATUS
        and     #DCD_BIT
        rts
