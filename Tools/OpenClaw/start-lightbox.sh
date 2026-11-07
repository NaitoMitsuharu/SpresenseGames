#!/usr/bin/env bash
# Run inside Termux (or its Linux environment), after Copilot login/model selection.
# Uses a separate profile; does not change the normal OpenClaw profile.
set -euo pipefail
umask 077
command -v node >/dev/null
command -v openclaw >/dev/null
# Sessions the app opens through Termux RUN_COMMAND can lack termux-exec, so the
# CLI's "#!/usr/bin/env node" line fails with "bad interpreter". Run it through node.
OPENCLAW_BIN="$(command -v openclaw)"
openclaw() { node "$OPENCLAW_BIN" "$@"; }
SCRIPT_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"

# The app's start button may be pressed while the Gateway already runs.
if node -e 'const s=require("node:net").connect(18789,"127.0.0.1");s.on("connect",()=>process.exit(0));s.on("error",()=>process.exit(1));setTimeout(()=>process.exit(1),1000)'; then
    printf '演出サービスはすでに起動しています（127.0.0.1:18789）。\n'
    if [[ "${LIGHTBOX_AUTO_PAIR:-1}" != "0" && -f "$SCRIPT_DIR/pair-lightbox.sh" ]]; then
        bash "$SCRIPT_DIR/pair-lightbox.sh" || true
    fi
    exit 0
fi

# validate checks token auth against this variable.
OPENCLAW_GATEWAY_TOKEN="$(node "$SCRIPT_DIR/lightbox-token.cjs")"
export OPENCLAW_GATEWAY_TOKEN
# Each openclaw call starts Node and takes seconds on a phone, and the app waits for
# the Gateway. Write the settings once; bump CONFIG_VERSION when they change.
CONFIG_VERSION=1
CONFIG_MARK="$SCRIPT_DIR/.gateway-config-v$CONFIG_VERSION"
if [[ ! -f "$CONFIG_MARK" ]]; then
    openclaw --profile lightbox config set gateway.mode local
    openclaw --profile lightbox config set gateway.bind loopback
    openclaw --profile lightbox config set gateway.port 18789
    openclaw --profile lightbox config set gateway.auth.mode token
    openclaw --profile lightbox config set gateway.http.endpoints.chatCompletions.enabled true
    # Scene JSON and optional screenshot review; filesystem/browser/shell tools are not required.
    openclaw --profile lightbox config set tools.deny '["*"]' --strict-json
    openclaw --profile lightbox config validate
    touch "$CONFIG_MARK"
fi
# setup-lightbox.sh configures ahead of time so the app's first start is quick.
if [[ "${1:-}" == "--configure-only" ]]; then
    exit 0
fi
printf '\nひかりのアトリエ → 設定\n接続先: http://127.0.0.1:18789\n認証キー: %s\n\n' "$OPENCLAW_GATEWAY_TOKEN"
printf 'この画面を閉じずに、ひかりのアトリエへ戻ってください。\n'
# Hand the key to the app; it is stored only when the app has none yet.
# Set LIGHTBOX_AUTO_PAIR=0 to skip (e.g. when the app is not installed).
if [[ "${LIGHTBOX_AUTO_PAIR:-1}" != "0" && -f "$SCRIPT_DIR/pair-lightbox.sh" ]]; then
    bash "$SCRIPT_DIR/pair-lightbox.sh" || true
fi

if command -v termux-wake-lock >/dev/null; then
    termux-wake-lock
fi
METER_PID=""
cleanup() {
    if [[ -n "$METER_PID" ]]; then kill "$METER_PID" >/dev/null 2>&1 || true; fi
    if command -v termux-wake-unlock >/dev/null; then termux-wake-unlock >/dev/null 2>&1 || true; fi
}
trap cleanup EXIT
if [[ -f "$SCRIPT_DIR/lightbox-observer.cjs" && -f "$SCRIPT_DIR/lightbox-pricing.json" ]]; then
    if ! node -e 'fetch("http://127.0.0.1:18791/v1/history",{headers:{Authorization:"Bearer "+process.env.OPENCLAW_GATEWAY_TOKEN},signal:AbortSignal.timeout(1500)}).then(r=>process.exit(r.ok?0:1)).catch(()=>process.exit(1))'; then
        node "$SCRIPT_DIR/lightbox-observer.cjs" &
        METER_PID=$!
    fi
fi
openclaw --profile lightbox gateway run --bind loopback --port 18789 --auth token
