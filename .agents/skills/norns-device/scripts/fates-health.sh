#!/bin/bash
# Health check for the Fates norns device. Usage: fates-health.sh [host]
# Prints a sectioned report, ends with HEALTH: OK / HEALTH: PROBLEMS.
# Exit code 0 = all core checks passed.

HOST="${1:-fates}"

ssh -o BatchMode=yes -o ConnectTimeout=10 "$HOST" '
fail=0
section() { echo; echo "=== $1 ==="; }

section services
for u in norns.target norns-jack norns-sclang norns-main norns-maiden; do
  s=$(systemctl is-active $u); echo "$u: $s"
  [ "$s" = active ] || fail=1
done
[ "$(systemctl --failed --no-legend --plain | wc -l)" = 0 ] || { echo "FAILED UNITS PRESENT"; fail=1; }
echo "norns-main restarts: $(systemctl show norns-main -p NRestarts --value)"

section git
cd ~/norns && echo "$(git rev-parse --abbrev-ref HEAD) @ $(git rev-parse --short HEAD)"

section version-file
cat ~/version.txt

section jack
ports=$(timeout 5 jack_lsp 2>/dev/null | grep -c ":")
shm=$(ls /dev/shm 2>/dev/null | grep -c jack)
echo "ports visible to a new client: $ports"
echo "shm registry files: $shm"
if [ "$ports" -lt 1 ]; then echo "JACK WEDGED (new clients refused)"; fail=1; fi
if [ "$shm" -lt 1 ]; then echo "JACK WEDGED (deleted shm registry - existing audio keeps playing, next restart will die)"; fail=1; fi

section codec
amixer --device hw:sndrpiproto cget name="Master Playback Volume" 2>/dev/null | grep ": values"
amixer --device hw:sndrpiproto cget name="Input Mux" 2>/dev/null | grep ": values"

section midi
echo "ttymidi in ALSA: $(aconnect -l 2>/dev/null | grep -c ttymidi) (want 1)"

section thermal
(vcgencmd get_throttled 2>/dev/null || sudo vcgencmd get_throttled 2>/dev/null || echo "throttled: n/a")

section journal-errors-1h
n=$(journalctl -u norns-main --since "-1h" --no-pager 2>/dev/null | grep -ci "SEGV\|jack_client_open\|unable to connect\|start-limit")
echo "serious errors: $n (want 0)"
[ "$n" = 0 ] || fail=1

echo
if [ "$fail" = 0 ]; then echo "HEALTH: OK"; else echo "HEALTH: PROBLEMS - see sections above"; fi
exit $fail
'
