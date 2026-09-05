# SpresenseMaze — Spresenseを傾けて遊ぶコロコロ迷路

Spresense(SPR-BOX)のIMUで操作する UE 5.8 製 Android 迷路ゲームです。

- **傾ける** → ボールが転がる(IMU-Adv の roll/pitch を使用)
- **瞬間的に持ち上げる** → ボールがジャンプ(加速度の合成値が閾値超えで検出)
- **端末へ短い衝撃を与える** → 泥の抵抗を一時的に弱め、ガラス床の破砕を早める
- **壁にぶつかる** → 衝突速度に応じた火花とスマホのバイブレーションで手応えが返る
- SPR-BOX未接続のAndroid端末では、**端末内蔵IMU**へ自動で切り替わります
- 端末IMUも利用できない場合だけ、**左の仮想スティック + 右のジャンプボタン**を表示します
- エディタ上では **WASD/矢印キー + Space** で同じ操作を再現できます(実機なしで開発可能)

UIは短い日本語表記を基本とし、9本の操作ガイドを収録しています。

## 構成

```
SpresenseMaze/
├─ SpresenseMaze.uproject
├─ Config/
│   ├─ DefaultEngine.ini                     … GameMode / Android 設定
│   └─ DefaultEditorPerProjectUserSettings.ini … UE の MCP サーバ自動起動
├─ Content/
│   ├─ Maps/MainMaze                         … 唯一のレベル(World Partition)
│   ├─ Materials/M_MazeTile, M_MazeGlow      … 単色 / 発光の2枚だけ
│   └─ Materials/M_Ball, Textures/T_SoccerBall
├─ Source/SpresenseMaze/
│   ├─ MazeGenerator.h/.cpp   … 迷路生成・ステージ管理・全ギミック・ゲームフロー
│   ├─ MazeBallPawn.h/.cpp    … ボール物理・IMU入力・ジャンプ・振動
│   ├─ MazeHUD.h/.cpp         … Canvas HUD(日本語表示・ミニマップ・チュートリアル)
│   └─ MazeGameMode.h/.cpp    … Pawn と HUD を差すだけ
└─ Plugins/SprBoxBridge/                     … SPR-BOX SDK 連携(再利用可能)
    ├─ ThirdParty/Android/sprboxlib-std-0.3.0-release.aar
    ├─ ThirdParty/Android/sprboxlib-imuadv-0.3.2-release.aar
    └─ Source/SprBoxBridge/
        ├─ SprBoxBridge_UPL.xml … .aar組込み・Manifest追記・Javaブリッジ注入
        └─ SprBoxSubsystem      … Blueprint/C++から呼べるAPI
```

Blueprint は使っていません。**すべて C++** です。

## 迷路はテキストで書く

`MazeGenerator` は文字列の配列を読んで ISM(InstancedStaticMesh)を敷き詰めます。
Details パネルの `MazeLayout` / `Stages` を編集すると即座に形が変わります。

```
##########
#S.......#
########.#
#G.......#
##########
```

| 文字 | 意味 | 挙動 |
|---|---|---|
| `#` | 壁 | 高さ `WallHeight` |
| `.` | 床 | |
| `S` `G` | スタート / ゴール | |
| `H` | ハードル | 高さ `HurdleHeight`。ジャンプで越える |
| `O` | 穴 | 落ちるとスタートへ戻る |
| `I` | 氷ゾーン | 操作力 35%・damping 0.02(滑る)。連続セルは継ぎ目のない全面材として表示 |
| `M` | 泥ゾーン | 操作力 55%・damping 3.5(重い)。短い衝撃で一時的に抜けやすくなる |
| `^` `v` `<` `>` | ブーストパッド | その向きに `BoostAccel` の力 |
| `C` | ガラス床 | 乗ると亀裂が成長して `CrumbleDelay` 秒後に破砕。衝撃で早まり、`CrumbleRespawnSeconds` 秒後に復活 |
| `X` | 破壊壁 | 高速衝突、または加速度衝撃を伴う衝突で破壊。通常壁と異なる3分割アンバープレート |
| `B` | ボタン | 踏むと同ステージの `D` が全て開く |
| `D` | 扉 | ボタンで地面に沈んで開く |
| `W` | ワープ装置 | シアンの回転リングへ入ると、ペアの `W` の隣に出る(出現順に 0-1, 2-3 …でペア) |

## ステージ

