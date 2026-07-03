; ===========================================================================
; serial_asm.s - Driver ACIA 6551 avec ISR Timer-1 (approche ORICOMMS)
;
; v0.3.3 - corrections issues du desassemblage ORICOMMS (Dbug/Github) :
;
;   BUG 1 - Command $0B activait accidentellement les IRQ TX :
;     $0B = 0b00001011 : bits 2-3 = 10 = TX on + IRQ TX active.
;     Sur le vrai materiel, chaque TDRE generait une IRQ non geree,
;     le gestionnaire ROM la traitait comme Timer-1 -> instabilite.
;     Correction : Command $05 (DTR, IRQ RX actif, TX on sans IRQ TX),
;     identique a la config ORICOMMS.
;
;   BUG 2 - Control $1E = 9600 bauds alors que ORICOMMS et diag.c
;     utilisent 1200 bauds ($18). Corrige : $18 (1200 bauds, 8N1).
;
;   AMELIORATION - ISR serial_isr splicee dans le vecteur IRQ ROM
;     ($0245), approche directe d'ORICOMMS :
;     . Latche ACIA_STATUS dans _acia_rx_status a chaque IRQ.
;     . Consomme les IRQs ACIA (bit7=1, RTI).
;     . Chaine vers le gestionnaire ROM original pour les IRQs VIA
;       (Timer-1 100Hz, scan clavier).
;     -> Les IRQs ACIA ne tombent plus dans le handler ROM.
;
; ADRESSE ACIA CONFIGURABLE AU RUNTIME (self-modifying code)
; ----------------------------------------------------------
; serial_init(base) recoit la base de l'ACIA dans A (poids faible) / X
; (poids fort) et patche les operandes absolues. ISR installee uniquement
; pour la base LOCI ($0380). Le code etant en RAM ($0501+), le SMC est
; legitime et le surcout est nul apres l'init.
; ===========================================================================

        .export _acia6551_init
        .export _acia6551_send_raw
        .export _acia6551_tx_ready
        .export _acia6551_recv
        .export _acia6551_poll
        .export _acia6551_dcd
        .export _acia6551_isr_remove

        .importzp ptr1, ptr2, tmp1, tmp2

; Vecteur IRQ ROM : operande du JMP a $0244 (adresse du handler utilisateur)
IRQ_VECTOR      = $0245

; Bit IRQ ACIA dans STATUS (bit 7)
ACIA_IRQ_BIT    = $80

ACIA_LOCI_LO    = $80

; Valeurs placeholders (reecrites par serial_init, > $00FF -> encodage absolu)
ACIA_DATA    = $031C
ACIA_STATUS  = $031D
ACIA_COMMAND = $031E
ACIA_CONTROL = $031F

RDRF         = $08
TDRE         = $10
DCD_BIT      = $20

OFF_DATA     = 0
OFF_STATUS   = 1
OFF_COMMAND  = 2
OFF_CONTROL  = 3

; ===========================================================================
; Variables BSS
;   _acia_rx_status : latch ACIA_STATUS mis a jour par serial_isr
;   _acia_irq_chain : 2 octets = ancien vecteur IRQ, pour le chainage ISR
; ===========================================================================
        .segment "BSS"
_acia_rx_status:  .res 1
_acia_irq_chain:  .res 2
        .segment "CODE"

; ===========================================================================
; serial_isr - ISR splicee dans le vecteur IRQ ROM ($0244)
;
; v0.3.4 : Command=$07 desactive les IRQ RX/TX ACIA. L'ISR tourne sur chaque
; tick Timer-1 (100Hz) et latche ACIA_STATUS de facon atomique ; elle enchaine
; toujours vers le handler ROM (bit7 jamais mis). Le latch permet a poll() et
; dcd() de lire le status cache sans acceder au hardware en section non-critique.
;
; La pile en entree contient : [SR][PC_hi][PC_lo] (push hardware IRQ).
; ===========================================================================
serial_isr:
        pha
isr_s1: lda     ACIA_STATUS         ; operande patchee par serial_init
        sta     _acia_rx_status     ; latch status pour le main loop
        and     #ACIA_IRQ_BIT       ; bit7 = IRQ ACIA ?
        bne     @consume
        pla
        jmp     (_acia_irq_chain)   ; IRQ VIA : chaine vers handler ROM original
@consume:
        pla
        rti                         ; IRQ ACIA : consomme, retour au code interrompu

; ===========================================================================
; _acia6551_isr_remove - Restaure le vecteur IRQ ROM original
; Appeler avant retour au BASIC ou reset propre.
; ===========================================================================
_acia6551_isr_remove:
        php
        sei
        lda     _acia_irq_chain
        sta     IRQ_VECTOR
        lda     _acia_irq_chain+1
        sta     IRQ_VECTOR+1
        plp
        rts

