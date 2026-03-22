; ===========================================================================
; serial_asm.s - Driver ACIA 6551 pour OricTel
;
; Supporte deux modes de fonctionnement:
;
; 1. Backend Digitelec DTL 2000 (recommande):
;    --serial digitelec:host:port
;    Buffer 512 octets + CTS flow control dans le modem.
;    V23 automatique. ACIA 100% fidele au datasheet MOS.
;    DTR pour connecter, DCD pour detecter la porteuse.
;
; 2. Backend TCP + patchs ACIA:
;    --serial tcp:host:port --serial-buffer 256 --serial-irq-on-rdrf
;    FIFO 256 dans l'ACIA + IRQ fiable.
;
; Dans les deux cas, l'ISR bufferise les octets en reception.
;
; Fonctions exportees:
;   _serial_init  - Initialise ACIA + IRQ + DTR
;   _serial_send  - Envoie un octet (polling TDRE)
;   _serial_recv  - Lit depuis le buffer logiciel
;   _serial_poll  - Verifie le buffer logiciel
;   _serial_dcd   - Verifie DCD (porteuse/connexion)
; ===========================================================================

        .export _serial_init
        .export _serial_send
        .export _serial_recv
        .export _serial_poll
        .export _serial_dcd

        .segment "CODE"

; --- Adresses ACIA 6551 ---
ACIA_DATA    = $031C
ACIA_STATUS  = $031D
ACIA_COMMAND = $031E
ACIA_CONTROL = $031F

; --- Bits Status ---
RDRF         = $08          ; Bit 3: donnee recue disponible
TDRE         = $10          ; Bit 4: transmetteur pret
DCD_BIT      = $20          ; Bit 5: Data Carrier Detect (0=connecte)

; --- Buffer circulaire en page zero ---
rx_head      = $E0
rx_tail      = $E1

        .segment "BSS"
rx_buffer:   .res 256

        .segment "CODE"

; ===========================================================================
; serial_init - Initialise l'ACIA
;
; Control: 7 bits, 1 stop, 1200 baud, horloge interne
;   Bits 6-5 = 01 (7 bits), bit 4 = 1 (interne), bits 3-0 = $08 (1200)
;   => $28  (pour mode Minitel V23: 7E1)
;   Note: avec le backend Digitelec, le V23 est automatique
;         et override le baud rate.
;
; Command: DTR on, RX IRQ active, RTS low, parite paire
;   Bit 0 = 1 (DTR = decrocher)
;   Bit 1 = 0 (RX IRQ active)
;   Bits 3-2 = 10 (RTS low, TX IRQ off)
;   Bit 5 = 1 (parite activee)
;   Bits 7-6 = 01 (parite paire)
;   => $69
; ===========================================================================
_serial_init:
        ; Buffer circulaire a zero
        lda     #0
        sta     rx_head
        sta     rx_tail

        ; Reset programme ACIA
        sta     ACIA_STATUS

        ; Control: 8 bits, 1 stop, 19200 baud, horloge interne
        ; Compatible TCP brut (bridge) et Digitelec (V23 override)
        lda     #$1F
        sta     ACIA_CONTROL

        ; Command: DTR on, RX IRQ active, RTS low, pas de parite
        lda     #$09
        sta     ACIA_COMMAND

        ; Vider le RDR
        lda     ACIA_DATA

        ; Activer les interruptions CPU
        cli
        rts

; ===========================================================================
; serial_send - Envoie un octet (polling TDRE)
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
; serial_recv - Lit un octet du buffer circulaire
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
; serial_poll - Verifie si des donnees dans le buffer
; ===========================================================================
_serial_poll:
        lda     rx_head
        sec
        sbc     rx_tail
        rts

; ===========================================================================
; serial_dcd - Verifie DCD (Data Carrier Detect)
; Sortie: A = 0 si connecte, non-zero si pas de porteuse
; (DCD est actif bas sur le 6551: bit 5 = 0 = connecte)
; ===========================================================================
_serial_dcd:
        lda     ACIA_STATUS
        and     #DCD_BIT
        rts

; ===========================================================================
; ISR ACIA - Reception par interruption
; ===========================================================================
acia_irq:
        lda     ACIA_STATUS
        and     #RDRF
        beq     @not_ours
        lda     ACIA_DATA
        ldx     rx_head
        sta     rx_buffer,x
        inx
        stx     rx_head
        sec
        rts
@not_ours:
        clc
        rts

        .interruptor acia_irq
