#!/usr/bin/env bash
# Run inside Termux. Hands the saved Gateway key to Spresense Games so the app
# configures itself when no key is stored yet. A key already saved in the app
# is never replaced (change it from ☰ → 設定 instead).
#
# The key travels only inside an explicit intent to the app's own package; it
# is not written to shared storage or printed.
set -euo pipefail
umask 077
SCRIPT_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
APP_PACKAGE="${LIGHTBOX_APP_PACKAGE:-com.lati.spresensegames}"
APP_ACTIVITY="${LIGHTBOX_APP_ACTIVITY:-com.epicgames.unreal.SplashActivity}"
ENDPOINT="${LIGHTBOX_ENDPOINT:-http://127.0.0.1:18789}"

if ! command -v am >/dev/null; then
    printf 'am コマンドが見つからないため、自動設定をスキップしました。\n' >&2
    exit 1
fi
TOKEN="${OPENCLAW_GATEWAY_TOKEN:-$(node "$SCRIPT_DIR/lightbox-token.cjs")}"

# Explicit component: the launcher filter has no DEFAULT category, so an
# implicit MAIN/LAUNCHER intent does not resolve through `am start`.
if am start -n "$APP_PACKAGE/$APP_ACTIVITY" \
        --es com.lati.lightbox.GATEWAY_ENDPOINT "$ENDPOINT" \
        --es com.lati.lightbox.GATEWAY_TOKEN "$TOKEN" >/dev/null 2>&1; then
    printf 'Spresense Gamesへ認証キーを渡しました（未設定の場合だけ保存されます）。\n'
else
    printf 'Spresense Gamesを起動できませんでした。アプリがインストールされているか確認してください。\n' >&2
    exit 1
fi
