; ===========================================================================
; serial_asm.s - Driver ACIA 6551 avec reception IRQ et buffer circulaire
;
; MOS 6551 ACIA mappee a $031C-$031F.
; Reception par interruption: l'ISR lit les octets et les stocke dans
; un buffer circulaire de 256 octets. Le programme principal lit le
; buffer sans risque d'overrun.
;
; Fonctions exportees:
;   _serial_init  - Initialise ACIA + IRQ
;   _serial_send  - Envoie un octet (polling TDRE)
;   _serial_recv  - Lit un octet du buffer (non-bloquant)
;   _serial_poll  - Verifie si donnees dans le buffer
; ===========================================================================

        .export _serial_init
        .export _serial_send
        .export _serial_recv
        .export _serial_poll

        .import __INTERRUPTOR_COUNT__

        .segment "CODE"

; --- Adresses ACIA 6551 ---
ACIA_DATA    = $031C
ACIA_STATUS  = $031D
ACIA_COMMAND = $031E
ACIA_CONTROL = $031F

; --- Bits Status ---
RDRF         = $08          ; Bit 3: donnee recue disponible
TDRE         = $10          ; Bit 4: transmetteur pret
IRQ_BIT      = $80          ; Bit 7: IRQ occurred

; --- Buffer circulaire en page zero (rapide) ---
; On utilise 2 octets en page zero pour les pointeurs head/tail
; et 256 octets en BSS pour le buffer
rx_head      = $E0          ; Pointeur ecriture (ISR)
rx_tail      = $E1          ; Pointeur lecture (programme principal)

        .segment "BSS"
rx_buffer:   .res 256       ; Buffer circulaire 256 octets

        .segment "CODE"

; ===========================================================================
; serial_init - Initialise l'ACIA avec reception par IRQ
;
; Control: 8 bits, 1 stop, 1200 baud, horloge interne = $18
; Command: pas de parite, DTR on, RTS low, RX IRQ ACTIVE = $09
;   Bit 0 = 1 (DTR)
;   Bit 1 = 0 (RX IRQ activee!) <-- difference avec version polling
;   Bits 3-2 = 10 (RTS low, TX IRQ off)
;   Le reste = 0
; ===========================================================================
_serial_init:
        ; Initialiser le buffer circulaire
        lda     #0
        sta     rx_head
        sta     rx_tail

        ; Reset programme ACIA
        sta     ACIA_STATUS

        ; Control: 8 bits, 1 stop, 1200 baud, horloge interne
        lda     #$18
        sta     ACIA_CONTROL

        ; Command: DTR on, RX IRQ active, RTS low, TX IRQ off, no parity
        lda     #$09
        sta     ACIA_COMMAND

        ; Vider le buffer de reception
        lda     ACIA_DATA

        ; Activer les interruptions CPU
        cli

        rts

; ===========================================================================
; serial_send - Envoie un octet (polling TDRE)
; Entree: A = octet
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
; serial_recv - Lit un octet du buffer circulaire (non-bloquant)
; Sortie: A = octet, ou $FF si buffer vide
; ===========================================================================
_serial_recv:
        ; Verifier si le buffer est vide
        lda     rx_head
        cmp     rx_tail
        beq     @empty

        ; Lire l'octet depuis le buffer
        ldx     rx_tail
        lda     rx_buffer,x

        ; Avancer le pointeur tail
        inx
        stx     rx_tail         ; Wrapping naturel sur 8 bits (0-255)

        rts

@empty:
        lda     #$FF
        rts

; ===========================================================================
; serial_poll - Verifie si des donnees sont dans le buffer
; Sortie: A = non-zero si donnees dispo, 0 sinon
; ===========================================================================
_serial_poll:
        lda     rx_head
        sec
        sbc     rx_tail         ; A = head - tail (nombre d'octets dispo)
        rts

; ===========================================================================
; ACIA IRQ handler - Appele par le mecanisme d'interruption cc65
;
; Lit l'octet recu de l'ACIA et le stocke dans le buffer circulaire.
; Retourne avec carry set si l'IRQ a ete traitee.
;
; Enregistre comme "interruptor" via le segment RODATA et la
; directive .interruptor de cc65.
; ===========================================================================

        .segment "RODATA"
        ; Rien ici, l'interruptor est declare plus bas

        .segment "CODE"

acia_irq:
        ; Verifier si c'est notre IRQ (ACIA status bit 7)
        lda     ACIA_STATUS
        bpl     @not_ours       ; Bit 7 = 0 -> pas notre IRQ

        ; Verifier RDRF
        and     #RDRF
        beq     @not_ours       ; Pas de donnee

        ; Lire l'octet (cela efface RDRF et IRQ)
        lda     ACIA_DATA

        ; Stocker dans le buffer circulaire
        ldx     rx_head
        sta     rx_buffer,x
        inx
        stx     rx_head         ; Wrapping naturel sur 8 bits

        ; IRQ traitee: carry set
        sec
        rts

@not_ours:
        ; Pas notre IRQ: carry clear
        clc
        rts

        ; Declarer comme interrupteur cc65
        .interruptor acia_irq
