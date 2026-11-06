#!/usr/bin/env bash
# One-time setup for Hikari Atelier, run inside Termux. Spresense Games copies
# this line to the clipboard and opens Termux; the user pastes it and presses Enter:
#
#   bash -c "$(curl -fsSL https://raw.githubusercontent.com/NaitoMitsuharu/spresesneMaze/master/Tools/OpenClaw/setup-lightbox.sh)"
#
# Safe to run again: every step checks first and only does what is missing.
set -euo pipefail
umask 077

REF="${LIGHTBOX_REF:-master}"
BASE="https://raw.githubusercontent.com/NaitoMitsuharu/spresesneMaze/$REF/Tools/OpenClaw"
DIR="$HOME/lightbox"
OPENCLAW_VERSION="${LIGHTBOX_OPENCLAW_VERSION:-2026.10.10}"
MODEL="${LIGHTBOX_MODEL:-github-copilot/gpt-6-luna}"
APP="com.lati.spresensegames/com.epicgames.unreal.SplashActivity"

step() { printf '\n[%s] %s\n' "$1" "$2"; }

step 1/5 "必要なツールを確認しています"
if ! command -v node >/dev/null || ! command -v curl >/dev/null; then
    pkg install -y nodejs-lts curl
fi
if ! command -v openclaw >/dev/null; then
    npm install -g "openclaw@$OPENCLAW_VERSION"
fi
printf 'node %s / openclaw %s\n' "$(node --version)" "$(openclaw --version 2>/dev/null | head -n1)"

step 2/5 "ひかりのアトリエ用のファイルを取得しています"
mkdir -p "$DIR"
# LIGHTBOX_SOURCE_DIR: copy from a local folder instead (testing unreleased changes).
for file in start-lightbox.sh pair-lightbox.sh setup-lightbox.sh lightbox-token.cjs lightbox-observer.cjs lightbox-pricing.json; do
    if [[ -n "${LIGHTBOX_SOURCE_DIR:-}" ]]; then
        cp "$LIGHTBOX_SOURCE_DIR/$file" "$DIR/$file.download"
    else
        curl -fsSL "$BASE/$file" -o "$DIR/$file.download"
    fi
    mv "$DIR/$file.download" "$DIR/$file"
done
chmod 700 "$DIR"/*.sh

step 3/5 "アプリからの起動を許可しています"
PROPS="$HOME/.termux/termux.properties"
mkdir -p "$HOME/.termux"
touch "$PROPS"
if grep -Eq '^[[:space:]]*allow-external-apps[[:space:]]*=' "$PROPS"; then
    sed -i -E 's/^[[:space:]]*allow-external-apps[[:space:]]*=.*/allow-external-apps = true/' "$PROPS"
else
    printf '\nallow-external-apps = true\n' >> "$PROPS"
fi
if command -v termux-reload-settings >/dev/null; then termux-reload-settings || true; fi

step 4/5 "Copilotへのログインを確認しています"
MARK="$DIR/.copilot-ready"
if [[ -f "$MARK" ]]; then
    printf 'ログイン済みです（やり直す場合は %s を削除して再実行）\n' "$MARK"
else
    printf 'ひかりのアトリエ専用のプロファイルでGitHub Copilotにログインします。\n'
    printf 'すでにこの端末でログインしている場合は n を押してください。\n'
    read -r -p 'ログインしますか？ [Y/n] ' answer
    if [[ ! "$answer" =~ ^[Nn] ]]; then
        openclaw --profile lightbox models auth login-github-copilot
        openclaw --profile lightbox models set "$MODEL" \
            || printf 'モデル %s を設定できませんでした。openclaw --profile lightbox models list で確認してください。\n' "$MODEL"
    fi
    touch "$MARK"
fi

step 5/5 "完了しました"
printf 'Spresense Gamesに戻ります。アプリが演出サービスの起動と認証キーの設定を自動で行います。\n'
printf 'このTermuxは閉じずにそのままにしてください。\n'
am start -n "$APP" >/dev/null 2>&1 || printf 'Spresense Gamesを開いてください。\n'
