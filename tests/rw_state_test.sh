#!/bin/bash
#
# rw_state_test.sh — regression for lib/rw-state.sh
#
# Host-only, no device, no root. Run it:
#
#   wsl.exe -e bash -lc "cd /mnt/c/work/roomwizard && ./tests/rw_state_test.sh"
#
# WHAT IT COVERS:
#
#   A  rw_state_image_deny over the REAL listing of a state.tar fetched from .188
#      (2026-10, before the image set existed) flags exactly the entries a
#      card image must not carry — VNC password, ScummVM ini and save, the two
#      wget files, the vendor /etc/hosts and the nine calibration .bak files.
#   B  Negative control: the same listing with those removed passes, exit 0.
#   C  Paths that would escape the extraction root (absolute, '..') are flagged by
#      rw_state_unsafe_paths and by the deny check; ordinary ones are not.
#   D  The hard list wins: a forbidden path ADDED to RW_STATE_IMAGE is still denied.
#      Fail closed: a file in no list, and p2's high scores, are denied.
#   E  The generated lister, run under dash against a synthetic tree, lists what
#      exists and nothing that does not; the IMAGE lister's output passes the deny
#      check (the two lists agree with each other), the BACKUP lister's does not.
#   F  rw_state_backup_foreign: what restore.sh may write is the BACKUP set only.
#   G  rw_state_ssh_keys_deny: a generic image (no state.tar) may carry no SSH key material.
#
# The count at the end includes a floor: a file that silently ran zero cases
# reports success just as loudly as one that ran all of them.

# shellcheck source-path=SCRIPTDIR
set -u

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
REPO_DIR="$(cd "$SCRIPT_DIR/.." && pwd)"

# shellcheck source=../lib/rw-state.sh
. "$REPO_DIR/lib/rw-state.sh"

RED='\033[0;31m'; GREEN='\033[0;32m'; NC='\033[0m'
PASS=0
FAIL=0
ok()  { PASS=$((PASS + 1)); echo -e "  ${GREEN}pass${NC}  $1"; }
bad() { FAIL=$((FAIL + 1)); echo -e "  ${RED}FAIL${NC}  $1"; }

TMP=$(mktemp -d)
trap 'rm -rf "$TMP"' EXIT INT TERM

# expect_out <expected-file> <actual-file> <description>
expect_out() {
    if diff <(sort "$1") <(sort "$2") > "$TMP/diff" 2>&1; then
        ok "$3"
    else
        bad "$3"; sed 's/^/        /' "$TMP/diff"
    fi
}
# expect_rc <want> <got> <description>
expect_rc() {
    if [ "$1" = "$2" ]; then ok "$3"; else bad "$3 (exit $2, expected $1)"; fi
}

