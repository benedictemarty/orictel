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
; driver AVANT de programmer l'ACIA. OricTel n'utilise plus que la base
; LOCI ($0380), partagee par le materiel LOCI reel et le PicoWiFiModemUSB
; (emule ou physique). Le mecanisme SMC et le discriminant de configuration
; sont conserves (la branche "horloge externe" reste du code mort inoffensif,
; jamais atteinte tant que la base vaut $0380).
; Le code etant en RAM ($0501+), le self-modifying code est legitime et
; le surcout est nul apres l'init (les operandes restent absolues).
; ===========================================================================

        .export _acia6551_init
        .export _acia6551_send_raw
        .export _acia6551_tx_ready
        .export _acia6551_recv
        .export _acia6551_poll
        .export _acia6551_dcd

        .importzp ptr1, ptr2, tmp1, tmp2

        .segment "CODE"

; Octet de poids faible de la base LOCI ($0380) : sert a distinguer la
; configuration ACIA reelle (LOCI/materiel) de l'emulateur ($031C).
ACIA_LOCI_LO = $80

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
; La config Control/Command depend de la base (etape 3):
;   - Emulateur ($031C) : Control=$00 (horloge externe -> "instant transfer"
;     Phosphoric, le mode le plus rapide sous emulateur), Command=$03
;     (DTR on, IRQ RX off, sans parite).
;   - LOCI/materiel ($0380) : Control=$1E (9600 bauds, 8N1, horloge interne),
;     Command=$0B (DTR+RTS actifs, sans parite, sans IRQ). Valeurs validees
;     par Phosphoric avec un vrai PicoWiFiModemUSB (sprint 60b, --loci).
; ===========================================================================
_acia6551_init:
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

        ; --- Choix Control/Command selon la base ACIA ---
        lda     ptr2            ; base poids faible
        cmp     #ACIA_LOCI_LO   ; $80 -> base $0380 = LOCI/materiel
        bne     @cfg_emu
        lda     #$1E            ; LOCI: 9600 bauds, 8N1, horloge interne
        sta     tmp1            ; -> Control
        lda     #$0B            ; LOCI: DTR+RTS actifs, sans parite, sans IRQ
        sta     tmp2            ; -> Command
        jmp     @prog
@cfg_emu:
        lda     #$00            ; Emu: horloge externe (instant transfer)
        sta     tmp1
        lda     #$03            ; Emu: DTR on, IRQ RX off
        sta     tmp2
@prog:
        ; --- Programmation ACIA (operandes deja patchees) ---
        lda     #$00
i_st1:  sta     ACIA_STATUS     ; Programmed reset (efface OVRN, TDRE=1)

        lda     tmp1
i_ct1:  sta     ACIA_CONTROL

        lda     tmp2
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
_acia6551_send_raw:
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
_acia6551_tx_ready:
t_st1:  lda     ACIA_STATUS
        and     #TDRE
        rts

; ===========================================================================
; serial_recv - Lecture directe ACIA (FIFO transparent)
; Avec --serial-buffer, RDRF=1 tant que FIFO non vide.
; Chaque lecture de DATA pop le prochain octet du FIFO.
; ===========================================================================
_acia6551_recv:
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
_acia6551_poll:
p_st1:  lda     ACIA_STATUS
        and     #RDRF
        rts

; ===========================================================================
; serial_dcd - Etat DCD
; ===========================================================================
_acia6551_dcd:
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