; ===========================================================================
; _acia6551_init - Patche les operandes puis programme l'ACIA
;
; Entree (cc65 __fastcall__): A = base poids faible, X = base poids fort.
;
; Config Control/Command selon la base :
;   LOCI ($0380) : Control=$18 (1200 bauds, 8N1, horloge interne)
;                  Command=$07 (DTR, IRQ RX desactive, TX on sans IRQ TX)
;                  -> installe serial_isr (latch STATUS sur Timer-1)
;   Emu  ($031C) : Control=$00 (horloge externe, instant transfer Phosphoric)
;                  Command=$03 (DTR, sans IRQ)
;                  -> pas d'ISR
; ===========================================================================
_acia6551_init:
        sta     ptr2            ; base poids faible
        stx     ptr2+1          ; base poids fort

        ldx     #0
@patch:
        lda     patchtab,x      ; adresse operande (poids faible)
        sta     ptr1
        lda     patchtab+1,x    ; adresse operande (poids fort)
        sta     ptr1+1
        lda     patchtab+2,x    ; offset registre (0..3)
        clc
        adc     ptr2
        ldy     #0
        sta     (ptr1),y
        lda     ptr2+1
        adc     #0
        iny
        sta     (ptr1),y
        inx
        inx
        inx
        cpx     #(3*10)         ; 10 sites patches (init×5, send×2, tx_ready×1, recv×1, isr×1)
        bne     @patch

        ; Config selon la base ACIA
        lda     ptr2
        cmp     #ACIA_LOCI_LO
        bne     @cfg_emu
        lda     #$18            ; LOCI: 1200 bauds, 8N1, horloge interne
        sta     tmp1
        lda     #$07            ; LOCI: DTR, IRQ RX desactive, TX on sans IRQ TX
        sta     tmp2            ; ($05 causait gel : RDRF leve -> /IRQ continu)
        jmp     @prog
@cfg_emu:
        lda     #$00            ; Emu: horloge externe (instant transfer)
        sta     tmp1
        lda     #$03            ; Emu: DTR, sans IRQ
        sta     tmp2

@prog:
        ; Programmation ACIA (operandes deja patchees, IRQs masquees)
        php
        sei
        lda     #$00
i_st1:  sta     ACIA_STATUS     ; Programmed reset
        lda     tmp1
i_ct1:  sta     ACIA_CONTROL
        lda     tmp2
i_cm1:  sta     ACIA_COMMAND
i_st2:  lda     ACIA_STATUS     ; Clear IRQ pending
i_da1:  lda     ACIA_DATA       ; Clear RDR
        sta     _acia_rx_status ; Init latch = status initial
        plp

        ; Installer ISR uniquement pour LOCI
        lda     ptr2
        cmp     #ACIA_LOCI_LO
        bne     @done

        ; Sauvegarder ancien vecteur IRQ dans _acia_irq_chain (chainage)
        php
        sei
        lda     IRQ_VECTOR
        sta     _acia_irq_chain
        lda     IRQ_VECTOR+1
        sta     _acia_irq_chain+1
        ; Installer serial_isr
        lda     #<serial_isr
        sta     IRQ_VECTOR
        lda     #>serial_isr
        sta     IRQ_VECTOR+1
        plp
@done:
        rts

; ===========================================================================
; _acia6551_send_raw - Ecriture directe ACIA, attend TDRE (attente BORNEE)
; Attente plafonnee a ~65536 iterations : sur vrai materiel un modem fige
; ne gele plus l'Oric (degradation gracieuse).
; ===========================================================================
_acia6551_send_raw:
        pha
        ldx     #0
        ldy     #0
s_st1:  lda     ACIA_STATUS
        and     #TDRE
        bne     s_ok
        dex
        bne     s_st1
        dey
        bne     s_st1
        pla
        rts
s_ok:   pla
s_da1:  sta     ACIA_DATA
        rts

; ===========================================================================
; _acia6551_tx_ready - TDRE set = transmetteur pret (non bloquant)
; ===========================================================================
_acia6551_tx_ready:
t_st1:  lda     ACIA_STATUS
        and     #TDRE
        rts

; ===========================================================================
; _acia6551_recv - Lecture ACIA (RDRF via latch ISR, DATA depuis hardware)
; Apres lecture de DATA, efface RDRF dans le latch ($FF ^ $08 = $F7) pour
; eviter une double-lecture si poll() est rappele avant le prochain tick ISR.
; ===========================================================================
_acia6551_recv:
        lda     _acia_rx_status
        and     #RDRF
        beq     r_empty
r_da1:  lda     ACIA_DATA           ; lit DATA (RDRF materiel repasse a 0)
        pha
        lda     _acia_rx_status
        and     #($FF ^ RDRF)       ; efface RDRF du latch (= $F7)
        sta     _acia_rx_status
        pla
        rts
r_empty:
        lda     #$FF
        rts

; ===========================================================================
; _acia6551_poll - RDRF set = donnee disponible (via latch ISR)
; ===========================================================================
_acia6551_poll:
        lda     _acia_rx_status
        and     #RDRF
        rts

; ===========================================================================
; _acia6551_dcd - Etat DCD (via latch ISR)
; ===========================================================================
_acia6551_dcd:
        lda     _acia_rx_status
        and     #DCD_BIT
        rts

; ===========================================================================
; Table de patch: 13 entrees (instruction+1, offset registre)
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
        .word   r_da1+1
        .byte   OFF_DATA
        .word   isr_s1+1
        .byte   OFF_STATUS
