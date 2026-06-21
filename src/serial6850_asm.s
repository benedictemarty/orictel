; ===========================================================================
; serial6850_asm.s - Driver ACIA 6850 + PIA 6821 du Digitelec DTL 2000
;
; Carte modem V23 DTL 2000 fidele emulee par Phosphoric (--dtl2000), fenetre
; $03F8-$03FD :
;   $03F8  PIA Port A   (DDRA si CRA bit2=0, ORA si CRA bit2=1)
;   $03F9  PIA CRA
;   $03FC  ACIA 6850    Control (W) / Status (R)
;   $03FD  ACIA 6850    Tx (W) / Rx (R)
;
; A la difference du MOS 6551 (4 registres, RDRF=$08/TDRE=$10) le Motorola
; 6850 n'a que 2 registres et des bits differents (RDRF=$01, TDRE=$02,
; DCD=$04). La composition se fait par le PIA (bit ligne), pas en Hayes AT :
; ce driver decroche directement (mode V23 asymetrique 75 TX / 1200 RX).
;
; Sequence d'init verifiee sur l'exemple period examples/dtl2000-test.bas :
;   CRA=$00 (DDRA) ; DDRA=$F4 ; ACIA reset=$03 ; CRA=$04 (ORA) ;
;   ORA=$D0 (ligne fermee = connectee, asym) ; ACIA=$09 (7E1 div16, RTS bas
;   = emission porteuse).
; ===========================================================================

        .export _acia6850_init
        .export _acia6850_send_raw
        .export _acia6850_tx_ready
        .export _acia6850_recv
        .export _acia6850_poll
        .export _acia6850_dcd

        .segment "CODE"

; Registres DTL 2000 (base $03F8)
DTL_PIA_A    = $03F8     ; DDRA / ORA
DTL_PIA_CRA  = $03F9     ; Control A (bit2 = selection DDR/OR)
DTL_ACIA_CS  = $03FC     ; ACIA Control (W) / Status (R)
DTL_ACIA_D   = $03FD     ; ACIA Tx (W) / Rx (R)

; Bits du registre Status ACIA 6850
SR_RDRF      = $01       ; Receive Data Register Full
SR_TDRE      = $02       ; Transmit Data Register Empty
SR_DCD       = $04       ; Data Carrier Detect (1 = porteuse perdue)

; ===========================================================================
; acia6850_init - Init V23 asymetrique + decroche ligne (connexion directe)
; ===========================================================================
_acia6850_init:
        lda     #$00
        sta     DTL_PIA_CRA     ; CRA bit2=0 -> acces DDRA
        lda     #$F4
        sta     DTL_PIA_A       ; DDRA = $F4 (b2,b4-b7 sortie)
        lda     #$03
        sta     DTL_ACIA_CS     ; ACIA master reset
        lda     #$04
        sta     DTL_PIA_CRA     ; CRA bit2=1 -> acces ORA
        lda     #$D0
        sta     DTL_PIA_A       ; ORA = $D0 (asym V23, ligne fermee=connectee)
        lda     #$09
        sta     DTL_ACIA_CS     ; 7E1, div16, RTS bas (emission porteuse)
        rts

; ===========================================================================
; acia6850_send_raw - Ecriture directe, bloque sur TDRE
; ===========================================================================
_acia6850_send_raw:
        pha
@wait:  lda     DTL_ACIA_CS
        and     #SR_TDRE
        beq     @wait
        pla
        sta     DTL_ACIA_D
        rts

; ===========================================================================
; acia6850_tx_ready - TDRE set = transmetteur pret (non bloquant)
; ===========================================================================
_acia6850_tx_ready:
        lda     DTL_ACIA_CS
        and     #SR_TDRE
        rts

; ===========================================================================
; acia6850_recv - Lecture directe (0xFF si rien)
; ===========================================================================
_acia6850_recv:
        lda     DTL_ACIA_CS
        and     #SR_RDRF
        beq     @empty
        lda     DTL_ACIA_D
        rts
@empty:
        lda     #$FF
        rts

; ===========================================================================
; acia6850_poll - RDRF set = donnee disponible
; ===========================================================================
_acia6850_poll:
        lda     DTL_ACIA_CS
        and     #SR_RDRF
        rts

; ===========================================================================
; acia6850_dcd - Etat DCD brut (bit2: 1 = porteuse perdue sur 6850).
; Non utilise par OricTel aujourd'hui ; fourni pour la symetrie d'API.
; ===========================================================================
_acia6850_dcd:
        lda     DTL_ACIA_CS
        and     #SR_DCD
        rts
