; ===========================================================================
; display_asm.s - Moteur de rendu cellules en assembleur
;
; 1) _blit_cell8: trace UNE cellule (8 octets de glyphe) en HIRES:
;      dst[ligne*40] = (src[ligne] AND and_tbl[ligne]) OR or_mask
;    La table AND porte le dithering couleur (ou $3F = neutre), le
;    masque OR porte bit6 (mode pixel) + bit7 (inversion video).
;
; 2) _blit_run: rend une SUITE de cellules vtx_cell_t contigues, en
;    resolvant les glyphes G0/G1 directement en assembleur. S'arrete
;    (et retourne le nombre de cellules consommees) des qu'une cellule
;    sort du fast-path: taille non normale, G2, flash, concealed ou
;    souligne. L'appelant C rend cette cellule puis relance.
;    ~300 cycles/cellule contre ~1000+ pour l'enrobage C autour de
;    blit_cell8 (appel cc65 ~300 cycles + chaine de branchements).
;
; Depend de l'ordre des champs de vtx_cell_t (videotex.h):
;   0=ch, 1=charset, 2=fg, 3=bg, 4=flags, 5=size (6 octets)
; et des flags: FLASH=$01 CONCEALED=$02 INVERT=$04 UNDERLINE=$08
; SEPARATED=$10.
;
; Utilise ptr1-ptr4/tmp1/tmp2/sreg (scratch zero page du runtime cc65,
; libres pendant un appel de fonction, IRQ ROM Oric ne les touche pas).
; ===========================================================================

        .export _blit_cell8
        .export _blit_src, _blit_dst, _blit_and, _blit_or
        .export _blit_run
        .export _run_cells, _run_dst, _run_count, _run_mode
        .export _display_clear
        .import _font_g0, _no_dither, _g1_dither, _g1_cache, _g1_glyph_60
        .importzp ptr1, ptr2, ptr3, ptr4, tmp1, tmp2, sreg

        .segment "BSS"
_blit_src:  .res 2          ; glyphe source (8 octets)
_blit_dst:  .res 2          ; destination HIRES (1ere scanline)
_blit_and:  .res 2          ; table AND 8 octets (dither ou neutre)
_blit_or:   .res 1          ; masque OR ($40 ou $C0 inversion)

_run_cells: .res 2          ; 1ere cellule vtx_cell_t de la plage
_run_dst:   .res 2          ; destination HIRES de la 1ere cellule
_run_count: .res 1          ; nombre max de cellules
_run_mode:  .res 1          ; dither: 0=aucun, 1=G1 seul, 2=tout

        .segment "CODE"

; Une ligne: Y=ligne pour src/and, puis Y=offset destination
.macro  BLITLINE line, dofs
        ldy     #line
        lda     (ptr1),y
        and     (ptr3),y
        ora     tmp1
        ldy     #dofs
        sta     (ptr2),y
.endmacro

; ---------------------------------------------------------------------------
; do_blit - trace 8 lignes: (ptr1) AND (ptr3) OR tmp1 -> (ptr2)
; Clobbers: A, Y, ptr2. Preserve X.
; ---------------------------------------------------------------------------
do_blit:
        BLITLINE 0, 0
        BLITLINE 1, 40
        BLITLINE 2, 80
        BLITLINE 3, 120

        ; Moitie basse: dst += 160 (l'offset Y plafonne a 255, 280 ne
        ; passe pas pour la ligne 7)
        clc
        lda     ptr2
        adc     #160
        sta     ptr2
        bcc     @nohi
        inc     ptr2+1
@nohi:
        BLITLINE 4, 0
        BLITLINE 5, 40
        BLITLINE 6, 80
        BLITLINE 7, 120
        rts

