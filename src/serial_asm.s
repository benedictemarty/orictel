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
        .export _serial_send_raw
        .export _serial_tx_ready
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
; Control: $00 = horloge externe, 8 bits, 1 stop. Phosphoric traite
;   l'horloge externe en "instant transfer": aucun cadencement baud,
;   les octets sont disponibles des leur arrivee TCP. C'est le mode
;   le plus rapide sous emulateur ($1F = 19200 cadence la livraison
;   a ~1.9 Ko/s, perceptible au chargement d'une page).
;   Le V23 reel 1200/75 7E1 demanderait Control=$18 + parite Command.
; Command: $03 = DTR on, RX IRQ off, no parity
; ===========================================================================
_serial_init:
        lda     #$00
        sta     ACIA_STATUS     ; Programmed reset (efface OVRN, TDRE=1)

        lda     #$00            ; Horloge externe = instant transfer
        sta     ACIA_CONTROL

        lda     #$03            ; DTR on, IRQ RX off
        sta     ACIA_COMMAND

        lda     ACIA_STATUS     ; Lire status pour effacer IRQ pending
        lda     ACIA_DATA       ; Clear RDR
        rts

; ===========================================================================
; serial_send_raw - Ecriture directe ACIA, bloque sur TDRE
; Ne pas appeler directement depuis le code applicatif: passer par
; serial_send() (file TX logicielle, serial_tx.c) pour ne pas bloquer
; la reception pendant l'attente TDRE.
; ===========================================================================
_serial_send_raw:
        pha
@wait:  lda     ACIA_STATUS
        and     #TDRE
        beq     @wait
        pla
        sta     ACIA_DATA
        rts

; ===========================================================================
; serial_tx_ready - TDRE set = transmetteur pret (non bloquant)
; ===========================================================================
_serial_tx_ready:
        lda     ACIA_STATUS
        and     #TDRE
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
