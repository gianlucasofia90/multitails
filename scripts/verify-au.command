#!/bin/bash
# Validazione Apple dell'Audio Unit installato. Risultato in auval_log.txt nella cartella del progetto.
cd "$(dirname "$0")/.."
exec > >(tee auval_log.txt) 2>&1
killall -9 AudioComponentRegistrar >/dev/null 2>&1; sleep 2
echo "=== MULTITAILS (aumf Mtls Gsof)"; auval -v aumf Mtls Gsof | tail -25
echo "=== fine"