本編6ステージ。後半4ステージはそれぞれ異なる経路構造と攻略テーマを持ちます。

1. **基礎区画** — 基本の迷路のみ
2. **砂岩回廊** — ハードル
3. **炉心区画** — 折り返しの多い長距離迷路・穴・ハードル
4. **低温区画** — 連続した氷ゾーンと泥ゾーンを順に通る蛇行コース
5. **加速区画** — 長距離迷路・方向別ブースト・破砕するガラス床・ブーストで突破する破壊壁
6. **中央制御室** — 分断された区画をワープで横断し、スイッチで扉を開く最終構成

`TutorialStages` にはギミック1つにつき1本、計9本のガイドが入っています
(移動 / ジャンプ / 落下 / 氷床 / 泥床 / 加速 / ガラス床 / スイッチ / ワープ)。
ホーム画面の「操作」から開始し、完了後は自動で次のガイドへ進みます。

## アートディレクション — KINETIC INSTRUMENT

6ステージをひとつの精密機器として見せる共通デザインへ統一し、ステージごとに
素材の色温度、機能光、迷路構造を変えています。隣接する壁を長い構造体へ自動結合し、
床も通常セルを連続面として生成します。
1マスごとのブロックや床目地が見えないため、従来のボクセル感を抑えています。

| ステージ | デザイン | 特徴 |
|---|---|---|
| 1 基礎区画 | **KINETIC INSTRUMENT** | グラファイト、サテンアルミ、低彩度の連続面 |
| 2 砂岩回廊 | **WARM TITANIUM** | 暖色チタンとアンバーの機能光 |
| 3 炉心区画 | **DEEP CORE** | 黒紫の光沢構造体と高コントラストの警告色 |
| 4 低温区画 | **CRYO ARRAY** | 青緑の金属面と冷たいアクア光 |
| 5 加速区画 | **COPPER FLUX** | 黒鉄・銅・オレンジのエネルギー系統 |
| 6 中央制御室 | **MONO CONTROL** | 中立色の制御面と機能色だけのアクセント |

画面内はステージ番号と名称だけを表示します。操作ガイドも同じ意匠に統一しています。

- 壁: 接続セルを長い構造体へ結合した、模様なしの低Roughness光沢面。上端は3段の面取り
- 床: 行・列の両方向へ最大矩形結合した無地の連続面。通常セルには目地なし。背景も無地のスタジオ面
- ボール: サッカーボール柄を使わないパールセラミック調
- 光: 壁・ハードル・扉の動的シャドウ、控えめなBloomとAmbient Occlusion
- UI: 青みのある半透明パネルと細いアンバーのアクセント
- ギミック: 全面材の氷・泥ゾーン、方向が明確なブースト、破砕アニメーション付きガラス床、3分割の破壊壁

ベース色と照明色はステージごとに変わりますが、機能色の役割は維持しています。

| 色 | 意味 | 使われる場所 |
|---|---|---|
| アンバー | **自分が働きかけるもの** | ゴール、スタート枠、ボタン、扉、ハードルの帯 |
| レッド | **危険を示すもの** | 穴の縁 |
| シアン | **自分を運ぶもの** | ブーストパッドの矢印、ワープの回転リングと光柱 |

ブースト、ガラス破砕、ワープ、破壊壁には個別の短い合成効果音があります。ガラス破砕時は
約85msのスローモーション、強い振動、発光する破片を同期させています。破壊壁は通常壁より
低く薄いアンバー色の3分割構造とし、両面の大きな発光X亀裂で破壊可能な対象だと示します。

氷と泥はセル単位の円形パッドではなく、連続する床マス全面を覆うゾーンです。
氷には細い傷と強い反射、泥には高い粗さと流れ跡を与え、境界と挙動を視覚的に判別できます。
通常の床と壁にはノイズやタイル模様を使わず、色・光沢・照明温度で各デザインを区別します。

色は `MazeGenerator` の **Theme** カテゴリで全部まとめて変えられます。

### Xperia 1 VI 描画ターゲット

`DefaultDeviceProfiles.ini` の `Android_Adreno_Vulkan` をXperia 1 VI向けの基準として
調整しています。

- Vulkan + Mobile Deferred + Mobile HDR
- Full HD+ネイティブ解像度
- 60fps上限（長時間展示時の温度・電池消費を考慮）
- 高品質テクスチャ、2カスケードの動的シャドウ、Bloom、軽量AO
- AndroidのDesktop Renderer / Lumenは実験扱いのため不使用