# The real `tar -tf state.tar` of the .188 capture, in archive order.
cat > "$TMP/real.lst" <<'EOF'
etc/touch_calibration.conf
etc/touch_calibration.conf.bak1
etc/touch_calibration.conf.bak2
etc/touch_calibration.conf.bak3
etc/touch_calibration.conf.bak4
etc/touch_calibration.conf.bak5
etc/touch_calibration.conf.bak6
etc/touch_calibration.conf.bak7
etc/touch_calibration.conf.bak8
etc/touch_calibration.conf.bak9
opt/games/rw_config.conf
opt/games/scummvm.ini
opt/vnc_client/vnc_client.conf
var/lib/alsa/asound.state
var/lib/bluetooth/
var/lib/bluetooth/A0:AD:9F:70:DD:CA/
var/lib/bluetooth/A0:AD:9F:70:DD:CA/cache/
var/lib/bluetooth/A0:AD:9F:70:DD:CA/cache/5C:D3:3D:83:A3:B6
var/lib/bluetooth/A0:AD:9F:70:DD:CA/cache/94:45:60:89:F6:48
var/lib/bluetooth/A0:AD:9F:70:DD:CA/cache/E4:17:D8:13:49:41
var/lib/bluetooth/A0:AD:9F:70:DD:CA/cache/08:C8:C2:3C:67:17
var/lib/bluetooth/A0:AD:9F:70:DD:CA/cache/E4:17:D8:42:BD:FF
var/lib/bluetooth/A0:AD:9F:70:DD:CA/cache/08:EB:ED:FE:C1:3D
var/lib/bluetooth/A0:AD:9F:70:DD:CA/cache/90:7A:58:B9:C6:F5
var/lib/bluetooth/A0:AD:9F:70:DD:CA/cache/AC:0B:FB:14:87:62
var/lib/bluetooth/A0:AD:9F:70:DD:CA/cache/E6:7A:00:00:20:9F
var/lib/bluetooth/A0:AD:9F:70:DD:CA/cache/E4:17:D8:40:EB:AE
var/lib/bluetooth/A0:AD:9F:70:DD:CA/cache/34:88:5D:55:62:C5
var/lib/bluetooth/A0:AD:9F:70:DD:CA/cache/00:0C:BF:40:02:9B
var/lib/bluetooth/A0:AD:9F:70:DD:CA/08:EB:ED:FE:C1:3D/
var/lib/bluetooth/A0:AD:9F:70:DD:CA/08:EB:ED:FE:C1:3D/info
var/lib/bluetooth/A0:AD:9F:70:DD:CA/08:EB:ED:FE:C1:3D/attributes
var/lib/bluetooth/A0:AD:9F:70:DD:CA/90:7A:58:B9:C6:F5/
var/lib/bluetooth/A0:AD:9F:70:DD:CA/90:7A:58:B9:C6:F5/info
var/lib/bluetooth/A0:AD:9F:70:DD:CA/90:7A:58:B9:C6:F5/attributes
var/lib/bluetooth/A0:AD:9F:70:DD:CA/E6:7A:00:00:20:9F/
var/lib/bluetooth/A0:AD:9F:70:DD:CA/E6:7A:00:00:20:9F/info
var/lib/bluetooth/A0:AD:9F:70:DD:CA/E6:7A:00:00:20:9F/attributes
var/lib/bluetooth/A0:AD:9F:70:DD:CA/E4:17:D8:40:EB:AE/
var/lib/bluetooth/A0:AD:9F:70:DD:CA/E4:17:D8:40:EB:AE/info
var/lib/bluetooth/A0:AD:9F:70:DD:CA/E4:17:D8:40:EB:AE/attributes
var/lib/bluetooth/A0:AD:9F:70:DD:CA/34:88:5D:55:62:C5/
var/lib/bluetooth/A0:AD:9F:70:DD:CA/34:88:5D:55:62:C5/info
var/lib/bluetooth/A0:AD:9F:70:DD:CA/34:88:5D:55:62:C5/attributes
var/lib/bluetooth/A0:AD:9F:70:DD:CA/settings
var/lib/bluealsa/
var/lib/bluealsa/08:C8:C2:3C:67:17
var/lib/bluealsa/08:EB:ED:FE:C1:3D
var/lib/bluealsa/90:7A:58:B9:C6:F5
etc/hostname
etc/hosts
etc/timezone
etc/localtime
home/root/.ssh/
home/root/.ssh/authorized_keys
opt/roomwizard/default-app
home/root/kq2.000
home/root/wget.err
home/root/wget.out
EOF

cat > "$TMP/forbidden.lst" <<'EOF'
etc/touch_calibration.conf.bak1
etc/touch_calibration.conf.bak2
etc/touch_calibration.conf.bak3
etc/touch_calibration.conf.bak4
etc/touch_calibration.conf.bak5
etc/touch_calibration.conf.bak6
etc/touch_calibration.conf.bak7
etc/touch_calibration.conf.bak8
etc/touch_calibration.conf.bak9
opt/games/scummvm.ini
opt/vnc_client/vnc_client.conf
etc/hosts
home/root/kq2.000
home/root/wget.err
home/root/wget.out
EOF

echo ""
echo "A  the real .188 listing"
rw_state_image_deny < "$TMP/real.lst" > "$TMP/a.out"; rc=$?
expect_out "$TMP/forbidden.lst" "$TMP/a.out" "A1 flags exactly the forbidden entries"
expect_rc 1 "$rc" "A2 exits 1 when it flags anything"
# The same listing in tar's "./"-prefixed shape (tar -C / -cf - .) must flag the same.
sed 's|^|./|' "$TMP/real.lst" | rw_state_image_deny | sed 's|^\./||' > "$TMP/a3.out"
expect_out "$TMP/forbidden.lst" "$TMP/a3.out" "A3 same verdict with ./-prefixed entries"

