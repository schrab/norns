#!/bin/bash
# Recover the Fates from a jackd wedge (either variant: orphaned registry or
# deleted-shm-while-playing). WARNING: audio stops for ~30 seconds.
# Afterwards run fates-health.sh to confirm.

HOST="${1:-fates}"

ssh -o BatchMode=yes -o ConnectTimeout=10 "$HOST" '
sudo systemctl stop norns.target; sleep 2
sudo systemctl stop norns-jack 2>/dev/null; sleep 2
sudo systemctl restart norns-jack; sleep 6
sudo systemctl reset-failed norns-main
sudo systemctl start norns.target; sleep 15
echo "services: $(systemctl is-active norns-jack) $(systemctl is-active norns-main)"
ports=$(timeout 5 jack_lsp 2>/dev/null | grep -c ":")
echo "jack ports after recovery: $ports (want > 0)"
'
