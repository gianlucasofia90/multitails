# Risposte all'impulso di fabbrica

File FLAC 48 kHz, 24 bit, stereo. Rigenerabili con `tools/make_irs.py`. L'ordine dei numeri è salvato nelle sessioni: i file nuovi vanno in coda.

| N. | File | Origine | Licenza |
|---|---|---|---|
| 00-03 | Plate Short, Medium, Long, Bright | Simulazione modale scritta per questo progetto | come il resto di MULTITAILS |
| 04-08 | Digital Hall, Room, Long, Gate, Reverse | Rete di ritardi scritta per questo progetto | come il resto di MULTITAILS |
| 09-16 | Tile Room, Living Room, Studio, Small Hall, Large Hall, Huge Hall, Church, Cathedral | Database di HybridReverb2, Institute of Communication Acoustics, Ruhr-Universität Bochum | CC BY-SA 4.0 |

## Plate
Lastra di acciaio di 2 × 1 m e 0,5 mm, il formato dell'EMT 140: circa 19 500 modi di una piastra di Kirchhoff appoggiata, fino a 15 kHz, un eccitatore e due pickup fuori centro. Il decadimento segue il comportamento documentato della lastra: i bassi durano di più, a 10 kHz si resta sotto il secondo. Le risposte sono calcolate, non misurate da una lastra reale.

`Plate Bright` smorza meno gli acuti: non è un comportamento dell'EMT 140, è una scelta musicale.

## Digital
Non sono catture di nessuna macchina. Una rete a 8 linee gira a 25-31 kHz con aritmetica a 12-14 bit, nello spirito dei riverberi rack di fine anni '80.

## Rooms
Fonte: [HybridReverb2 impulse response database](https://github.com/jpcima/HybridReverb2-impulse-response-database). Autore: Christian Borß. © 2009 Institute of Communication Acoustics, Ruhr-Universität Bochum, Germania. Licenza: [CC BY-SA 4.0](https://creativecommons.org/licenses/by-sa/4.0/).

Attenzione: non sono registrazioni di stanze vere. Sono state generate con il simulatore acustico tinyAVE.

Modifiche fatte qui: scelti 8 ambienti su 7 stanze, presi i canali `left_fl` e `right_fr`, tagliata la coda sotto -85 dB e oltre 10 s, normalizzata l'energia. Questi 8 file modificati restano sotto CC BY-SA 4.0.