echo ""
echo "B  negative control: a clean listing"
grep -vxF -f "$TMP/forbidden.lst" "$TMP/real.lst" > "$TMP/clean.lst"
rw_state_image_deny < "$TMP/clean.lst" > "$TMP/b.out"; rc=$?
: > "$TMP/empty"
expect_out "$TMP/empty" "$TMP/b.out" "B1 the clean listing prints nothing"
expect_rc 0 "$rc" "B2 and exits 0"
if [ "$(wc -l < "$TMP/clean.lst")" -gt 40 ]; then ok "B3 the clean fixture is not trivially small"; else bad "B3 clean fixture has $(wc -l < "$TMP/clean.lst") lines"; fi

echo ""
echo "C  paths that escape the root"
printf '%s\n' /etc/passwd ../etc/x etc/../../x home/root/.. > "$TMP/unsafe.lst"
printf '%s\n' etc/hostname ./etc/timezone 'var/lib/bluetooth/..x' > "$TMP/safe.lst"
rw_state_unsafe_paths < "$TMP/unsafe.lst" > "$TMP/c1.out"; rc=$?
expect_out "$TMP/unsafe.lst" "$TMP/c1.out" "C1 rw_state_unsafe_paths flags all four"
expect_rc 1 "$rc" "C2 and exits 1"
rw_state_unsafe_paths < "$TMP/safe.lst" > "$TMP/c3.out"; rc=$?
expect_out "$TMP/empty" "$TMP/c3.out" "C3 ordinary paths, including a name starting '..', pass"
expect_rc 0 "$rc" "C4 and exit 0"
printf '%s\n' /etc/hostname ../etc/hostname > "$TMP/c5.lst"
rw_state_image_deny < "$TMP/c5.lst" > "$TMP/c5.out"
expect_out "$TMP/c5.lst" "$TMP/c5.out" "C5 the deny check refuses an allowed name made absolute or ../"

echo ""
echo "D  the hard list, and failing closed"
out=$( RW_STATE_IMAGE+=(opt/vnc_client opt/games/scummvm.ini etc/hosts)
       printf '%s\n' opt/vnc_client/vnc_client.conf opt/games/scummvm.ini etc/hosts | rw_state_image_deny )
want=$(printf '%s\n' opt/vnc_client/vnc_client.conf opt/games/scummvm.ini etc/hosts)
if [ "$out" = "$want" ]; then ok "D1 a forbidden path added to RW_STATE_IMAGE is still denied"; else bad "D1 got: $out"; fi
printf '%s\n' usr/bin/evil home/root/data/snake.hig home/root/data/ssh/ssh_host_rsa_key \
    'opt/games/scumm/Full Throttle/ft.la0' home/root/.sshx > "$TMP/d2.lst"
rw_state_image_deny < "$TMP/d2.lst" > "$TMP/d2.out"
expect_out "$TMP/d2.lst" "$TMP/d2.out" "D2 unlisted files, p2 contents, game data and a .ssh lookalike are denied"
printf '%s\n' ./ var/ var/lib/ home/ home/root/ > "$TMP/d3.lst"
rw_state_image_deny < "$TMP/d3.lst" > "$TMP/d3.out"
expect_out "$TMP/empty" "$TMP/d3.out" "D3 ancestor directory entries of allowed paths pass"
printf '%s\n' var lib/ > "$TMP/d4.lst"
rw_state_image_deny < "$TMP/d4.lst" > "$TMP/d4.out"
expect_out "$TMP/d4.lst" "$TMP/d4.out" "D4 an ancestor name as a FILE, and an unrelated dir, are denied"

echo ""
echo "E  the generated lister against a synthetic tree"
R="$TMP/root"
mkdir -p "$R/etc" "$R/opt/games/scumm" "$R/opt/vnc_client" "$R/opt/roomwizard" "$R/var/lib/bluetooth/AA" \
    "$R/home/root/.ssh" "$R/home/root/data/ssh" "$R/home/root/subdir"
