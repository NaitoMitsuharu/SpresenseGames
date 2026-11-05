# タイトル・迷路・ひかりの箱のUI更新

フライトの機体とHUDの白・ミント・グラファイトを基調として、3つの体験の入口と操作部を整えました。
日本語の読みやすさ、PDT-FP1の横長画面、ゲームや光を見るための余白を優先しています。

## 参考にした事例

- [Nicolás Bertoa: Unreal 5 – Main Menu UI Design](https://nbertoa.com/2022/08/11/unreal-5-main-menu-ui-design/)：3D背景とメニューの分離、文字の強弱、ホバーの見せ方。
- [Oliver Read: Lost Replicant UI Prototype](https://oliver-read.com/prototype-master-page/lostreplicant)：作品の雰囲気を入口と操作部へ一貫させる考え方。
- [Epic: User Interfaces Feature Template](https://dev.epicgames.com/documentation/fortnite/user-interfaces-feature-template-in-unreal-editor-for-fortnite)：部品と表現を組み合わせるUIの構成例。

参考作品の画像・素材は同梱していません。既存の機体モデルとCanvas／Slateの描画で構成しています。

## 変更内容

- タイトル：3D機体の専用照明、緩やかな浮遊、左側の縦メニュー、文字と余白の階層、入力状態の表示。
- 迷路：開始画面、ステージ表示、ミニマップ、傾き表示、操作ボタン、クリア画面、2列のランキングカード。
  ゲーム中の生の姿勢数値を、方向が分かるコンパクトな表示へ変更しています。
- ひかりの箱：選択中の景色が分かるプリセット、主操作の白いピルボタン、保存・呼び出しの配置、
  角丸の文字入力・接続設定ダイアログ。長い認識文と状態文は画面内に収めます。
- 共通部品：`ExperienceUI.h/.cpp` に角丸、薄いパネル、グラデーション、Slateの入力欄・ボタンを集約。
  フライトの既存のHUD描画と機体モデルは維持しています。

迷路の保存形式、判定・難易度、センサー処理、ひかりの箱の演出JSONと接続処理は変更していません。
Canvasの文字計測には描画と同じUFontを使い、日本語のフォールバックとボタン内の位置を合わせています。

## 表示確認

Windows Editorをビルドした後、次を実行します。画像は `Saved/UiRefresh/幅x高さ/` に保存します。

```powershell
python Tools/ReviewExperienceUI.py
python Tools/ReviewExperienceUI.py --width 2520 --height 1080
```

タイトル、迷路のホーム・プレイ・ガイド・クリア・ランキング、ひかりの箱と2つのダイアログを撮影します。
`--quick` はタイトル・迷路ホーム・プレイ・ひかりの箱だけです。

迷路のクリア・ランキング撮影はDevelopment用のレイアウト確認です。
合成した記録はSaveGameへ書き込まず、既存のプレイヤー記録も上書きしません。

Android実機の描画性能と実際のタッチ操作は別途確認してください。

## 2026-11-04の確認結果

- Windows EditorとAndroid arm64のコンパイル、Android_ASTC APKの生成に成功。
- 既存のフライト・ひかりの箱の自動テスト11件成功、失敗0件。HTTP fixtureで通信も検証。
- 1600×900と2520×1080で各画面を描画確認。機体のはみ出し、ダイアログの折り返し、操作部の余白を調整。
- PDT-FP1はADB未接続のため、実機タッチとフレームレートは未確認。

APK: `Package/LightBox/Android_ASTC/SpresenseMaze-arm64.apk`


## 2026-11-05：ひかりの箱の操作を最小化

通常画面は右上のハンバーガーメニューと、下部の半透明入力欄・マイク・送信のみ。タイトル、説明、景色選択、クレジットの常時表示を外しました。操作の結果やエラーは必要時だけ短時間表示します。

- 約0.22秒のフェード＋移動で半透明のフローティングメニューを開閉。
- 保存、呼出、戻す、やり直す、姿勢、履歴、設定、景色、トップをメニューに収納。
- アイコンはフォントに依存しない線画で描き、必要な短いラベルとツールチップを付加。
- 半透明入力欄からEnter／矢印で送信。空欄は送信不可、作成中は停止ボタンへ切り替え。
- メニューの外側タップ、ウィンドウの×、Escで閉じる。入力中のゲームキー誤動作を抑止。
- Androidの入力中に出るネイティブ入力バーも半透明にし、ひかりの箱を離れると元の外観へ復帰。
- 設定や履歴は必要な情報だけをウィンドウ内に表示。クレジット記録は維持。

Windows 1600×900と2340×1080で画面を確認。Android XQ-FS54で入力、空欄／未接続時の送信、メニュー開閉、外側タップ、プリセット、戻す／やり直す、保存／呼出、景色一覧、履歴、設定を確認しました。お気に入りと景色・履歴はバックアップから元に戻しています。UI確認で新しいAI依頼記録は増えていません。

確認画像は`Saved/UiRefresh/phone-final-main.png`、`phone-final-menu.png`、`phone-typing-glass.png`。開閉動画は`Saved/UiRefresh/phone-menu-animation.mp4`です。PDT-FP1そのものでの確認は未実施です。

## マイクの独立配置と認証の保持

Sprightの`ExhibitionScreen.kt`の`VoiceButton`を参照し、入力欄の左に独立した白い塗りのマイクを配置しました。背景のプレートや文字は付けず、聞き取り・認識中だけ円形リングが回ります。72×72の操作領域を確保し、入力欄とは20の間隔を空けています。入力欄と送信ボタンの半透明の外観は維持しています。

認証エラーは再起動でキーが消えていたためでした。Android Keystoreを使ったAES-GCM暗号化保存を追加し、接続先を改ざん検出の対象に含めています。暗号文はアプリ専用の`no_backup`に保存し、平文のキーをSaveGameやログへ記録しません。未設定の空欄を確認しても設定済みとは扱わず、設定画面のヒントで設定の有無が分かります。方式の根拠：[Android Keystore公式資料](https://developer.android.com/privacy-and-security/keystore)。

Android XQ-FS54で再起動後の復元、実際のAI指示の反映、マイクの聞き取り開始・取消・リング、文字入力を確認。検証用の景色変更はUndoで元に戻し、お気に入りを維持しました。AI確認は1回、推定0.12558クレジットで、アプリの記録にも残しています。

実際のAndroid Keystoreを使った保存・再読込、接続先の分離、毎回異なる暗号化、改ざん・破損・鍵の欠落、入力不正時の保持を13項目検証しています。再実行は`python Tools/AndroidCredentialTests/run.py`（インストール済みのDevelopment版と同じデバッグ署名を使用）。テストは専用の保存先と鍵を使い、終了時に削除します。

画像：`Saved/UiRefresh/phone-voice-separated.png`、`phone-voice-listening.png`、`phone-key-restored-after-restart.png`。記録：`Saved/UiRefresh/voice-ui-review.json`。
