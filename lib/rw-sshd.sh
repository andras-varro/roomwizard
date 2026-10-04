#!/bin/bash
#
# lib/rw-sshd.sh — the checks around an sshd_config change that keep it from
#                  locking the operator out.
#
# SOURCED, not executed:   . "$REPO_ROOT/lib/rw-sshd.sh"
#                          (the online half needs lib/rw-ssh.sh sourced first)
#
# ── What is NOT here ────────────────────────────────────────────────────────
#
# What sshd_config ends up CONTAINING. That is directive records in
# device-files/provision-rules.conf — groups `sshd` (crypto, limits; every mode),
# `sshd-password` and `sshd-key` (one per run, chosen by --ssh-auth) — applied by
# lib/rw-provision.sh's two executors, so commissioning/provision.sh and
# commissioning/commission-offline.sh write the same bytes. This file holds what a
# directive record cannot express: the preconditions and the undo.
#
#   rw_sshd_key_installed AUTHKEYS MARKER   offline key-only precondition
#   rw_sshd_check_offline CFG SSHD PLAN     offline stand-in for `sshd -t`
#   rw_sshd_guard_script                    device-side snapshot / sshd -t / undo
#   rw_sshd_key_login_ok TARGET             online key-only precondition
#   rw_sshd_await_key_login TARGET          the same, retried past the reload window
#   rw_sshd_offered_methods TARGET          what the server now offers
#   rw_sshd_commit_ssh TARGET MODE          online: sshd -t, reload, re-prove, confirm
#
# ── Never lock the operator out ─────────────────────────────────────────────
#
# There is no serial console (SYSTEM_ANALYSIS.md#312-serial-ports), so a unit whose
# sshd will not start, or that admits no key the operator holds, is recovered only
# by pulling the card. Every step below exists to make that unreachable:
#
#   1. key-only is refused unless a key is PROVEN (online: a BatchMode publickey
#      login; offline: authorized_keys written by this same run and holding a key
#      type the hardened config still accepts);
#   2. the previous file is snapshotted before the plan touches it;
#   3. `sshd -t` must pass, or the snapshot goes back;
#   4. online, sshd is reloaded under a dead-man timer and the key login is proven
#      again on a FRESH connection — if that confirmation never arrives, the device
#      restores the snapshot and reloads by itself.
#
# Offline there is no ARM sshd to run, so step 3 is rw_sshd_check_offline: every
# keyword and every algorithm name the plan writes must appear in the card's own
# /usr/sbin/sshd. A weaker witness than `sshd -t` — a substring match — and said so.

# The dead-man window. The guard script carries the same 90 as its `arm` default;
# it is not passed over ssh, so the two must be changed together.
RW_SSHD_CONFIRM_SECS=90

# Key types the hardened PubkeyAcceptedKeyTypes still accepts, as they appear in
# authorized_keys. ssh-rsa is the KEY type name and stays valid (it signs with
# rsa-sha2-*); ssh-dss is absent on purpose — a DSA key would be installed and then
# refused, which is exactly the lockout this exists to prevent.
RW_SSHD_KEYLINE_ERE='(^|[[:space:]])(ssh-ed25519|ssh-rsa|ecdsa-sha2-nistp(256|384|521)|sk-ssh-ed25519@openssh\.com|sk-ecdsa-sha2-nistp256@openssh\.com)[[:space:]]+AAAA[0-9A-Za-z+/]+=*'

# ---------------------------------------------------------------------------
# rw_sshd_key_installed AUTHKEYS MARKER
#
# 0 when AUTHKEYS exists, was written AFTER MARKER (i.e. by this run, not left on
# the card by somebody else), and holds at least one key line of an accepted type.
# ---------------------------------------------------------------------------
rw_sshd_key_installed() {
    local ak="$1" marker="$2"
    if [ ! -s "$ak" ]; then
        echo "  no authorized_keys at $ak" >&2; return 1
    fi
    if [ ! "$ak" -nt "$marker" ]; then
        echo "  $ak was not written by this run — a key left on the card is not proof the operator holds it" >&2
        return 1
    fi
    if ! grep -Eq "$RW_SSHD_KEYLINE_ERE" "$ak"; then
        echo "  $ak holds no key this config accepts (ed25519, ecdsa, rsa, or an sk- key; DSA is refused)" >&2
        return 1
    fi
    return 0
}

