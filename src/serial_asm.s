; ===========================================================================
; serial_asm.s - Driver ACIA 6551 bas niveau pour OricTel
;
; MOS 6551 ACIA mappee a $031C-$031F
; Configuration: Minitel V23 - 7 bits, parite paire, 1 stop, 1200 baud
;
; Fonctions exportees (convention cc65 __fastcall__):
;   _serial_init  - Initialise l'ACIA
;   _serial_send  - Envoie un octet (A = donnee)
;   _serial_recv  - Recoit un octet (A = donnee, $FF si vide)
;   _serial_poll  - Verifie donnee dispo (A = status & RDRF)
; ===========================================================================

        .export _serial_init
        .export _serial_send
        .export _serial_recv
        .export _serial_poll

; --- Adresses ACIA 6551 ---
ACIA_DATA    = $031C        ; R: RDR, W: TDR
ACIA_STATUS  = $031D        ; R: Status, W: Reset
ACIA_COMMAND = $031E        ; R/W: Command
ACIA_CONTROL = $031F        ; R/W: Control

; --- Bits Status ---
RDRF         = $08          ; Bit 3: donnee recue disponible
TDRE         = $10          ; Bit 4: transmetteur pret

; ===========================================================================
; serial_init - Initialise l'ACIA pour mode Minitel V23
;
; Control Register ($031F):
;   Bit 7   = 0 : 1 stop bit
;   Bits 6-5 = 01: 7 data bits
;   Bit 4   = 1 : horloge interne (baud rate generator)
;   Bits 3-0 = 1000: 1200 baud
;   => $38
;
; Command Register ($031E):
;   Bits 7-6 = 01: parite paire
;   Bit 5   = 1 : parite activee
;   Bit 4   = 0 : echo off
;   Bits 3-2 = 10: RTS low, TX IRQ off
;   Bit 1   = 1 : RX IRQ desactive (polling)
;   Bit 0   = 1 : DTR actif
;   => $6B
; ===========================================================================
_serial_init:
        ; Reset programme (ecriture a $031D)
        sta     ACIA_STATUS

        ; Configuration controle: 7 bits, 1 stop, 1200 baud, horloge interne
        lda     #$38
        sta     ACIA_CONTROL

        ; Configuration commande: parite paire, DTR on, RTS low, IRQ off
        lda     #$6B
        sta     ACIA_COMMAND

        ; Lecture pour vider le buffer de reception
        lda     ACIA_DATA
        rts

; ===========================================================================
; serial_send - Envoie un octet
; Entree: A = octet a envoyer (parametre cc65 __fastcall__)
; Sortie: rien
; Modifie: A, X preserves
; ===========================================================================
_serial_send:
        pha                 ; Sauvegarder l'octet a envoyer
@wait_tdre:
        lda     ACIA_STATUS
        and     #TDRE       ; Transmetteur pret ?
        beq     @wait_tdre  ; Non, attendre
        pla                 ; Recuperer l'octet
        and     #$7F        ; Masquer bit 7 (7 bits Minitel)
        sta     ACIA_DATA   ; Envoyer
        rts

; ===========================================================================
; serial_recv - Recoit un octet (non-bloquant)
; Entree: rien
; Sortie: A = octet recu, ou $FF si aucune donnee
; Modifie: A
; ===========================================================================
_serial_recv:
        lda     ACIA_STATUS
        and     #RDRF       ; Donnee disponible ?
        beq     @no_data    ; Non
        lda     ACIA_DATA   ; Lire l'octet recu
        and     #$7F        ; Masquer bit 7 (7 bits)
        rts
@no_data:
        lda     #$FF        ; Pas de donnee
        rts

; ===========================================================================
; serial_poll - Verifie si une donnee est disponible
; Entree: rien
; Sortie: A = non-zero si donnee dispo, 0 sinon
; Modifie: A
; ===========================================================================
_serial_poll:
        lda     ACIA_STATUS
        and     #RDRF
        rts
