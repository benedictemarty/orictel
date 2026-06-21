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
;
; ADRESSE ACIA CONFIGURABLE AU RUNTIME (self-modifying code)
; ----------------------------------------------------------
; serial_init(base) recoit la base de l'ACIA dans A (poids faible) / X
; (poids fort) et patche les operandes absolues des instructions de ce
; driver AVANT de programmer l'ACIA. Cela permet le meme binaire pour:
;   - $031C : ACIA emulee Phosphoric/Euphoric
;   - $0380 : ACIA emulee par la cartouche LOCI reelle (modem USB CDC)
; Le code etant en RAM ($0501+), le self-modifying code est legitime et
; le surcout est nul apres l'init (les operandes restent absolues).
; ===========================================================================

        .export _serial_init
        .export _serial_send_raw
        .export _serial_tx_ready
        .export _serial_recv
        .export _serial_poll
        .export _serial_dcd

        .importzp ptr1, ptr2

        .segment "CODE"

; Valeurs par defaut (placeholders): operandes reecrites par serial_init.
; Conservees > $00FF pour forcer un encodage absolu (3 octets) a l'assemblage.
ACIA_DATA    = $031C
ACIA_STATUS  = $031D
ACIA_COMMAND = $031E
ACIA_CONTROL = $031F

RDRF         = $08
TDRE         = $10
DCD_BIT      = $20

; Offsets des registres relativement a la base ACIA
OFF_DATA     = 0
OFF_STATUS   = 1
OFF_COMMAND  = 2
OFF_CONTROL  = 3

; ===========================================================================
; serial_init(unsigned base) - Patche les operandes puis programme l'ACIA
;
; Entree (cc65 __fastcall__): A = base poids faible, X = base poids fort.
;
; Etape 1: pour chaque site d'acces ACIA, ecrire base+offset dans les deux
;          octets d'operande de l'instruction (self-modifying code).
; Etape 2: programmer l'ACIA (les instructions sont desormais patchees).
;
; Control: $00 = horloge externe, 8 bits, 1 stop. Phosphoric traite
;   l'horloge externe en "instant transfer": aucun cadencement baud,
;   les octets sont disponibles des leur arrivee TCP. C'est le mode
;   le plus rapide sous emulateur.
;   Le V23 reel 1200/75 7E1 demanderait Control=$18 + parite Command.
; Command: $03 = DTR on, RX IRQ off, no parity
; ===========================================================================
_serial_init:
        sta     ptr2            ; base poids faible
        stx     ptr2+1          ; base poids fort

        ldx     #0              ; index dans patchtab (3 octets/entree)
@patch:
        lda     patchtab,x      ; adresse operande (poids faible)
        sta     ptr1
        lda     patchtab+1,x    ; adresse operande (poids fort)
        sta     ptr1+1
        lda     patchtab+2,x    ; offset registre (0..3)
        clc
        adc     ptr2            ; + base poids faible
        ldy     #0
        sta     (ptr1),y        ; operande poids faible
        lda     ptr2+1
        adc     #0              ; + retenue
        iny
        sta     (ptr1),y        ; operande poids fort
        inx
        inx
        inx
        cpx     #(3*12)         ; 12 sites patches ?
        bne     @patch

        ; --- Programmation ACIA (operandes deja patchees) ---
        lda     #$00
i_st1:  sta     ACIA_STATUS     ; Programmed reset (efface OVRN, TDRE=1)

        lda     #$00            ; Horloge externe = instant transfer
i_ct1:  sta     ACIA_CONTROL

        lda     #$03            ; DTR on, IRQ RX off
i_cm1:  sta     ACIA_COMMAND

i_st2:  lda     ACIA_STATUS     ; Lire status pour effacer IRQ pending
i_da1:  lda     ACIA_DATA       ; Clear RDR
        rts

; ===========================================================================
; serial_send_raw - Ecriture directe ACIA, bloque sur TDRE
; Ne pas appeler directement depuis le code applicatif: passer par
; serial_send() (file TX logicielle, serial_tx.c) pour ne pas bloquer
; la reception pendant l'attente TDRE.
; ===========================================================================
_serial_send_raw:
        pha
s_st1:  lda     ACIA_STATUS
        and     #TDRE
        beq     s_st1
        pla
s_da1:  sta     ACIA_DATA
        rts

; ===========================================================================
; serial_tx_ready - TDRE set = transmetteur pret (non bloquant)
; ===========================================================================
_serial_tx_ready:
t_st1:  lda     ACIA_STATUS
        and     #TDRE
        rts

; ===========================================================================
; serial_recv - Lecture directe ACIA (FIFO transparent)
; Avec --serial-buffer, RDRF=1 tant que FIFO non vide.
; Chaque lecture de DATA pop le prochain octet du FIFO.
; ===========================================================================
_serial_recv:
r_st1:  lda     ACIA_STATUS
        and     #RDRF
        beq     r_empty
r_da1:  lda     ACIA_DATA
        rts
r_empty:
        lda     #$FF
        rts

; ===========================================================================
; serial_poll - RDRF set = donnees dans le FIFO
; ===========================================================================
_serial_poll:
p_st1:  lda     ACIA_STATUS
        and     #RDRF
        rts

; ===========================================================================
; serial_dcd - Etat DCD
; ===========================================================================
_serial_dcd:
d_st1:  lda     ACIA_STATUS
        and     #DCD_BIT
        rts

; ===========================================================================
; Table de patch: pour chaque site, adresse de l'operande (instruction+1)
; et offset du registre vise. Parcourue par serial_init.
; ===========================================================================
patchtab:
        .word   i_st1+1
        .byte   OFF_STATUS
        .word   i_ct1+1
        .byte   OFF_CONTROL
        .word   i_cm1+1
        .byte   OFF_COMMAND
        .word   i_st2+1
        .byte   OFF_STATUS
        .word   i_da1+1
        .byte   OFF_DATA
        .word   s_st1+1
        .byte   OFF_STATUS
        .word   s_da1+1
        .byte   OFF_DATA
        .word   t_st1+1
        .byte   OFF_STATUS
        .word   r_st1+1
        .byte   OFF_STATUS
        .word   r_da1+1
        .byte   OFF_DATA
        .word   p_st1+1
        .byte   OFF_STATUS
        .word   d_st1+1
        .byte   OFF_STATUS
