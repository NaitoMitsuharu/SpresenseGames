#!/usr/bin/env bash
# One-time setup for Hikari Atelier, run inside Termux. Spresense Games copies
# this line to the clipboard and opens Termux; the user pastes it and presses Enter:
#
#   bash -c "$(curl -fsSL https://raw.githubusercontent.com/NaitoMitsuharu/SpresenseGames/master/Tools/OpenClaw/setup-lightbox.sh)"
#
# Safe to run again: every step checks first and only does what is missing.
set -euo pipefail
umask 077

REF="${LIGHTBOX_REF:-master}"
BASE="https://raw.githubusercontent.com/NaitoMitsuharu/SpresenseGames/$REF/Tools/OpenClaw"
DIR="$HOME/lightbox"
OPENCLAW_VERSION="${LIGHTBOX_OPENCLAW_VERSION:-2026.10.10}"
MODEL="${LIGHTBOX_MODEL:-github-copilot/gpt-6-luna}"
APP="com.lati.spresensegames/com.epicgames.unreal.SplashActivity"

step() { printf '\n[%s] %s\n' "$1" "$2"; }

# Never stop at a pager (":" prompt) or a package question while installing.
export DEBIAN_FRONTEND=noninteractive PAGER=cat APT_PAGER=cat LESS=FRX

step 1/6 "必要なツールを確認しています"
# A freshly installed Termux ships old libraries. Installing nodejs-lts alone then
# leaves a node that cannot start ("CANNOT LINK EXECUTABLE ... _ZNSt6__ndk1...").
# So when node is missing or broken, bring every package up to date first.
node_works() { command -v node >/dev/null && node --version >/dev/null 2>&1; }
if ! node_works || ! command -v curl >/dev/null; then
    printf 'Termuxのパッケージを最新にしています（初回は数分かかります）\n'
    pkg upgrade -y -o Dpkg::Options::=--force-confdef -o Dpkg::Options::=--force-confold
    pkg install -y nodejs-lts curl
    if ! node_works; then
        pkg reinstall -y nodejs-lts libc++ || true
    fi
    if ! node_works; then
        printf 'Node.jsを起動できません。Termuxで pkg upgrade を実行してから、この1行をもう一度実行してください。\n' >&2
        exit 1
    fi
fi
# npm 12 skips install scripts unless they are allowed. OpenClaw needs them
# (bundled plugins, esbuild), so allow exactly these packages. The marker also
# repairs an earlier install whose scripts were skipped.
mkdir -p "$DIR"
OPENCLAW_MARK="$DIR/.openclaw-$OPENCLAW_VERSION-installed"
if ! command -v openclaw >/dev/null || [[ ! -f "$OPENCLAW_MARK" ]]; then
    npm install -g --allow-scripts=@google/genai,esbuild,protobufjs,openclaw "openclaw@$OPENCLAW_VERSION"
    touch "$OPENCLAW_MARK"
fi
printf 'node %s / openclaw %s\n' "$(node --version)" "$(openclaw --version 2>/dev/null | head -n1)"

step 2/6 "ひかりのアトリエ用のファイルを取得しています"
mkdir -p "$DIR"
# LIGHTBOX_SOURCE_DIR: copy from a local folder instead (testing unreleased changes).
for file in start-lightbox.sh pair-lightbox.sh setup-lightbox.sh lightbox-token.cjs lightbox-boot.cjs lightbox-observer.cjs lightbox-pricing.json; do
    if [[ -n "${LIGHTBOX_SOURCE_DIR:-}" ]]; then
        cp "$LIGHTBOX_SOURCE_DIR/$file" "$DIR/$file.download"
    else
        curl -fsSL "$BASE/$file" -o "$DIR/$file.download"
    fi
    mv "$DIR/$file.download" "$DIR/$file"
done
chmod 700 "$DIR"/*.sh

step 3/6 "アプリからの起動を許可しています"
PROPS="$HOME/.termux/termux.properties"
mkdir -p "$HOME/.termux"
touch "$PROPS"
if grep -Eq '^[[:space:]]*allow-external-apps[[:space:]]*=' "$PROPS"; then
    sed -i -E 's/^[[:space:]]*allow-external-apps[[:space:]]*=.*/allow-external-apps = true/' "$PROPS"
else
    printf '\nallow-external-apps = true\n' >> "$PROPS"
fi
if command -v termux-reload-settings >/dev/null; then termux-reload-settings || true; fi

step 4/6 "Copilotへのログインを確認しています"
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

step 5/6 "演出サービスの初期設定をしています（数分かかることがあります）"
# Doing this here keeps the app's first Gateway start within its wait time.
LIGHTBOX_AUTO_PAIR=0 bash "$DIR/start-lightbox.sh" --configure-only

step 6/6 "完了しました"
printf 'Spresense Gamesに戻ります。アプリが演出サービスの起動と認証キーの設定を自動で行います。\n'
printf 'このTermuxは閉じずにそのままにしてください。\n'
am start -n "$APP" >/dev/null 2>&1 || printf 'Spresense Gamesを開いてください。\n'
