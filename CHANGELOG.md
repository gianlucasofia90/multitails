# Changelog

## [0.1.3] - 2026-11-06
- macOS: il plugin ora funziona anche sui Mac Intel con macOS 10.13 High Sierra o successivo, compreso Catalina. Prima richiedeva macOS 11: in Ableton Live il VST3 non compariva e l'AU compariva ma non si apriva. Sui Mac Apple Silicon non cambia nulla.

## [0.1.2] - 2026-11-03
- Interfaccia molto più fluida: lo sfondo e i controlli fermi vengono conservati in immagine invece di essere ridisegnati a ogni fotogramma. Nelle misure il tempo di disegno scende da 100-180 ms a meno di 10 ms per fotogramma.
- Convoluzione senza picchi di CPU: la coda dell'IR viene calcolata a piccole dosi invece che tutta insieme ogni 1024 campioni. Con due IR da 9,4 s e buffer da 128 campioni il blocco tipico più lento (99° percentile) scende di circa cinque volte; il carico medio cresce di circa il 10 %.
- Decay e Damp mandati dal sequencer raggiungono la coda circa 21 ms più tardi. Con i valori fermi il suono non cambia, salvo differenze sotto -45 dB con Damp attivo.

## [0.1.1] - 2026-11-03
- macOS: nuovo installer `.pkg` con VST3 e AU. Basta approvare il pacchetto una volta, e i plugin installati non restano bloccati da Gatekeeper.
- macOS: corretta la versione minima del sistema: le build ora partono da macOS 11, come previsto.
- La CI verifica firma ad hoc, versione minima e installer a ogni build.

## [0.1.0] - 2026-11-03
Prima versione.
- Split transiente/tonale in tempo e frequenza, con Focus, Smoothing, Trans Decay e Source. La somma dei due flussi è esattamente l'ingresso.
- Due catene indipendenti di sei effetti in qualsiasi ordine, ognuno attivabile, con le code che continuano dopo lo spegnimento (`TAILS`).
- Convoluzione con 17 risposte di fabbrica (plate, digitali, stanze) e caricamento di WAV, AIFF, FLAC e OGG. Decay e Damp in tempo reale.
- Tape echo a quattro testine (riferimento Binson Echorec), con modi Echo, Repeat e Swell.
- Delay fino a 3,5 s (riferimento H-Delay), con Ping Pong, Analog 1-4, LoFi, filtri e modulazione.
- Chorus BBD (riferimento bx_bluechorus2 / Boss CE-2), con modi Mono, Stereo e Wide.
- Rotary: horn e drum separati con inerzia propria, Doppler e ampiezza, due microfoni.
- Filtri HPF e LPF a 12 o 24 dB e BPF.
- Sequencer a 8 corsie fino a 32 passi, con destinazioni su quasi tutti i parametri, Sync DAW o FREE.
- MIDI map a 64 slot: CC, note, pitch bend e pressure, con MIDI learn e modi Absolute, Toggle, Momentary, Relative.
- 15 preset di fabbrica e preset utente.
- Formati: VST3 (macOS, Windows, Linux), AU, AAX (non firmato). VST2 solo con SDK Steinberg proprio.
- Grafica: fondo arancione volpe con fascia azzurra e scacchiera chiara, due code dalla punta bianca dietro i pannelli, tag del preset blu. Al posto delle strisce gialle e nere, una banda sfumata dall'arancione al bianco della punta della coda.
- Catene come schema di routing: griglia scura con un filo per flusso e i blocchi infilati in ordine, blocchi spenti tratteggiati, e accanto al nome del flusso una striscia compatta con l'ordine (C T D H R F). Clic, punto di accensione e trascinamento funzionano come prima.
