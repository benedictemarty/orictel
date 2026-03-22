; ===========================================================================
; serial_asm.s - Driver ACIA 6551 avec IRQ + buffer FIFO emulateur
;
; Utilise les nouvelles options de Phosphoric v1.15.2:
;   --serial-buffer 256    : FIFO 256 octets dans l'emulateur (plus d'overrun)
;   --serial-irq-on-rdrf   : IRQ re-triggee tant que RDRF est set
;
; Reception par IRQ avec buffer circulaire logiciel 256 octets.
; Le buffer FIFO de l'emulateur elimine l'overrun hardware.
; L'IRQ-on-RDRF garantit que l'ISR est toujours appelee.
;
; Fonctions exportees:
;   _serial_init  - Initialise ACIA + IRQ
;   _serial_send  - Envoie un octet (polling TDRE)
;   _serial_recv  - Lit depuis le buffer logiciel
;   _serial_poll  - Verifie le buffer logiciel
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

; --- Buffer circulaire en page zero ---
rx_head      = $E0          ; Pointeur ecriture (ISR)
rx_tail      = $E1          ; Pointeur lecture (programme)

        .segment "BSS"
rx_buffer:   .res 256       ; Buffer circulaire 256 octets

        .segment "CODE"

; ===========================================================================
; serial_init - Initialise l'ACIA avec reception par IRQ
;
; Control: 8 bits, 1 stop, 19200 baud, horloge interne = $1F
;   Vitesse max car le buffer FIFO de l'emulateur absorbe les pics.
;   Le V23 (--serial-v23) override le baud si necessaire.
;
; Command: DTR on, RX IRQ ACTIVE, RTS low, TX IRQ off, no parity = $09
; ===========================================================================
_serial_init:
        ; Buffer circulaire a zero
        lda     #0
        sta     rx_head
        sta     rx_tail

        ; Reset programme ACIA
        sta     ACIA_STATUS

        ; Control: 8 bits, 1 stop, 19200 baud, horloge interne
        lda     #$1F
        sta     ACIA_CONTROL

        ; Command: DTR on, RX IRQ active, RTS low, TX IRQ off
        lda     #$09
        sta     ACIA_COMMAND

        ; Vider le RDR
        lda     ACIA_DATA

        ; Activer les interruptions CPU
        cli
        rts

; ===========================================================================
; serial_send - Envoie un octet (polling TDRE)
; Entree: A = octet
;
; Avec --serial-irq-on-rdrf, la lecture de STATUS ne perd plus les IRQ RX.
; L'IRQ est re-declenchee tant que RDRF est set dans le FIFO.
; ===========================================================================
_serial_send:
        pha
@wait_tdre:
        lda     ACIA_STATUS
        and     #TDRE
        beq     @wait_tdre
        pla
        sta     ACIA_DATA
        rts

; ===========================================================================
; serial_recv - Lit un octet du buffer circulaire logiciel
; Sortie: A = octet, ou $FF si buffer vide
; ===========================================================================
_serial_recv:
        lda     rx_head
        cmp     rx_tail
        beq     @empty
        ldx     rx_tail
        lda     rx_buffer,x
        inx
        stx     rx_tail
        rts
@empty:
        lda     #$FF
        rts

; ===========================================================================
; serial_poll - Verifie si des donnees sont dans le buffer
; Sortie: A = non-zero si donnees dispo
; ===========================================================================
_serial_poll:
        lda     rx_head
        sec
        sbc     rx_tail
        rts

; ===========================================================================
; ISR ACIA - Lit l'octet recu et le stocke dans le buffer
;
; Avec --serial-irq-on-rdrf, l'IRQ est re-triggee tant que le FIFO
; de l'emulateur contient des donnees. L'ISR est appelee pour chaque
; octet disponible.
; ===========================================================================
acia_irq:
        lda     ACIA_STATUS
        and     #RDRF
        beq     @not_ours

        ; Lire l'octet (efface RDRF pour ce byte dans le FIFO)
        lda     ACIA_DATA

        ; Stocker dans le buffer circulaire
        ldx     rx_head
        sta     rx_buffer,x
        inx
        stx     rx_head

        sec             ; IRQ traitee
        rts

@not_ours:
        clc             ; Pas notre IRQ
        rts

        .interruptor acia_irq