## ゲームの流れ

```
ホーム ──[開始]──> プレイ ──ゴール──> クリア表示 ──> 選択画面
  ↑        │                                              │
  │        └─[操作]──────> 操作ガイド(自動で連続)           │
  └──────────[ホーム] / 最終ステージクリア─────────────────┘
                                      [次へ]
```

HUD は Canvas 直描きで、ボタンはタッチ対応です。プレイ中は右上にミニマップ、
左上にステージ名を表示します。SPR-BOXまたはAndroid端末のIMU操作時は、傾きインジケータが出ます。
タッチ操作のガイドは「スティック」「ジャンプボタン」と実際のUI名称を表示します。文字は
表示サイズごとにランタイムフォントを直接ラスタライズし、スマホでの拡大ぼけを防ぎます。

## 前提

1. **UE 5.8** + Android開発環境(エディタの Platforms > Android のセットアップ手順に従い Android Studio/SDK/NDK を導入)
2. **Visual Studio 2022**(C++によるゲーム開発ワークロード)
3. SPR-BOX操作を使う場合のみ、Android端末に **SPR-BOX Service アプリ**(sprbox-SDK の app モジュールの APK)をインストール済みであること
4. SPR-BOX操作を使う場合のみ、Spresense と USB接続し、SPR-BOX Service の接続許可ポップアップを許可済みであること

## ビルド手順

1. `SpresenseMaze.uproject` を右クリック → **Generate Visual Studio project files**
2. `.uproject` をダブルクリック(モジュールのビルドを聞かれたら Yes)
3. **Play** で動作確認(WASD=傾き、Space=ジャンプ)
4. **Platforms > Android > Package Project** で APK 作成 → 端末にインストール

## 実機での動き

`MazeBallPawn::BeginPlay()` が `InitSprBox(480Hz / ±16g / 2000dps)` を呼び、
SPR-BOX Service にバインドします。Spresense 接続済みなら自動で IMU ストリーミングが
始まり、端末上の仮想コントローラーに代わって IMU で操作できます。

Android/iOSで有効なIMUデータが来ていない間は、画面左下に仮想スティック、右下に
SPR-BOX接続中はその姿勢・加速度を優先し、未接続時はAndroid端末の姿勢・加速度へ
自動で切り替わります。端末IMUも利用できない場合だけ仮想スティックとジャンプボタンを
表示します。PCエディタでは従来どおりWASD/矢印キー + Spaceへフォールバックします。

Androidアプリアイコンは `Build/Android/IconSource/SpresenseMazeIcon.png` を原画として、
LDPIからXXXHDPIまでドットの輪郭を保った解像度別PNGを収録しています。

### キャリブレーション

2系統あります。

- ホーム画面の**「調整」** … SDK の2ステップ・ウィザード。
  5秒静止 → 姿勢を変える → 5秒静止。サンプリング中は入力を凍結します
- プレイ中の**「姿勢リセット」** … SDK の `lightReset` を即時実行。
  ゲーム中に基準姿勢がずれた場合の補正に使います

端末を95°以上傾けると入力を止めて警告を出し、80°を下回るまで解除しません
(ヒステリシス付き)。

## 調整ポイント

エディタで Pawn / MazeGenerator を選択して変更できます。

**MazeBallPawn**

| プロパティ | 意味 | 初期値 |
|---|---|---|
| TiltForce | 傾き最大時の加速力 | 1500 |
| MaxTiltDeg | この角度で入力100% | 30° |
| TiltDeadZoneDeg | 手ブレ無視の不感帯 | 3° |
| JumpAccelThreshold | ジャンプ判定の加速度(m/s²)。静止時≈9.8 | 17 |
| JumpImpulse | ジャンプの初速(cm/s) | 400 |
| JumpCooldown | 連続ジャンプ防止 | 0.8s |
| FallRespawnDepth | この深さまで落ちたらリスポーン | 500 |
| ImuRate | IMU サンプルレート(Hz) | 480 |
| VibrationMin/MaxImpactSpeed | 振動の強さにマップする衝突速度 | 60 / 550 |

**MazeGenerator**

