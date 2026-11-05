# ひかりの箱

SpresenseMazeのトップ画面から選べる3つ目の体験です。同じAPKに含まれます。
箱のSpresenseとXperia 1 VIIをUSB接続し、Xperia 1 VIIの画面で光の粒を動かします。
展示画面とデモ名にはOpenClawやCopilotの名称を表示しません。

APKは既存プロジェクトと同じAndroid 14以上・arm64向けです。対象実機はXperia 1 VII（XQ-FS54／Android 16）です。
[ソニーのXperia 1 VII型番資料](https://helpguide.sony.net/mobile/xperia-1m7/v1/en/contents/about_help_guide.html)

## 粒子が立体を作る演出

新しいAI生成は粒子彫刻が標準です。「なす」などの指示で、AIが作る輪郭・ヘタ・茎などの立体を粒子の到着位置に変換し、粒が移動して形を作ります。現在の形から次の形へも連続して移動します。羽ばたきなど継続的に部品を動かす指示には、従来の立体描画を使います。

標準65,536粒、最大1,000,000粒。数は必要に応じて指定し、床は標準非表示です。箱を傾けると立体が傾き、振る反応が「散開」の景色では粒が散って再び形へ集まります。詳しい仕組みと実機計測は[粒子彫刻](PointSculpture.md)を参照してください。

## 操作

- 箱を傾ける：光が傾けた側へ流れます。
- 箱を回す：渦の流れに影響します。IMUの姿勢から回転速度を求めます。
- 箱を短く振る：光が散る／集まる反応。景色の設定で変わります。
- 静かに保つ：光が少しずつまとまります。
- 入力欄上のマイクアイコン：Xperia 1 VIIのマイクで日本語を認識し、認識した言葉を表示して自動で演出を変更します。聞き取り中にもう一度マイクを押すと取り消します。
- 下部の半透明入力欄：直接文字を入力し、右端の矢印で送信します。Androidではキーボードを閉じる操作だけでは送信しません。PCではEnterも使えます。作成中は矢印が停止アイコンになり、依頼を取り消せます。
- 右上のハンバーガーメニュー →「星」「海」「蛍」：端末内の景色を選びます。これらはAIへの依頼ではありません。
- メニュー → 時計アイコン「記録」：指示と設定差分、利用トークン、推定クレジットを確認し、過去の景色を追加のAI通信なしで呼び出せます。
- メニュー → グリッドアイコン「景色」：群泳、料理、森、太陽系、雪、ハート、街と花火を端末内で選び、言葉で変更できます。
- メニュー → 左右の矢印「戻す」「やり直す」：直前の景色全体を16段階戻したり進めたりできます。
- メニュー → 保存／フォルダーアイコン「保存」「呼出」：お気に入りを1件保存し、アプリ再起動後も呼び出せます。
- メニュー → 照準アイコン「姿勢」：持ちやすい角度を基準姿勢にします。
- メニュー → 家アイコン「トップ」：ゲーム選択へ戻ります。進行中の音声入力と通信を取り消します。

箱の新しいサンプルが来なくなった場合は既存の共通入力に従い端末IMUへ切り替え、
端末IMUもない場合は画面中央のドラッグで操作できます。通常画面の常設説明は省略しています。
位置の移動距離を推定する機能はありません。

PCでは矢印／WASDで傾け、Q/Eで回転、Spaceで振る、Vで文字入力、Rで姿勢リセット。
1/2/3で景色、Escでトップへ戻ります。メニューやウィンドウを操作中のEscは、その画面を閉じます。文字入力中はゲームのショートカットを抑止します。

メニューは約0.22秒のフェードとスライドで表示され、外側のタップまたは右上の×で閉じます。
履歴や設定も半透明ウィンドウへまとめています。詳しい利用量と接続情報は必要なときだけ開いて確認できます。

## デモの流れ（約90秒）

1. 星あかりのまま箱を傾けたり振ったりして、入力への反応を体験します。
2. 「水中のように、青くゆっくり動いて」と話し、認識結果の表示と自動で始まる景色の作成、色と動きの変化を見せます。
3. 「もっと粘り気を出して」と話して、箱を傾けた際の追従の違いを見せます。
4. 「振ると散らばらず、集まるようにして」と話し、同じ操作の反応を変えます。
5. 景色を保存し、別の景色から呼び出します。

光の数・群泳・雨雪・炎・花火・形への整列に加え、物体の組み立て・配置・アニメーション、
世界全体の色と明るさ、視点も言葉で操作できます。詳細と制限は [景色の操作機能](LightBoxScene.md) を参照してください。
物体は端末内で共通の形や独自の多角形を組み合わせて生成します。任意コードや新しいゲームルールは生成しません。

## 構成

```text
Spresense → USB / SPR-BOX Service → UE5の姿勢入力 → 端末内の光のシミュレーション
Xperia 1 VIIのマイク → Android音声認識 → 端末内OpenClaw → SIM回線 → GitHub Copilot
                                             ↓ 検証済みの景色JSON
                                          UE5へ反映
```

音声認識はAndroidの認識サービスを使います。音声が外部に送信されるかは、そのサービスの構成によります。
初回はマイク許可後にもう一度マイクアイコンを押してください。音声認識サービスが無効／未導入の場合は、
日本語認識を設定するか文字入力を使用します。

OpenClawやTermuxをAPK内に同梱したり、アプリから自動インストールしたりはしません。
追加PCを使わない実機構成には、Xperia 1 VII上で別途Gatewayを起動する準備が必要です。
Android上のOpenClawホスティングは公式の管理サービス対象外です。

## Xperia 1 VIIでの演出サービス準備

1. Termux上（必要に応じてそのLinux環境内）で、互換性のあるNode.jsとOpenClawを用意します。
   `openclaw --version` と `openclaw gateway run --help` が動くことを確認してください。
   [公式のAndroidホスティングに関する説明](https://github.com/openclaw/openclaw/issues/48855)
2. デモ専用プロファイルでCopilotへログインし、利用できるモデルを選びます。

   ```bash
   openclaw --profile lightbox models auth login-github-copilot
   openclaw --profile lightbox models list
   openclaw --profile lightbox models set github-copilot/<利用可能なモデルID>
   ```

   モデルIDの部分は表示されたものへ置き換えます。契約・組織ポリシーによって使用可能なモデルが異なります。
   [Copilot接続の公式資料](https://docs.openclaw.ai/providers/github-copilot)
3. このリポジトリの `Tools/OpenClaw/start-lightbox.sh`、`lightbox-token.cjs`、`lightbox-observer.cjs`、`lightbox-pricing.json` をXperia 1 VIIの同じフォルダーへコピーし、実行します。

   ```bash
   bash start-lightbox.sh
   ```

   デモ専用プロファイルだけにHTTPエンドポイントとツール不使用の設定を行い、
   ループバックの18789番ポートでGatewayを起動します。保存済みの認証キーがある場合は再利用し、なければ生成して専用設定へ保存し、画面に表示します。
   通常のOpenClawプロファイルは変更しません。
4. Termuxの電池最適化を無効にし、Gatewayを起動したままSpresenseMazeへ戻ります。
5. 「ひかりの箱」→「設定」で接続先 `http://127.0.0.1:18789` と、起動画面の認証キーを入力します。
   Androidでは認証キーをAndroid Keystoreの鍵で暗号化し、アプリ専用のバックアップ対象外領域へ保存します。再起動後も同じ接続先で復元されます。SaveGame、ソース、履歴には含めません。Windowsでは起動中だけ保持します。
6. 文字入力で1回依頼し、次に内蔵マイクで依頼します。SIMでCopilotへ到達できることもここで確認します。

接続には `POST /v1/chat/completions`、モデル指定にはGatewayの既定エージェントを示す
`openclaw/default` を使います。直前の景色を毎回添えるため、相対的なお願いにも対応します。
通信失敗時の自動再送はしません。不正な景色の返答に限り、一度だけ組み直しを依頼します。
[HTTPエンドポイントの公式資料](https://docs.openclaw.ai/gateway/openai-http-api)

認証エラー、通信失敗、1回最大150秒のタイムアウト、不正／未完成な景色JSONでは既存の景色を保ちます。
依頼後にプリセットや保存景色へ切り替えた場合、古い返答は適用しません。
センサーへの即時反応は回線やAIを待たずに動きます。

## 開発と検証

新規アセットの再生成（既存ゲームのアセットは変更しません）：

```powershell
& 'D:\Epic Games\UE_5.8\Engine\Binaries\Win64\UnrealEditor-Cmd.exe' SpresenseMaze.uproject -run=pythonscript -script=Tools/CreateLightBoxAssets.py -unattended -nullrhi
```

新規マップは `/Game/Maps/LightBox_World`。GameMode接頭辞とMapsToCookへ登録済みです。
既存のSprBoxBridgeとAttitudeInputSubsystemを再利用します。

自動テストには、景色JSONの検証、不正返答時の維持、接続先の検証、30/120fpsの動きの比較、
振る反応、保存・再読込、HTTP通信と古い返答の取り消しが含まれます。
HTTPテストのfixtureは実際のOpenClaw／Copilotへ接続しません。

```powershell
# 別ターミナルでfixtureを起動
python Tools/LightBoxMockGateway.py
# ビルド済みエディタで実行
& 'D:\Epic Games\UE_5.8\Engine\Binaries\Win64\UnrealEditor-Cmd.exe' SpresenseMaze.uproject -unattended -nullrhi -LightBoxMockGateway '-ExecCmds=Automation RunTests SpresenseMaze' '-TestExit=Automation Test Queue Empty'
```

Xperia 1 VII上のGatewayと実際のCopilotによる文字入力、粒子彫刻のフレームレートは[粒子彫刻の実機記録](PointSculpture.md)を参照してください。SIM単独、Spresense USB併用、直接の発話精度、長時間展示時の発熱は別途確認が必要です。

### 2026-11-04の確認結果

- Windows Editor / Android arm64のコンパイル成功。
- Android_ASTC APK生成成功：`Package/LightBox/Android_ASTC/SpresenseMaze-arm64.apk`。
- 機能拡張後は、既存フライト6件＋ひかりの箱11件の自動テスト、計17件成功・失敗0件。
- HTTP fixtureへの実リクエスト、返答の適用、不正JSON時の維持、依頼取消後の景色維持を確認。
- トップ、ひかりの箱、文字入力、接続設定を1600×900で描画し、画像を確認。
- 起動用Bashスクリプトの構文とPython補助スクリプトの構文を確認。
- XQ-FS54／Android 16をADB接続し、新しいAPKを更新インストール。Termux OpenClawと本物のCopilotへの日本語指示9件の応答を検証。
- この時点では対象機種の同定、SIM単独、Spresense USB併用、実際の発話は未検証。詳細は [拡張機能と実機検証](LightBoxScene.md) を参照。

画面画像は `Saved/AutoShots/00_title.png`、`03_lightbox.png`、
`03_lightbox_words.png`、`03_lightbox_settings.png` に保存しています。
拡張後のテストレポートは `Saved/LightBoxExpandedTests/index.json` です。

## 記録とクレジット

右上の「記録」で、最大300回のAI通信の履歴と推定利用量を確認できます。
認証済みの端末内記録サービス（18791番）が元のモデルusageを読み取り、キャッシュ書込も含めて公表単価で計算します。
確定請求額・残高ではありません。失敗や自動修正も集計し、未取得を0に置き換えません。
現在の単価ファイルはGPT-6 Lunaを収録し、それ以外は単価未取得として表示します。

アプリ内の記録は `Saved/LightBox/Journal/journal.json`、画像は同じフォルダーの`<ID>_before.png`と`<ID>_after.png`です。
画像は最新20回分を保持します。認証キーは含みません。過去に画像が保存されていない指示は、設定を再描画する形でレビューします。
従来のお気に入り保存とは別に、表示中の景色も継続用に記録します。

2026-11-05の[品質レビュー](LightBoxReview-2026-11-05.md)と[画像付きレビュー](../Saved/SceneReview/review.html)を参照してください。