# ---------------------------------------------------------------------------
# rw_sshd_check_offline CFG SSHD_BINARY PLAN
#
# Echo every problem; return 1 if there were any. For each `directive` record in
# PLAN that targets /etc/ssh/sshd_config:
#
#   - CFG has exactly one uncommented line for the key, and it is "Key Value"
#     (sshd keywords are case-insensitive and the FIRST occurrence wins, so a
#     second spelling would silently override ours);
#   - the key, lowercased, appears in SSHD_BINARY — sshd's keyword table is
#     lowercase, and an unknown keyword is what makes `sshd -t` fail;
#   - for an algorithm list, every name appears in SSHD_BINARY — an unknown
#     algorithm name is the other way `sshd -t` fails.
#
# And CFG has no uncommented Match line, because a directive the executors APPEND
# would then land inside the Match block.
# ---------------------------------------------------------------------------
rw_sshd_check_offline() {
    local cfg="$1" bin="$2" plan="$3" bad=0 kind mode target src key val n lk name
    [ -f "$cfg" ]  || { echo "  no such config: $cfg"; return 1; }
    [ -s "$bin" ]  || { echo "  no sshd binary to check against: $bin"; return 1; }
    [ -f "$plan" ] || { echo "  no such plan: $plan"; return 1; }

    if grep -Eqi '^[[:space:]]*Match[[:space:]]' "$cfg"; then
        echo "  $cfg has an active Match block — appended directives would land inside it"
        bad=1
    fi

    while IFS=$'\t' read -r kind mode target src; do
        if [ "$kind" != directive ] || [ "$target" != /etc/ssh/sshd_config ]; then continue; fi
        key="${src%%=*}"; val="${src#*=}"
        lk=$(printf '%s' "$key" | tr '[:upper:]' '[:lower:]')
        n=$(grep -Eci "^[[:space:]]*${key}[[:space:]]" "$cfg" || true)
        if [ "$n" != 1 ]; then
            echo "  $key: $n uncommented line(s), want exactly 1"; bad=1
        elif ! grep -qxF "$key $val" "$cfg"; then
            echo "  $key: the line is not '$key $val'"; bad=1
        fi
        if ! grep -aqF "$lk" "$bin"; then
            echo "  $key: this sshd does not know the keyword"; bad=1
        fi
        case "$key" in
            Ciphers|MACs|KexAlgorithms|HostKeyAlgorithms|PubkeyAcceptedKeyTypes)
                for name in $(printf '%s' "$val" | tr ',' ' '); do
                    if ! grep -aqF -- "$name" "$bin"; then
                        echo "  $key: this sshd does not know the algorithm $name"; bad=1
                    fi
                done ;;
        esac
    done < "$plan"
    [ "$bad" = 0 ]
}

# ---------------------------------------------------------------------------
# rw_sshd_guard_script
#
# Emits the device-side half as POSIX sh for BusyBox ash, the same way
# rw_provision_online_script emits the executor — and it honours $RW_SSHD_ROOT and
# $RW_SSHD_BIN for the same reason: tests/rw_provision_test.sh runs it against a
# copied tree with a stub sshd.
#
#   snapshot        copy sshd_config to sshd_config.rw-prev
#   check           sshd -t; on failure restore the snapshot and exit 1
#   arm [SECS]      reload sshd, and restore + reload again after SECS unless
#                   `confirm` has run by then
#   confirm         disarm. Run over a FRESH ssh login, so it is itself the proof
#   restore         put the snapshot back now
#
# Restores write through `cat >`, so sshd_config keeps its inode and mode.
# ---------------------------------------------------------------------------
rw_sshd_guard_script() {
    cat <<'GUARD'
# rw-sshd guard. Generated by rw_sshd_guard_script; do not edit on the device.
R="${RW_SSHD_ROOT:-}"
SSHD="${RW_SSHD_BIN:-/usr/sbin/sshd}"
CFG="$R/etc/ssh/sshd_config"
PREV="$R/etc/ssh/sshd_config.rw-prev"
FLAG="$R/etc/ssh/.rw-sshd-unconfirmed"
INIT="$R/etc/init.d/sshd"

restore() {
    [ -f "$PREV" ] || { echo "  NO SNAPSHOT at $PREV — nothing to restore" >&2; return 1; }
    cat "$PREV" > "$CFG" && echo "  restored        $CFG from $PREV"
}

case "$1" in
snapshot)
    cp "$CFG" "$PREV" && echo "  snapshot        $PREV" ;;
check)
    if out=$("$SSHD" -t -f "$CFG" 2>&1); then
        echo "  sshd -t         ok"
    else
        echo "  sshd -t FAILED: $out" >&2
        restore
        exit 1
    fi ;;
arm)
    secs="${2:-90}"
    : > "$FLAG"
    ( sleep "$secs"
      if [ -f "$FLAG" ]; then
          rm -f "$FLAG"; restore; "$INIT" reload
      fi ) </dev/null >/dev/null 2>&1 &
    if "$INIT" reload >/dev/null 2>&1; then
        echo "  sshd reloaded; the previous config returns in ${secs} s unless confirmed"
    else
        rm -f "$FLAG"; restore; "$INIT" reload >/dev/null 2>&1
        echo "  sshd reload FAILED — previous config restored" >&2
        exit 1
    fi ;;
confirm)
    rm -f "$FLAG" && echo "  confirmed" ;;
restore)
    rm -f "$FLAG"; restore ;;