| プロパティ | 意味 | 初期値 |
|---|---|---|
| CellSize / WallHeight / HurdleHeight | 1マス / 壁 / ハードルの寸法 | 250 / 250 / 60 |
| JumpClearance | 壁の上端からこの分だけ下がジャンプの天井 | 20 |
| BoostAccel | ブーストパッドの力 | 3200 |
| IceControlScale / MudControlScale | 氷 / 泥での操作力の倍率 | 0.35 / 0.55 |
| CrumbleDelay / CrumbleRespawnSeconds | ガラスが割れるまで / 復活まで | 0.8s / 3.5s |
| BreakableWallImpactSpeed | 破壊壁を割る衝突速度の基準 | 290 |
| BreakableWallShockScale | 加速度衝撃を破壊力へ加算する倍率 | 32 |
| StartAtStage | デバッグ用: このステージから開始 | 0 |

持ち方に合わせて傾き方向が逆なら `MazeBallPawn.cpp` の `MapTilt(CurrentPitchDeg)` /
`MapTilt(CurrentRollDeg)` の符号を変えてください。

### ジャンプが壁を越えない仕組み

ハードル(60)の上に乗ったボールは既に高い位置にいるため、素の `JumpImpulse` だと
壁(250)を越えられてしまいます。ジャンプ力自体を下げると今度は床からハードルを
越えられなくなるので、**絶対的な天井**を渡す方式にしています。

`MazeGenerator` が毎 Tick `Ball->JumpCeilingZ = 壁の高さ - JumpClearance` を渡し、
`TryJump()` が現在のボール上端から逆算して初速を `sqrt(2g·余裕)` で頭打ちにします。
床の上では余裕が大きいので制限がかからず従来どおり、ハードルの上では自動的に
低いジャンプになります。浮いた状態でジャンプしても余裕が減る分だけ初速が下がるので
自己補正されます。

## 日本語表示について

`GEngine->GetLargeFont()` は Roboto を事前レンダリングしたビットマップフォントで、
**Latin グリフしか持ちません**。一方 Slate のデフォルトフォントには
`Engine/Content/Slate/Fonts/DroidSansFallback.ttf`(CJK収録)が fallback typeface
として登録済みです。

そこで HUD の文字は全て `AMazeHUD::JText()` を通しています。

- フォント: `FCoreStyle::GetDefaultFontStyle("Bold", Size)`
- 描画: `FCanvasTextItem`(`FSlateFontInfo` を受けるコンストラクタ)
- 計測: `FSlateFontMeasure`

フォントアセットの新規作成は不要で、`BaseGame.ini` の
`+EngineExtraStageFiles=(Path="Content/Slate")` によりパッケージ後の Android にも
同梱されます。

> **注意**: 日本語リテラルを含むソースは **UTF-8 BOM 付き**で保存してください。
> BOM が無いと MSVC がロケールのコードページで読んで文字化けします。

## Blueprintからも使えます

`SprBoxSubsystem`(Get Game Instance Subsystem)に以下が公開されています:

- `InitSprBox(Rate, AccelRange, GyroRange, Beta)`
- `GetImuData()` → ax/ay/az/gx/gy/gz/roll/pitch/yaw/Sequence/bValid
- `IsSpresenseConnected()` / `GetConnectionStatus()`
- `StartImuCalibration()` / `FinishImuCalibration()` / `RecalibrateImu()`

エフェクトや音は以下の BlueprintImplementableEvent にフックできます。
**現状どれも未実装です**(Blueprint サブクラスを作れば C++ を触らずに追加できます)。

- `MazeBallPawn::OnJumped`
- `MazeGenerator::OnGoalReached` / `OnStageStarted` / `OnAllStagesCleared` / `OnButtonPressed`

## SDK 依存について

sprbox-SDK **v0.3.0** の aar を使っています。v0.3.0 で protocol v3 が入り、
`sprboxlib-std` が protobuf に依存するようになりましたが、**protobuf のクラスは
aar に同梱されていません**。flat aar 参照(`implementation files(...)`)では POM が
無く transitive 解決されないため、`SprBoxBridge_UPL.xml` で明示的に宣言しています。

```gradle
implementation 'com.google.protobuf:protobuf-javalite:4.29.3'
```

これが無いと最初のフレーム受信時に `NoClassDefFoundError` で落ちます。
公式の sprbox-SDKSampleApp も同じ書き方をしています。
`sprboxlib-imuadv` は `std.protocol.v3` を参照するので、**std と imuadv は必ず
セットで更新**してください。

