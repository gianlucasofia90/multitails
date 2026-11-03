# Changelog

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
