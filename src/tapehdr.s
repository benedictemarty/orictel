;
; OricTel - En-tete cassette Oric avec nom de programme fixe.
;
; Remplace l'en-tete par defaut de cc65 (libsrc/atmos/tapehdr.s) qui nomme
; le programme avec le timestamp de compilation (.sprintf("%u", .time)).
; Comme les objets du projet sont lies AVANT atmos.lib, ce module resout
; __TAPEHDR__ en premier et le module de la bibliotheque est ignore.
;
; Le nom "ORICTEL" est repris tel quel sur la cassette (.tap) et, apres
; conversion, comme nom de fichier sur la disquette Sedoric (.dsk).
;

        ; Symbole force par le fichier de configuration du linker.
        .export __TAPEHDR__:abs = 1

        ; Symboles fournis par la configuration / le startup.
        .import __AUTORUN__, __PROGFLAG__
        .import __BASHEAD_START__, __MAIN_LAST__

; ------------------------------------------------------------------------
; En-tete cassette Oric

.segment        "TAPEHDR"

        .byte   $16, $16, $16           ; Octets de synchro
        .byte   $24                     ; Marqueur de debut d'en-tete

        .byte   $00                     ; $2B0
        .byte   $00                     ; $2AF
        .byte   <__PROGFLAG__           ; $2AE Type ($00=BASIC, $80=machine)
        .byte   <__AUTORUN__            ; $2AD Auto-run ($C7=run, $00=load seul)
        .dbyt   __MAIN_LAST__ - 1       ; $2AB Adresse de fin de fichier
        .dbyt   __BASHEAD_START__       ; $2A9 Adresse de debut de fichier
        .byte   $00                     ; $2A8

        ; Nom du fichier (17 caracteres max), termine par zero.
        .asciiz "ORICTEL"
