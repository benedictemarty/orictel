; ===========================================================================
; display_asm.s - Blit cellule 6x8 optimise
;
; Trace une cellule (8 octets de glyphe) dans le framebuffer HIRES:
;   dst[ligne*40] = (src[ligne] AND and_tbl[ligne]) OR or_mask
; La table AND porte le dithering couleur (ou $3F = neutre), le masque
; OR porte bit6 (mode pixel) + bit7 (inversion video).
;
; ~190 cycles par cellule contre ~1500-2500 pour la boucle C cc65
; (arithmetique de pointeurs 16 bits et tests par ligne).
;
; Utilise ptr1/ptr2/ptr3/tmp1 (scratch zero page du runtime cc65,
; libres pendant un appel de fonction, IRQ ROM Oric ne les touche pas).
; ===========================================================================

        .export _blit_cell8
        .export _blit_src, _blit_dst, _blit_and, _blit_or
        .importzp ptr1, ptr2, ptr3, tmp1

        .segment "BSS"
_blit_src:  .res 2          ; glyphe source (8 octets)
_blit_dst:  .res 2          ; destination HIRES (1ere scanline)
_blit_and:  .res 2          ; table AND 8 octets (dither ou neutre)
_blit_or:   .res 1          ; masque OR ($40 ou $C0 inversion)

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