`sprboxlib-imuadv` は **v0.3.2** から `TARGET_OWN_POS` ではなく `TARGET_IMU` で
IMUストリームを取得します。現行ファームウェアは `V3_FEATURE_OWN_POS_STREAM` を
一切advertiseしないため(`sprbox-SDK` の
`examples/serial_controller/tests/cmd_decoder_test.cxx` 参照)、`own_pos` は
接続はできてもサンプルが一切届きませんでした。`TARGET_IMU` は同じサンプル形式・
同じレート(480/960/1920Hz含む)に対応しているため、姿勢計算(iml2 /
InertialMotionLib2)側のロジックは変更していません。

### 解決済み: iml2 (InertialMotionLib2) が初期化できなかった問題

`sprboxlib-imuadv` に同梱されている `libinertialmotionlib2.so` (arm64-v8a) を
NDKの `llvm-objdump`/`llvm-nm` で逆アセンブルし、同じ `.so` を(sprbox-SDKを介さず)
直接使っている社内の別リポジトリ `SpresenseDroidPractice` の実装と突き合わせて、
sprbox-SDK側に **2つの実バグ** があることを特定・修正しました(いずれも
sprbox-SDK側でコミット済み)。

1. **`pThis` 規約の誤り** — `Iml2_Create` を含むすべての `Iml2_*` エクスポート
   関数は、第一引数 `pThis` が **NULL のときだけ** 成功し、非NULLだと即座に
   `IML_RESULT_FAIL_INVALIDVALUE` を返す実装だった(内部staticシングルトンへ
   ディスパッチするつくりで、呼び出し側が渡す `IML_INTERFACE*` は実質使われて
   いない)。`inertialmotionlib2_jni.c` の全呼び出しを `pThis=NULL` に修正。
2. **`IML_FLOAT64` の型サイズ誤り** — `IML_Common.h` で `IML_FLOAT64` が
   `double`(8バイト)と定義されていたが、`SpresenseDroidPractice` の
   `IML_Common.h` では `float`(4バイト)。`IML_Vector3D`(加速度・ジャイロ・
   重力・位置・速度・キャリブレーションパラメータ全てに使用)のレイアウトが
   実バイナリの想定の2倍のサイズになっており、これが `SetGravityNorm`/
   `SetLatitude` の失敗や、position/velocityの `Infinity`、quaternionへの
   経過時間の混入といった破損出力の根本原因だった。`float` に修正。

この2点の修正後、`Iml2_Create` / `SetCondition` / `SetPosition` /
`SetVelocity` / `GetQuaternion` / `Iml2_UpdateImu` は正常に成功し、
position/velocityは滑らかに積分され、quaternionも正しい単位クォータニオンに
なりました。`SetGravityNorm`/`SetLatitude` はこの `.so` ビルドでは今も
非SUCCESSを返しますが、`SpresenseDroidPractice` の参照実装でもこの失敗は
致命的として扱われていない(該当ログすら出さずスルーしている)ため、
同様に非致命的として扱っています。

さらに、SpresenseMaze側にも別の実バグがありました。`MazeBallPawn.cpp` の
キャリブレーションwizard進行ロジック(`CalibPhase` タイマー)が
`bControlEnabled`(ホーム画面では`false`)の後に置かれていたため、
「調整」ボタンを押しても永久に1/2で止まっていました。進行ロジックを
`bControlEnabled` チェックより前に移動して修正。

実機(Xperia XQ-FS54)で、キャリブレーションwizardが最後まで完了し、
姿勢インジケータ・ゲーム画面の姿勢表示(`R/P/Y`)が実機の傾きに正しく追従する
ところまで確認済みです。

## トラブルシューティング

- **データが来ない**: SPR-BOX Service アプリが起動しているか、USB許可済みか確認(SDK の README 記載の再接続手順を試す)。ホーム画面の「サービス」「ボード」のランプで切り分けできます
- **起動直後に落ちる / NoClassDefFoundError**: 上記 protobuf の宣言漏れ
- **gradleビルド失敗**: `SprBoxBridge_UPL.xml` の依存バージョン(kotlin-stdlib等)と UE5.8 の AGP の組合せ問題の可能性
- **ジャンプが敏感/鈍い**: `JumpAccelThreshold` を調整(激しく振る子向けには20以上推奨)
- **文字が豆腐になる**: ソースが UTF-8 BOM 付きで保存されているか確認
