#!/bin/bash
# Copy the Tulip/AMYboard Python that amyboard.py needs from a tulipcc checkout
# into py/tulipcc/ (frozen into the firmware unchanged), and record the commit.
#   TULIPCC=~/github/shorepine/tulipcc REF=origin/main scripts/sync_tulipcc_py.sh
set -e
TULIPCC=${TULIPCC:-$HOME/github/shorepine/tulipcc}
REF=${REF:-origin/main}
HERE="$(cd "$(dirname "$0")/.." && pwd)"
DEST="$HERE/py/tulipcc"
FILES="
tulip/shared/py/tulip.py
tulip/shared/py/midi.py
tulip/shared/py/synth.py
tulip/shared/py/sequencer.py
tulip/shared/py/arpegg.py
tulip/shared/py/patches.py
tulip/shared/py/tulip_queue.py
tulip/shared/py/upysh.py
tulip/shared/py/sh1107.py
tulip/shared/py/ssd1327.py
tulip/shared/py/ads1115.py
tulip/shared/amyboard-py/amyboard.py
"
mkdir -p "$DEST"
for f in $FILES; do
    git -C "$TULIPCC" show "$REF:$f" > "$DEST/$(basename "$f")"
done
git -C "$TULIPCC" show "$REF:LICENSE" > "$DEST/LICENSE"  # MIT: the notice travels with the copies
COMMIT=$(git -C "$TULIPCC" rev-parse "$REF")
echo "$COMMIT" > "$DEST/TULIPCC_COMMIT"
echo "synced $(echo $FILES | wc -w | tr -d ' ') files from tulipcc $REF ($COMMIT)"