*)
    echo "usage: rw-sshd-guard snapshot|check|arm [secs]|confirm|restore" >&2; exit 2 ;;
esac
GUARD
}

# ---------------------------------------------------------------------------
# rw_sshd_key_login_ok TARGET
#
# A publickey-only BatchMode login. rw_ssh_probe already sets BatchMode, which
# rules out a password prompt; PreferredAuthentications rules out every other
# method, so success can only mean a key worked.
# ---------------------------------------------------------------------------
rw_sshd_key_login_ok() {
    rw_ssh_probe "$1" -o PreferredAuthentications=publickey >/dev/null
}

# ---------------------------------------------------------------------------
# rw_sshd_await_key_login TARGET
#
# rw_sshd_key_login_ok, retried while the answer is `down`, for up to
# RW_SSHD_SETTLE_SECS. The reload is SIGHUP, and sshd answers it by closing its
# listener and re-exec'ing itself — measured on the reference unit (a second sshd on
# port 2222, same candidate config): "Connection refused" at +0..35 ms after the
# HUP, listening again at ~0.76 s. A single probe straight after `arm` lands in that
# window, so a config a key login provably accepts was rolled back as a failure.
# Only `down` is retried: `auth` or `hostkey` is the new config's real answer.
# On failure the state, the attempt count and ssh's own stderr go to stderr.
# ---------------------------------------------------------------------------
RW_SSHD_SETTLE_SECS="${RW_SSHD_SETTLE_SECS:-30}"   # well inside the 90 s dead-man window
RW_SSHD_RETRY_SLEEP="${RW_SSHD_RETRY_SLEEP:-1}"
rw_sshd_await_key_login() {
    local target="$1" start=$SECONDS tries=0
    while :; do
        tries=$((tries + 1))
        rw_sshd_key_login_ok "$target" && return 0
        [ "$RW_SSH_LAST_STATE" = down ] || break
        [ $((SECONDS - start)) -lt "$RW_SSHD_SETTLE_SECS" ] || break
        sleep "$RW_SSHD_RETRY_SLEEP"
    done
    echo "  key login probe: '$RW_SSH_LAST_STATE' after $tries attempt(s) in $((SECONDS - start)) s" >&2
    printf '%s\n' "${RW_SSH_LAST_STDERR:-(ssh printed nothing)}" | sed 's/^/    ssh: /' >&2
    return 1
}

# ---------------------------------------------------------------------------
# rw_sshd_offered_methods TARGET
#
# The server's method list, read from the parenthetical in ssh's refusal when no
# key is offered: "publickey" in key-only mode, "publickey,password" with
# passwords on. ⚠️ This is the one place that list is the right thing to read —
# it IS the server's list. lib/rw-ssh.sh must never classify on it.
# ---------------------------------------------------------------------------
rw_sshd_offered_methods() {
    # Through rw_ssh_probe, the one BatchMode probe; its stderr global carries the
    # refusal. Not in a subshell, or that global is lost.
    rw_ssh_probe "$1" -o PubkeyAuthentication=no >/dev/null || true
    printf '%s\n' "${RW_SSH_LAST_STDERR:-}" \
        | sed -n 's/.*Permission denied (\([^)]*\)).*/\1/p' | head -1
}

# ---------------------------------------------------------------------------
# rw_sshd_commit_ssh TARGET MODE
#
# Run AFTER the plan has written sshd_config and AFTER `snapshot`, with the guard
# script already at /tmp/rw-sshd-guard.sh on the device. sshd -t, reload under the
# dead-man timer, prove the key login on a fresh connection, confirm.
# ---------------------------------------------------------------------------
rw_sshd_commit_ssh() {
    local target="$1" mode="$2" offered
    ssh "$target" sh /tmp/rw-sshd-guard.sh check || {
        echo "  sshd -t refused the new config; the previous one is back in place" >&2
        return 1; }
    ssh "$target" sh /tmp/rw-sshd-guard.sh arm || return 1   # the guard's default, 90 s
    if ! rw_sshd_await_key_login "$target"; then
        echo "  a fresh key login FAILED after the reload. The device restores the" >&2
        echo "  previous sshd_config by itself within $RW_SSHD_CONFIRM_SECS s." >&2
        return 1
    fi
    if ! ssh "$target" sh /tmp/rw-sshd-guard.sh confirm; then
        echo "  the key login worked but the confirm call failed. The device" >&2
        echo "  restores the previous sshd_config by itself within $RW_SSHD_CONFIRM_SECS s." >&2
        return 1
    fi
    offered=$(rw_sshd_offered_methods "$target")
    echo "  the server now offers: ${offered:-(could not read)}"
    case "$mode,$offered" in
        key,*password*|key,*keyboard-interactive*)
            echo "  key-only was written but a password method is still offered" >&2
            return 1 ;;
    esac
    ssh "$target" "rm -f /tmp/rw-sshd-guard.sh" || true
    return 0
}