for f in etc/hostname etc/hosts etc/timezone etc/touch_calibration.conf etc/touch_calibration.conf.bak1 \
         etc/input_config.conf opt/games/rw_config.conf opt/games/scummvm.ini opt/games/scumm/x.la0 \
         opt/vnc_client/vnc_client.conf opt/roomwizard/default-app var/lib/bluetooth/AA/info \
         home/root/.ssh/authorized_keys home/root/data/snake.hig home/root/data/ssh/ssh_host_rsa_key \
         home/root/kq2.000 home/root/uImage-test home/root/subdir/x ft.s01; do
    : > "$R/$f"
done
ln -s /usr/share/zoneinfo/UTC "$R/etc/localtime"
ln -s /nowhere "$R/home/root/dangling"
SH=$(command -v dash || command -v sh)
for set in image backup; do
    rw_state_lister "$set" > "$TMP/lister.$set"
    if $SH -n "$TMP/lister.$set"; then ok "E0 $set lister parses under $SH"; else bad "E0 $set lister does not parse under $SH"; fi
    # Re-root it: the shipped script's first line is `cd / || exit 1`.
    sed "1s|^cd / |cd \"$R\" |" "$TMP/lister.$set" > "$TMP/lister.$set.rooted"
    if ! cmp -s "$TMP/lister.$set" "$TMP/lister.$set.rooted"; then
        ok "E0 $set lister re-rooted (its first line is the cd)"
    else
        bad "E0 $set lister: first line is not 'cd / ...', cannot re-root"
    fi
    $SH "$TMP/lister.$set.rooted" > "$TMP/listed.$set"
done
cat > "$TMP/want.image" <<'EOF'
etc/hostname
etc/timezone
etc/localtime
etc/touch_calibration.conf
etc/input_config.conf
opt/games/rw_config.conf
opt/roomwizard/default-app
var/lib/bluetooth
home/root/.ssh
EOF
cat "$TMP/want.image" - > "$TMP/want.backup" <<'EOF'
etc/hosts
etc/touch_calibration.conf.bak1
opt/vnc_client/vnc_client.conf
opt/games/scummvm.ini
home/root/data/snake.hig
home/root/data/ssh
home/root/kq2.000
ft.s01
EOF
expect_out "$TMP/want.image" "$TMP/listed.image" "E1 image lister: what exists, a symlink as itself, no missing path"
expect_out "$TMP/want.backup" "$TMP/listed.backup" "E2 backup lister adds the private files, p2, and loose saves (not uImage-*, subdirs, dangling links)"
tar -C "$R" -cf - -T "$TMP/listed.image" | tar -tf - | rw_state_image_deny > "$TMP/e3.out"; rc=$?
expect_out "$TMP/empty" "$TMP/e3.out" "E3 a tar of the image set passes the deny check"
expect_rc 0 "$rc" "E4 and exits 0"
tar -C "$R" -cf - -T "$TMP/listed.backup" | tar -tf - | rw_state_image_deny > "$TMP/e5.out"
grep -vxF -f "$TMP/want.image" "$TMP/want.backup" | sed 's|^home/root/data/ssh$|home/root/data/ssh/|' > "$TMP/e5.want"
echo home/root/data/ssh/ssh_host_rsa_key >> "$TMP/e5.want"
expect_out "$TMP/e5.want" "$TMP/e5.out" "E5 a tar of the backup set is refused on exactly its backup-only entries"

echo ""
echo "F  what restore.sh will write: the BACKUP set and nothing else"
tar -C "$R" -cf - -T "$TMP/listed.backup" | tar -tf - > "$TMP/f1.lst"
rw_state_backup_foreign < "$TMP/f1.lst" > "$TMP/f1.out"; rc=$?
expect_out "$TMP/empty" "$TMP/f1.out" "F1 a tar of the backup set is all members"
expect_rc 0 "$rc" "F2 and exits 0"
rw_state_backup_foreign < "$TMP/real.lst" > "$TMP/f3.out"
expect_out "$TMP/empty" "$TMP/f3.out" "F3 the real .188 listing is all backup members"
printf '%s\n' etc/shadow etc/ssh/sshd_config usr/bin/x home/root/subdir/x home/root/subdir/ \
    /etc/hostname ../x opt/games/snake > "$TMP/f4.lst"
