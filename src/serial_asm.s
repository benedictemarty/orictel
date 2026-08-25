; ===========================================================================
; serial_asm.s - Driver ACIA 6551 en polling pur (sans IRQ, sans ISR)
;
; v0.3.5 - retour au polling direct apres regression ISR (v0.3.3/v0.3.4) :
;
;   L'ISR splicee dans le vecteur IRQ ROM ($0245) gelait la machine des son
;   installation (tempete d'IRQ, PC bloque dans le handler ROM $EE22,
;   reproduit sur emulateur Phosphoric headless). Avec les IRQ ACIA
;   desactivees (Command=$07), une ISR n'apporte de toute facon plus rien :
;   le polling direct du registre STATUS suffit et a fait ses preuves
;   (v0.3.2, chaine complete validee jusqu'au service 3617.fr).
;
;   Cause racine du gel (v0.3.3/v0.3.4) : les Command $05/$07 ont les bits
;   TIC (2-3) a 01 = "RTS bas, IRQ TX ACTIVEE" (datasheet 6551). Comme TDRE
;   est leve en permanence, l'ACIA reassertait /IRQ sans fin -> le 6502 ne
;   sortait plus du handler IRQ -> gel total (clavier mort, ecran fige).
;   L'analyse v0.3.3 etait inversee : $0B (TIC=10 = RTS bas, IRQ TX
;   DESACTIVEE) etait la bonne valeur depuis v0.3.2.
;
;   Command=$0B : DTR (b0=1), IRQ RX desactivee (b1=1), TIC=10 (b2-3) =
;   RTS bas sans IRQ TX, sans parite. Aucune IRQ ACIA n'est generee :
;   le handler ROM Timer-1 reste seul maitre du vecteur IRQ.
;
;   Acquis ORICOMMS conserve - Control $18 : 1200 bauds, 8N1, horloge
;   interne (ORICOMMS et diag.c utilisent 1200 bauds, pas 9600/$1E).
;
;   La programmation des registres reste protegee par SEI/PLP (sequence
;   ORICOMMS 08 78 ... 28) : sur le vrai LOCI, une IRQ VIA au milieu du
;   cycle d'ecriture echantillonne par la MIA (PIO RP2040) corrompt l'acces.
;
;   v0.3.6 - meme protection etendue aux ACCES OCTET (send_raw / recv) :
;   pendant un handshake AT (des dizaines d'octets), l'IRQ Timer-1 ROM
;   (~100 Hz, une toutes les ~10 000 cy) tombait au milieu d'un acces $0380
;   -> octet TX/RX corrompu -> "OK"/"CONNECT" illisible -> AT en timeout sur
;   materiel reel (invisible en emulation, la MIA de Phosphoric ne corrompt
;   pas sur acces concomitant d'IRQ). L'ecriture DATA (send_raw) et la paire
;   status->data (recv) sont desormais encadrees par PHP/SEI ... PLP. La
;   BOUCLE D'ATTENTE TDRE reste HORS SEI (interruptible) pour ne pas affamer
;   l'IRQ Timer-1 : seul l'acces registre lui-meme est protege.
;
; ADRESSE ACIA CONFIGURABLE AU RUNTIME (self-modifying code)
; ----------------------------------------------------------
; serial_init(base) recoit la base de l'ACIA dans A (poids faible) / X
; (poids fort) et patche les operandes absolues. Le code etant en RAM
; ($0501+), le SMC est legitime et le surcout est nul apres l'init.
; ===========================================================================

        .export _acia6551_init
        .export _acia6551_send_raw
        .export _acia6551_tx_ready
        .export _acia6551_recv
        .export _acia6551_poll
        .export _acia6551_dcd

        .importzp ptr1, ptr2, tmp1, tmp2

ACIA_LOCI_LO = $80

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
; _acia6551_init - Patche les operandes puis programme l'ACIA
;
; Entree (cc65 __fastcall__): A = base poids faible, X = base poids fort.
;
; Config Control/Command selon la base :
;   LOCI ($0380) : Control=$18 (1200 bauds, 8N1, horloge interne)
;                  Command=$0B (DTR, IRQ RX off, TIC=10: RTS bas sans IRQ TX)
;   Emu  ($031C) : Control=$00 (horloge externe, instant transfer Phosphoric)
;                  Command=$03 (DTR, sans IRQ)
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
        cpx     #(3*12)         ; 12 sites patches (init x5, send x2, tx_ready,
        bne     @patch          ;   recv x2, poll, dcd)

        ; Config selon la base ACIA
        lda     ptr2
        cmp     #ACIA_LOCI_LO
        bne     @cfg_emu
        lda     #$18            ; LOCI: 1200 bauds, 8N1, horloge interne
        sta     tmp1
        lda     #$0B            ; LOCI: DTR, IRQ RX off, TIC=10 (pas d'IRQ TX)
        sta     tmp2            ; ($05/$07 avaient TIC=01 = IRQ TX -> tempete IRQ)
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
        plp
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
s_st1:  lda     ACIA_STATUS     ; attente TDRE : boucle HORS SEI (interruptible)
        and     #TDRE
        bne     s_ok
        dex
        bne     s_st1
        dey
        bne     s_st1
        pla                     ; timeout borne : octet abandonne (pas de gel)
        rts
s_ok:   pla                     ; A = octet a emettre (pile nettoyee)
        php                     ; --- section critique (comme l'init SEI/PLP) ---
        sei
s_da1:  sta     ACIA_DATA       ; ecriture echantillonnee par la MIA, non scindee
        plp                     ; --- fin : restaure l'indicateur I de l'appelant ---
        rts

; ===========================================================================
; _acia6551_tx_ready - TDRE set = transmetteur pret (non bloquant)
; ===========================================================================
_acia6551_tx_ready:
t_st1:  lda     ACIA_STATUS
        and     #TDRE
        rts

; ===========================================================================
; _acia6551_recv - Lecture ACIA non bloquante (polling STATUS direct)
; ===========================================================================
_acia6551_recv:
        php                     ; --- section critique : status puis data consecutifs ---
        sei
r_st1:  lda     ACIA_STATUS     ; la MIA LOCI exige la paire lecture-status/lecture-data
        and     #RDRF
        beq     r_empty         ;   non separee par une IRQ VIA
r_da1:  lda     ACIA_DATA       ; consomme l'octet RX immediatement apres le status
        plp                     ; --- fin : restaure I ---
        rts
r_empty:
        lda     #$FF
        plp                     ; restaure I aussi sur le chemin "vide"
        rts

; ===========================================================================
; _acia6551_poll - RDRF set = donnee disponible (polling STATUS direct)
; ===========================================================================
_acia6551_poll:
p_st1:  lda     ACIA_STATUS
        and     #RDRF
        rts

; ===========================================================================
; _acia6551_dcd - Etat DCD (polling STATUS direct)
; ===========================================================================
_acia6551_dcd:
d_st1:  lda     ACIA_STATUS
        and     #DCD_BIT
        rts

; ===========================================================================
; Table de patch: 12 entrees (instruction+1, offset registre)
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