; ---------------------------------------------------------------------------
; display_clear - remplit le framebuffer HIRES ($A000-$BF3F, 8000
; octets) avec $40 (bit6 = mode pixel, pixels eteints).
; Fill deroule par pages: ~5 ms, contre ~920 ms pour la boucle C cc65
; (~115 cycles/octet d'acces pile) qui plombait chaque vtx_clear_page.
; ---------------------------------------------------------------------------
_display_clear:
        lda     #$40
        ldx     #0
@page:
        .repeat 31, P
        sta     $A000 + P*256,x
        .endrepeat
        inx
        bne     @page
        ; queue: $BF00-$BF3F (64 octets)
        ldx     #$3F
@tail:  sta     $BF00,x
        dex
        bpl     @tail
        rts

; ---------------------------------------------------------------------------
; blit_cell8 - une cellule via les globales _blit_*
; ---------------------------------------------------------------------------
_blit_cell8:
        lda     _blit_src
        sta     ptr1
        lda     _blit_src+1
        sta     ptr1+1
        lda     _blit_dst
        sta     ptr2
        lda     _blit_dst+1
        sta     ptr2+1
        lda     _blit_and
        sta     ptr3
        lda     _blit_and+1
        sta     ptr3+1
        lda     _blit_or
        sta     tmp1
        jmp     do_blit

; ---------------------------------------------------------------------------
; blit_run - plage de cellules via les globales _run_*
; Retour (uchar dans A): nombre de cellules rendues.
; ---------------------------------------------------------------------------
_blit_run:
        lda     #0
        sta     tmp2            ; cellules consommees
        ldx     _run_count
        bne     @start
        jmp     @ret
@start:
        lda     _run_cells
        sta     ptr4
        lda     _run_cells+1
        sta     ptr4+1
        lda     _run_dst
        sta     sreg
        lda     _run_dst+1
        sta     sreg+1

@loop:
        ; --- Eligibilite fast-path ---
        ldy     #5              ; size
        lda     (ptr4),y
        beq     @szok
        jmp     @ret            ; taille non normale -> stop
@szok:
        ldy     #4              ; flags
        lda     (ptr4),y
        and     #$0B            ; FLASH|CONCEALED|UNDERLINE
        beq     @flok
        jmp     @ret
@flok:
        ; --- Masque OR (inversion video) ---
        ldy     #4
        lda     (ptr4),y
        and     #$04            ; ATTR_INVERT
        beq     @noinv
        lda     #$C0
        bne     @setor
@noinv: lda     #$40
@setor: sta     tmp1

        ; --- Jeu de caracteres ---
        ldy     #1
        lda     (ptr4),y
        beq     @g0
        cmp     #1
        beq     @g1
        jmp     @ret            ; G2 et autres -> stop

        ; --- G1 mosaique: glyphe depuis le cache ---
@g1:    ldy     #0
        lda     (ptr4),y
        and     #$7F
        cmp     #$60            ; cas special rangee haute pleine
        bne     @g1n
        lda     #<_g1_glyph_60
        sta     ptr1
        lda     #>_g1_glyph_60
        sta     ptr1+1
        jmp     @g1d
@g1n:   ; pattern = (ch & $1F) | ((ch & $40) >> 1), glyphe = cache + p*8
        pha
        and     #$1F
        sta     ptr1            ; temporaire
        pla
        and     #$40
        lsr     a
        ora     ptr1            ; A = pattern (0-63)
        ldy     #0
        sty     ptr1+1
        asl     a
        rol     ptr1+1
        asl     a
        rol     ptr1+1
        asl     a
        rol     ptr1+1          ; pattern*8 (16 bits)
        clc
        adc     #<_g1_cache
        sta     ptr1
        lda     ptr1+1
        adc     #>_g1_cache
        sta     ptr1+1
        ldy     #4              ; +512 si mosaique separee ($10)
        lda     (ptr4),y
        and     #$10
        beq     @g1d
        inc     ptr1+1
        inc     ptr1+1
@g1d:   ; G1: dither sauf mode 0
        lda     _run_mode
        beq     @annone
        jmp     @anfg

        ; --- G0 alphanumerique: glyphe = font_g0 + (ch-$20)*8 ---
@g0:    ldy     #0
        lda     (ptr4),y
        and     #$7F
        sec
        sbc     #$20
        bcs     @g0a
        lda     #0              ; < $20 -> espace
@g0a:   ldy     #0
        sty     ptr1+1
        asl     a
        rol     ptr1+1
        asl     a
        rol     ptr1+1
        asl     a
        rol     ptr1+1
        clc
        adc     #<_font_g0
        sta     ptr1
        lda     ptr1+1
        adc     #>_font_g0
        sta     ptr1+1
        ; G0: dither seulement en mode 2 (tout dither)
        lda     _run_mode
        cmp     #2
        beq     @anfg

        ; --- Table AND neutre ---
@annone:
        lda     #<_no_dither
        sta     ptr3
        lda     #>_no_dither
        sta     ptr3+1
        jmp     @blit

        ; --- Table AND = trame de la couleur d'encre ---
@anfg:  ldy     #2              ; fg
        lda     (ptr4),y
        and     #7
        asl     a
        asl     a
        asl     a
        clc
        adc     #<_g1_dither
        sta     ptr3
        lda     #0
        adc     #>_g1_dither
        sta     ptr3+1

        ; --- Blit (preserve X) ---
@blit:  lda     sreg
        sta     ptr2
        lda     sreg+1
        sta     ptr2+1
        jsr     do_blit

        ; --- Avancer: cellule += 6, dst += 1 ---
        clc
        lda     ptr4
        adc     #6
        sta     ptr4
        bcc     @na
        inc     ptr4+1
@na:    inc     sreg
        bne     @nb
        inc     sreg+1
@nb:    inc     tmp2
        dex
        beq     @ret
        jmp     @loop

@ret:   lda     tmp2
        ldx     #0
        rts
