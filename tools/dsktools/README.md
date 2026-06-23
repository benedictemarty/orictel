# Outils de création de disquette Sedoric

Ces sources proviennent de l'**OSDK** (Oric SDK) de Fabrice Frances, librement
redistribuables. Ils servent à fabriquer une image disquette Sedoric 3
bootable (`.dsk`) à partir de la cassette `orictel.tap`.

| Fichier | Rôle |
|---------|------|
| `tap2dsk.c` | Construit une image disque Sedoric (ancien format Oric) à partir d'un ou plusieurs `.tap`. |
| `sedoric3.h` | Image binaire du système Sedoric 3 embarquée (pistes système du master). |
| `old2mfm.c` | Convertit l'ancien format `ORICDISK` vers le format moderne `MFM_DISK` lu par les émulateurs (Phosphoric, Oricutron, Euphoric). |

## Chaîne de conversion

```
orictel.tap --tap2dsk--> orictel.dsk (ORICDISK) --old2mfm--> orictel.dsk (MFM_DISK)
```

La cible `make dsk` compile ces outils (dans `build/`) puis enchaîne les deux
étapes automatiquement.