rw_state_backup_foreign < "$TMP/f4.lst" > "$TMP/f4.out"; rc=$?
expect_out "$TMP/f4.lst" "$TMP/f4.out" "F4 system files, a nested /home/root file, unsafe paths and a binary are foreign"
expect_rc 1 "$rc" "F5 and exits 1"

echo ""
echo "G  rw_state_ssh_keys_deny: a generic image carries no SSH key material"
printf '%s\n' ./ ./etc/ssh/sshd_config ./home/root/.ssh/ ./home/root/.ssh/authorized_keys ./usr/bin/ssh > "$TMP/g1.lst"
rw_state_ssh_keys_deny < "$TMP/g1.lst" > "$TMP/g1.out"; rc=$?
printf '%s\n' ./home/root/.ssh/authorized_keys > "$TMP/g1.want"
expect_out "$TMP/g1.want" "$TMP/g1.out" "G1 an authorized_keys under home/root/.ssh is refused, the .ssh directory entry and sshd_config are not"
expect_rc 1 "$rc" "G2 and exits 1"
printf '%s\n' home/root/.ssh/id_ed25519 home/root/.ssh/known_hosts home/pi/.ssh/authorized_keys2 \
    etc/skel/authorized_keys > "$TMP/g3.lst"
rw_state_ssh_keys_deny < "$TMP/g3.lst" > "$TMP/g3.out"
expect_out "$TMP/g3.lst" "$TMP/g3.out" "G3 any file inside a .ssh directory, in any home, and any authorized_keys* is refused"
printf '%s\n' home/root/.ssh home/root/.ssh/id_rsa home/root/.ssh/id_ecdsa home/root/.ssh/id_ed25519 \
    home/root/.ssh/id_dsa etc/ssh/ssh_host_rsa_key etc/ssh/ssh_host_ed25519_key > "$TMP/g3b.lst"
rw_state_ssh_keys_deny < "$TMP/g3b.lst" > "$TMP/g3b.out"; rc=$?
expect_out "$TMP/g3b.lst" "$TMP/g3b.out" "G3b SSH host private keys (ssh_host_*_key) and user keys (id_*) anywhere are refused"
expect_rc 1 "$rc" "G3c and exits 1"
printf '%s\n' ./ ./etc/ ./etc/ssh/ ./etc/ssh/sshd_config ./etc/ssh/ssh_host_rsa_key.pub ./home/root/ ./home/root/.ssh/ \
    ./home/root/.sshrc.d/x ./usr/share/doc/ssh-keys.txt ./etc/ssh/ssh_host_ed25519_key.pub > "$TMP/g4.lst"
rw_state_ssh_keys_deny < "$TMP/g4.lst" > "$TMP/g4.out"; rc=$?
expect_out "$TMP/empty" "$TMP/g4.out" "G4 control: a rootfs listing with sshd_config, .ssh directory, .pub files and look-alike names passes"
expect_rc 0 "$rc" "G5 and exits 0"
printf '%s\n' ./etc/ssh/sshd_config ./etc/ssh/ssh_host_ed25519_key.pub home/root/.ssh/ > "$TMP/g6.lst"
rw_state_ssh_keys_deny < "$TMP/g6.lst" > "$TMP/g6.out"; rc=$?
expect_out "$TMP/empty" "$TMP/g6.out" "G6 a legitimate sshd_config, public key, and empty .ssh directory pass"
expect_rc 0 "$rc" "G7 and exits 0"

echo ""
TOTAL=$((PASS + FAIL))
echo "  $PASS passed, $FAIL failed"
# groups A..G: 3 + 3 + 5 + 4 + 9 + 5 + 7 = 36 (group E has four zero-numbered cases, group G now has 7)
MIN_CASES=36
if [ "$TOTAL" -lt "$MIN_CASES" ]; then
    echo -e "  ${RED}HARNESS ERROR${NC}: only $TOTAL cases ran, expected at least $MIN_CASES."
    exit 2
fi
[ "$FAIL" -eq 0 ] || exit 1
echo -e "  ${GREEN}all good${NC}"
