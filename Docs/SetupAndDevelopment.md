# セットアップ・開発ガイド

使い方の概要は[スクリーンショット付きREADME](../README.md)へ。以下のコマンドはリポジトリ直下で実行します。

Spresense(SPR-BOX)のIMUで操作する UE 5.8 製 Android ゲームです。トップ画面で
**迷路**、**フライト**、**ひかりのアトリエ** を1回タップすると選択・3Dプレビュー、もう一度タップすると開始します
(各画面の「トップ」から戻れます)。

### ひかりのアトリエ

箱を傾けたり振ったりして光の粒を動かし、Xperia 1 VIIのマイクから「水中のようにして」
「森を作って」「目玉焼きを作って」と話すと、光の数・群れ・形・物体・背景・動きを組み合わせて景色を作る体験です。
**ひかりのアトリエの景色は、鳥・海・太陽・背景を含めてすべて粒子だけで描きます。** 通常の3Dモデル、水面や床の板、背景板を描く処理と専用素材は削除しました。タイトルの「ひかりのアトリエ」のプレビューも粒子です。
「なす」なら粒子が移動して形を作り、「カモメが飛ぶ」なら粒子でできた翼が羽ばたきます。波・花の開閉・曲線の流れなども同じ粒子の描画で再生します。変更しない部分を残し、変更部分だけが現在位置から各目的地へ吸い寄せられます。集合時間は指定できます。
通常の景色は131,072粒を基準に、**最大1,000,000粒の総予算**を指定できます。少数の光だけの景色では使用数が少なくなります。自動の床・枠はありません。詳しくは[粒子のみの描画と実機確認](ParticlesOnly.md)を参照してください。
AIは形・色・動きを設計し、端末内で粒子の位置を計算します。毎フレーム100万個の座標をAIへ問い合わせる方式ではありません。保存・呼び出し、戻す・やり直すも維持しています。
景色の中央をドラッグして360度見回し、2本指のピンチで拡大縮小できます。**☰ → 自動回転／視点を戻す**も利用できます。
右下には景色の説明・実粒子数・直近の依頼の推定crを薄い文字で残します。ドル表示はありません。音声認識やAIの待機中は処理段階と経過時間、形成中は0〜100%のバーを表示します。AI内部の正確な完了率は取得していません。
**☰ → ☆ 造形を磨く**では、完成画面をAIへ送り、比率・部品の接続・模様・構図の改善を依頼できます。追加のAI利用量が発生します。断面で作る胴体や点ごとの斑点・縞にも対応しています。[差分移動・表示・造形の仕組み](ParticleExperience.md)を参照してください。
右上の **☰ → 記録** には指示・設定差分・元トークン・累計推定クレジットを表示し、AI変更の前後画像も保存します。
マイクは入力欄の上中央にあります。話した内容は画面に表示され、そのまま景色の作成を開始します。
文字入力は半透明の入力欄から行い、**矢印を押して送信**します。
聞き取り中はマイクの周囲にリングが回り、もう一度押すと取り消せます。Androidではキーボードを閉じただけでは送信しません。
声での変更には端末内OpenClawとCopilotの準備が必要です。基本の3つの景色と、群泳・料理・森・羽ばたき・吸い上げ・連動する装置など13の見本は接続なしで選べます。
展示画面には技術検証を示す名称を出しません。
操作・端末内サービスの起動・検証手順は [ひかりのアトリエのガイド](LightBox.md)、
操作できる範囲と指示の例は [景色の操作機能](LightBoxScene.md) を参照してください。

### 迷路

- **傾ける** → ボールが転がる(IMU-Adv の roll/pitch を使用)
- **瞬間的に持ち上げる** → ボールがジャンプ(加速度の合成値が閾値超えで検出)
- **端末へ短い衝撃を与える** → 泥の抵抗を一時的に弱め、ガラス床の破砕を早める
- **壁にぶつかる** → 衝突速度に応じた火花とスマホのバイブレーションで手応えが返る
- SPR-BOX未接続のAndroid端末では、**端末内蔵IMU**へ自動で切り替わります
- 端末IMUも利用できない場合だけ、**左の仮想スティック + 右のジャンプボタン**を表示します
- エディタ上では **WASD/矢印キー + Space** で同じ操作を再現できます(実機なしで開発可能)

UIは短い日本語表記を基本とし、9本の操作ガイドを収録しています。

### フライト

自動で前進する機体を傾きで操縦し、敵を撃ちながらリングをくぐるゲームです。

- **左右に傾ける(roll)** → 左右へ移動 / **奥へ傾ける(pitch)** → 下降、手前へ傾ける → 上昇
- 機体は傾きと横移動に合わせてバンクし、回避時に大きく傾きます(yaw は基準から±15°に制限して機首へ反映)
- **レーザーは自動連射**。2門の機銃は遠い照準で交わるので、照準 = 実際の射線です
- **リング**を通過すると発光・効果音・翼端の光跡。5連続ごとにシールド回復。逃しても進行します
- コースは **海上 → 峡谷 → 空中基地 → 大型機** の順。撃墜されると区間の頭から再開します
- 開始時と入力元の切り替え時は一時停止し、「姿勢リセット」後のカウントダウンで再開します
- 「一時停止」から 再開 / 姿勢リセット / トップへ。PCでは WASD・矢印(傾き)、Q/E(yaw)、P(一時停止)、R(姿勢リセット)

操縦は小さな傾きで照準を合わせやすい応答曲線とし、切り返し・減速を速めています。
カメラは水平を保ちながら速度に応じて先を見て、低空・地形への接近で風切り音と画角が変わります。
小さな入力は精密に、大きな入力は軽快に反応します。通常操縦の最大入力は24°、不感帯は1.8°です。
**クイック回避**は短く左右へ傾けて戻す、画面右下の左右ボタン、PCのSpaceで発動します。
PCではA/D・左右キーで方向を指定でき、無入力のSpaceは最後に操縦した方向へ回避します。
回避は0.26秒・最大6.4mの横移動で、コース端を越えません。短い防御時間があり、再使用は1.6秒後です。
一時停止・カウントダウン中は回避と再使用待ち時間も停止します。傾け続ける通常操縦では
ジェスチャー回避は発動しません。一時停止画面でオン・オフを切り替えると端末に設定を保存します。

フライト専用HUDは白・ミント色・半透明の丸いボタンで統一しています。スコアは左上、リングは右上、
シールドは左下、回避ボタンは右下です。シールドは連続ゲージと遅れて減る残量、得点は補間表示、
コンボと予告は必要時のみ表示します。角度の数値や入力方式の表示はプレイ中に出しません。
通過リングも細い4分割アーチにし、遠方の発光を抑えています。タイトル・迷路・ひかりのアトリエも、
白・ミント・グラファイトと角丸の操作部を共通の基調にしています。
新しいタイトルは機体を右側に見せ、左側から3つの体験を選べます。
UIの方針と参考事例・画面確認手順は [UIデザインの更新](UIRefresh.md) を参照してください。

機体もHUDと同じ白・ミント・コーラルの配色に合わせています。自機はセラミック調の白い外装、
グラファイトの骨格、薄いデルタ翼、スモークキャノピー、横長の双発ノズルを持つ機体です。
翼の識別ストライプと灯火はミント色で、動く補助翼・昇降舵と銃口・光跡の位置も形状に合わせています。
敵ドローンは前進翼と分割バイザー、砲台は六角形の台座と角形の双砲身、大型機は幅広い翼と
段差のある装甲です。敵のセンサー・ノズルはコーラル色、射撃直前は暖色へ変化します。
外形だけで種類を区別できるようにし、発光は細いスリットに限定しています。

編隊は正面、追従、横断、左右からの挟撃、降下の5種類。敵は射撃前に約0.9秒発光し、
HUDに狙う位置を表示します。予告中は狙いが固定されるため、見てから回避できます。

リングと撃破を6秒以内につなぐとコンボになり、5コンボごとに倍率が0.2上昇します（最大2倍）。
リングは100点、5連続ごとに追加200点、敵機250点、砲台400点、大型機5000点が倍率の対象です。
継続した低空・接近飛行は25点、敵弾のかすめ回避は50点。低空・接近飛行は本コースの
同じ10m区間につき一度だけ加点し、周回するボス区間では加点しません。
被弾でコンボが切れます。撃墜時は得点・撃破数・リング数をチェックポイントの状態へ戻し、
被弾数と再挑戦数は保持します。クリアで5000点、無被弾かつ再挑戦なしなら追加3000点です。
評価はS（30000点以上）、A（22000点以上）、B（14000点以上）、C。
クリア時だけ自己ベストを `FlightRecords_v1` に保存し、自動撮影では保存しません。

## 構成

```
SpresenseMaze/
├─ SpresenseMaze.uproject
├─ Config/
│   ├─ DefaultEngine.ini                     … GameMode / Android 設定
│   ├─ DefaultDeviceProfiles.ini             … Xperia 1 VII 向け描画ターゲット
│   └─ DefaultEditorPerProjectUserSettings.ini … UE の MCP サーバ自動起動
├─ Content/
│   ├─ Maps/Title_Select                     … トップ画面(空のレベル、TitleGameMode)
│   ├─ Maps/Flight_Course                    … フライト(空のレベル、FlightGameMode)
│   ├─ Maps/MainMaze                         … 迷路(World Partition)
│   ├─ Flight/M_FlightSurface, M_FlightAdditive … 金属・塗装・ガラス用 / 加算発光
│   ├─ Materials/M_MazeTile, M_MazeGlow      … 単色 / 発光
│   └─ Materials/M_Ball, Textures/T_SoccerBall
├─ Build/Android/IconSource/                 … アプリアイコンの原画
├─ Source/SpresenseMaze/
│   ├─ MazeGenerator.h/.cpp   … 迷路生成・ステージ管理・全ギミック・ゲームフロー
│   ├─ MazeBallPawn.h/.cpp    … ボール物理・IMU入力・ジャンプ・振動
│   ├─ MazeHUD.h/.cpp         … Canvas HUD(日本語表示・ミニマップ・操作ガイド・ランキング)
│   ├─ MazeSaveGame.h/.cpp    … ステージ別タイムランキングの永続化
│   ├─ MazeGameMode.h/.cpp    … Pawn と HUD を差すだけ
│   ├─ AttitudeInputSubsystem … 迷路とフライト共通の姿勢入力(入力元選択・基準姿勢・平滑化)
│   ├─ TitleGameMode          … トップ画面(3つのメニュー)、FlightGameMode、背景の機体
│   ├─ FlightPawn             … 機体の操縦・自動射撃・シールド・カメラ・状態遷移
│   ├─ FlightCourse           … コース経路・景観・敵・弾・リング・爆発・効果音
│   ├─ FlightScenery          … 岩礁・峡谷の連続地形・基地施設・雲の配置
│   ├─ FlightLandmarks        … 灯台・港・峡谷の橋と滝・大型格納庫・レーダー
│   ├─ FlightScore / FlightScoring / FlightSaveGame … 得点・チェックポイント・自己ベスト保存
│   ├─ FlightFeedbackHUD     … コンボ・加点・編隊接近・射撃予告
│   ├─ FlightModels           … 機体・敵・リングの専用3Dモデル(コードで生成)
│   ├─ FlightAirframes        … 自機・ドローン・砲台・大型機の形状、装甲、灯火
│   ├─ FlightHUD / GameHudBase … フライトHUD / 日本語Canvas描画の共通部
├─ Tools/CreateFlightAssets.py              … 上記マテリアルと2つのマップを生成するエディタスクリプト
├─ Tools/CreateFlightScenery.py             … 岩肌・水面・雲・白波のマテリアルを生成
└─ Plugins/SprBoxBridge/                     … SPR-BOX SDK 連携(再利用可能)
    ├─ ThirdParty/Android/sprboxlib-std-0.3.0-release.aar
    ├─ ThirdParty/Android/sprboxlib-imuadv-0.3.5-release.aar
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

## タイムランキング

ステージごとのクリアタイムを端末に保存し、全ステージクリア後に一覧表示します。

- 保存先: SaveGame スロット `MazeTimes`(`UMazeSaveGame`)
- 各ステージ **上位5件**を昇順で保持(`UMazeSaveGame::MaxRankedTimes`)
- 画面には「今回」のタイムと `1位`〜`5位` を3列×2段で並べ、今回の記録に当たる行を
  ミントでハイライトします。自己ベストには `BEST` を表示します
- **全ステージを通しでクリアしたときだけ**ディスクへ書き込みます。途中でホームへ
  戻った場合は記録されません(`AMazeGenerator::HandleGoalOverlap`)

展示では「記録が残る」ことが並ぶ動機になるので、周回のたびに消えないよう端末側へ
永続化しています。

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

### Xperia 1 VII 描画ターゲット

`DefaultDeviceProfiles.ini` の `Android_Adreno_Vulkan` をXperia 1 VII向けの基準として
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

## Android試用版のインストール

[差分移動・進捗表示・画像レビューを含む「ひかりのアトリエ」試用版](https://github.com/NaitoMitsuharu/spresesneMaze/releases/tag/v0.5.0-preview.3) から
`SpresenseMaze-arm64.apk` を取得してください。Android 14以上のarm64端末向けで、
ゲームデータはAPK内に含まれるため、別のOBBファイルは不要です。
APKを端末へコピーして開き、ファイルを開いたアプリからのインストールを許可すると導入できます。

USBデバッグを使う場合は、PCから次の手順でインストール・起動できます。

```powershell
adb devices
adb install -r -t SpresenseMaze-arm64.apk
adb shell am start -n com.lati.spresensegames/com.epicgames.unreal.SplashActivity
```

パッケージ名は `com.lati.spresensegames` です（旧名 `com.lati.spresensemaze` から変更）。旧パッケージのアプリは別アプリ扱いのため `-r` でも記録は引き継がれません。
`-r` は既存アプリの記録を残して更新します。同じ署名のAPKに限り更新できるため、
署名の不一致が表示された場合はアンインストールせずに確認してください。
UEが生成する `Install_SpresenseMaze-arm64.bat` は最初に既存アプリを削除するため、
記録を残したい場合は上記の更新手順を使ってください。

起動後は「フライト」を選び、遊ぶ持ち方で姿勢を合わせて開始します。
SPR-BOX未接続でも端末内蔵IMUで試せます。回避は画面右下の左右ボタン、または
短く左右へ傾けて戻す動作です。ジェスチャー回避は一時停止画面で切り替えられます。
この試用版はDevelopmentビルドです。実機での操作感・表示・発熱は別途確認してください。

## 新規Android端末で「ひかりのアトリエ」を使う

APKをインストールするだけで、3つの基本の景色、13の見本、保存・呼び出し、傾きによる操作を使えます。
**言葉で景色を作るには、同じ端末で動くTermux／OpenClawと、Copilotの利用権限が別途必要**です。
PCはインストールに使えますが、準備後の展示では不要です。SIMまたはWi-Fiでインターネットへ接続します。

### 1. 端末とセンサーを準備する

- 対象端末は **Xperia 1 VII** です。接続実機はXQ-FS54／Android 16、2340×1080で、Spresenseの箱とUSB接続して使用します。APKの最低要件はAndroid 14以上・arm64です。
- Spresenseの箱を使う場合は、SDK対応ファームウェアを書き込んだ箱と、SDK配布物の **SPR-BOX Service APK**（`jp.co.sony.sprbox.service`）が必要です。Serviceをインストールし、USBデータケーブルで箱と端末を接続してUSB許可を与えます。
- SPR-BOX未接続でも端末内蔵IMUで試せます。「ひかりのアトリエ」の **☰ → 姿勢** で持ち方を合わせます。
- 日本語のAndroid音声認識サービスを有効にします。初回のマイク許可後は、もう一度マイクを押してください。サービスを用意できない端末では文字入力を使えます。

### 2. Termuxを入れて、ひかりのアトリエを開く

[Termux公式の導入手順](https://github.com/termux/termux-app#installation)（F-Droid版など）に従ってTermuxをインストールし、Spresense Gamesで「ひかりのアトリエ」を開きます。
あとはアプリが**自動で準備**します。開いたときの進捗表示で次の順に確認し、足りないものだけ補います。

1. 演出サービス（`127.0.0.1:18789`）が動いているか
2. Termuxが使えるか（インストール、許可、セットアップ済みか）
3. 演出サービスが止まっていれば、Termuxに新しいセッションを作って起動
4. 認証キーが未設定なら、Termuxから受け取って自動で保存

最初の1回だけ、アプリに「言葉で景色を作る準備」の画面が出ます。画面の案内に従ってください。

- **コマンド実行の許可**：Androidの「Termuxでのコマンド実行」の許可ダイアログで「許可」を選びます。
- **Termuxの初回セットアップ**：「コピーしてTermuxを開く」を押すと、セットアップ用の1行がコピーされてTermuxが開きます。Termuxの画面を長押しして「Paste」を選び、Enterを押します。
  Node.js／OpenClawの導入、必要ファイルの取得、アプリからの起動の許可（`allow-external-apps`）をまとめて行い、Copilotへのログインを案内します。ログイン時はブラウザーでGitHubの認証を完了してください。
  終わると自動でSpresense Gamesに戻り、アプリが演出サービスの起動と認証キーの設定を続けます。

コピーされる1行は次のとおりです。何度実行しても、足りない手順だけを行います。

```bash
bash -c "$(curl -fsSL https://raw.githubusercontent.com/NaitoMitsuharu/spresesneMaze/master/Tools/OpenClaw/setup-lightbox.sh)"
```

検証した構成はNode.js 24.22.2／OpenClaw 2026.10.10、モデルは`github-copilot/gpt-6-luna`です。別のモデルを使うときは`openclaw --profile lightbox models list`で確認し、`openclaw --profile lightbox models set <モデルID>`で設定します。
AndroidでのGatewayホスティングは公式の標準構成ではありません。セットアップで依存ライブラリや実行環境のエラーが出た場合は、その環境を先に整えてください。
APKにはTermux、OpenClaw、GitHubのログイン情報を同梱していません。[OpenClaw公式インストール資料](https://docs.openclaw.ai/install)、[Copilot連携の公式資料](https://docs.openclaw.ai/providers/github-copilot)も参照してください。
Termuxは電池使用量の「制限なし」に設定してください。

#### 認証キーについて

演出サービスの認証キーは**端末内サービスのキー**で、GitHubのパスワードやGitHubのアクセストークンではありません。
初回に生成したキーは専用プロファイルに保存し、次の起動でも同じものを使います。
アプリは、Termuxからアプリ宛てに直接返された結果としてキーを受け取り、アプリに認証キーがまだ無いときだけAndroid Keystoreで暗号化して保存します。設定済みのキーは置き換えません。キーを共有ストレージに書いたり、ほかのアプリへ渡したりはしません。
専用Gatewayは`127.0.0.1:18789`、利用量の記録サービスは`127.0.0.1:18791`で動きます。外部公開やポート転送は不要です。キーは公開・リポジトリへ登録しないでください。

#### 手動で準備する場合

自動準備を使わない場合は、Termuxで次を実行します。`start-lightbox.sh`も、起動時にアプリへ認証キーを渡します（未設定のときだけ保存）。

```bash
cd ~/lightbox
bash start-lightbox.sh
```

自動の受け渡しを使わないときは`LIGHTBOX_AUTO_PAIR=0 bash start-lightbox.sh`で起動し、☰ → 設定でキーを入力します。キーの表示は`node lightbox-token.cjs; echo`です。キーをチャットやGitHubへ貼り付けないでください。

### 3. 文字と音声で確認する

1. 「ひかりのアトリエ」の進捗表示が終わり、準備の画面が出ていないことを確認します。**☰ → 設定**の認証キー欄が「設定済み」になっています。
2. 半透明の入力欄へ「青い海の中を、256匹の魚が3つの群れで泳ぐ景色にして」と入力し、キーボードを閉じて矢印を押します。
3. 景色が変わったら、独立したマイクを押して「魚だけ金色にして」と話します。認識が完了すると自動送信します。聞き間違いがあれば停止ボタンで取り消し、文字入力で送信できます。
4. 右下の説明・実粒子数・今回の推定cr、**☰ → 記録**の詳細を確認します。取得前は集計中として表示します。修正再試行も今回の依頼のcrに含めます。
5. 造形を改善したいときは、形成が終わってから**☰ → ☆ 造形を磨く**を押します。現在の景色の画像をOpenClaw経由でモデルへ送るため、画像入力に対応するモデルが必要です。通常生成とは別の追加依頼です。

作成中は矢印が停止アイコンに変わり、押すと取り消せます。通信エラーや不正な返答では現在の景色を維持します。
認証キーはAndroid Keystoreの鍵で暗号化して端末内に保存するため、アプリ再起動での再入力は不要です。
アプリのデータ消去・アンインストール・新しい端末への移行の後も、ひかりのアトリエを開けば自動で設定し直します。
クレジットはモデル利用量からの推定で、確定した請求額や残高ではありません。

### 翌日以降の起動と困ったとき

**ひかりのアトリエを開くだけです。** 端末の再起動などで演出サービスが止まっていれば、開いたときに自動でTermuxに起動させます。
途中で止まったときは **☰ → 設定 → 「接続を自動で準備する」** で、確認をやり直せます。
終了時はTermuxの該当セッションでCtrl+Cを押します。

| 症状 | 確認すること |
|---|---|
| 「認証キーを入力してください」 | ☰ → 設定 → 「接続を自動で準備する」を押す。準備の画面が出たら案内に従う |
| 認証エラー | Gatewayのキーを変更した場合は、☰ → 設定でアプリ側のキーも更新（自動設定は設定済みのキーを置き換えません） |
| 演出サービスにつながらない／「演出サービスが起動しませんでした」 | 準備の画面の「Termuxを開く」でTermuxの表示を確認（Copilotの再ログインが必要な場合があります）。電池制限、SIM／Wi-Fiも確認 |
| 準備の画面が何度も出る | 「コピーしてTermuxを開く」の1行をTermuxで実行し終えたか確認。Termuxの通知に`allow-external-apps`の案内が出ていないか確認 |
| 景色は変わるがクレジットが未取得 | ~/lightboxに初回セットアップのファイルがあり、18791番の記録サービスが起動しているか確認。未取得は無料の意味ではありません |
| マイクが使えない／聞き取れない | マイク許可、日本語の音声認識サービス、通信を確認。文字入力で切り分け |
| Spresenseを傾けても動かない | Service APK、USBデータケーブル、USB許可、ファームウェアを確認し、☰ → 姿勢を実行 |

操作と接続の詳細は[ひかりのアトリエのガイド](LightBox.md)、今回の実機確認結果は[入力と景色のレビュー](LightBoxInputReview-2026-11-05.md)を参照してください。

## ビルド手順

1. `SpresenseMaze.uproject` を右クリック → **Generate Visual Studio project files**
2. `.uproject` をダブルクリック(モジュールのビルドを聞かれたら Yes)
3. **Play** で動作確認(WASD=傾き、Space=ジャンプ)
4. **Platforms > Android > Package Project** で APK 作成 → 端末にインストール

## 実機での動き

共通の `UAttitudeInputSubsystem`(GameInstanceサブシステム)が一度だけ
`InitSprBox(480Hz / ±16g / 2000dps)` を呼び、SPR-BOX Service にバインドします。Spresense 接続済みなら自動で IMU ストリーミングが
始まり、端末上の仮想コントローラーに代わって IMU で操作できます。

入力は3段のフォールバックになっています。SPR-BOXの**新しいサンプルが届いている間**は
その姿勢・加速度を最優先し、0.5秒以上サンプルが更新されなければ切断扱いとして
Android端末内蔵IMUへ自動で切り替わります(接続表示だけでは使用中と判定しません)。どちらのIMUも
使えない場合にだけ、画面左下に仮想スティック、右下にジャンプボタンを表示します。
PCエディタでは従来どおりWASD/矢印キー + Spaceへフォールバックします。

Androidアプリアイコンは `Build/Android/IconSource/SpresenseGamesIcon.png` を原画として、
LDPIからXXXHDPIまでドットの輪郭を保った解像度別PNGを収録しています。

### キャリブレーション

2系統あります。

- ホーム画面の**「調整」** … SDK の2ステップ・ウィザード。
  5秒静止 → 姿勢を変える → 5秒静止。サンプリング中は入力を凍結します
- プレイ中の**「姿勢リセット」** … 現在の持ち方を中立にします(入力元ごとに保持)。
  表示する角度とゲームに適用する角度は同じ値です。迷路とフライトで共有されます

入力元が切り替わると、迷路はボールへの入力を止めて「姿勢リセット」を促し、
フライトは一時停止します。持ち替え直後の急な傾き入力を防ぐためです。

### 共通の姿勢入力 (`UAttitudeInputSubsystem`)

`GetState()` が1フレームに1回だけ更新した `FAttitudeState` を返します。

| 項目 | 内容 |
|---|---|
| `Source` | `SprBox` / `DeviceMotion` / `Keyboard` / `None` |
| `RollDeg` / `PitchDeg` / `YawDeg` | 中立からの角度(平滑化後)。roll+ = 右下がり、pitch+ = 奥が下がる |
| `Accel` | 重力込みの加速度 (m/s²)。静止時 |a| ≈ 9.8 |
| `SampleTime` / `SampleCount` | 入力元が最後に新しいサンプルを出した時刻 / 通し番号 |
| `bAvailable` | 新しいサンプルが届いているか |

`ResetNeutral()` で中立を設定、`GetSourceEpoch()` は入力元が変わるたびに増えます。

端末を95°以上傾けると入力を止めて警告を出し、80°を下回るまで解除しません
(ヒステリシス付き)。

## フライトのアセットと確認用撮影

マテリアルとマップはC++から作れないため、エディタスクリプトで生成しています。

```
UnrealEditor-Cmd.exe SpresenseMaze.uproject -run=pythonscript -script=Tools/CreateFlightAssets.py -unattended
```

マップは空で、空・光・霧・景観・敵はすべて実行時にC++で生成します。各マップのGameModeは
`DefaultEngine.ini` の `GameModeMapPrefixes`(`Title_` / `Flight_`)で決まります。

開発ビルドでは `-TitleAutoShot -FlightAutoShot` を付けて起動すると、トップ画面と
フライトの各区間(海上・峡谷・空中基地・大型機・一時停止)を自動で巡回して
`Saved/AutoShots/` にスクリーンショットを保存し、終了します。

### フライトの景観

`FlightScenery.cpp` が景観を生成します。海上は侵食された岩礁と群島、峡谷は水際から
台地まで連続する両岸の地形です。標高に沿った地層・濡れた水際・細かな岩肌の濃淡を
組み合わせています。空中基地は格納庫、管制塔、支持フレーム、エンジンポッド、手すり、
誘導灯を備えた施設として構成し、雲は輪郭がぼける半透明面で描画します。

水面は複数方向の動く波の法線と粗さで表現します。景観は区間ごとのメッシュと
インスタンス描画を使い、距離で描画を打ち切ります。ボリューム雲や追加の外部アセットは
必要ありません。Android実機でのフレームレート・発熱は別途確認してください。

岩は細かな法線と水際の濡れ、海岸線には動く泡を加えています。基地の外壁は継ぎ目、傷、
汚れを表現する素材です。海上には灯台と港、峡谷には高架橋と滝、基地にはアーチ屋根の
大型格納庫とパラボラレーダーがあり、飛行通路の外側へ配置しています。

景観用の7つのマテリアルだけを再生成する場合は次を実行します。既存マップと機体素材は維持します。

```
UnrealEditor-Cmd.exe SpresenseMaze.uproject -run=pythonscript -script=Tools/CreateFlightScenery.py -unattended -nullrhi
```

開発ビルドの `-FlightUpgradeReview` は各ランドマーク、射撃予告、一時停止、リザルトを巡回し、
`Saved/FlightUpgradeReview/` へ撮影して終了します。`10_result_preview.png` の得点は画面確認用の
サンプルで、プレイ記録へは保存しません。`SpresenseMaze.Flight` の自動テストは操縦の時間積分、
コンボ、重複加点の防止、チェックポイント復帰、記録の保存・読み込みを検証します。

`-FlightHudReview` は新HUDの準備・カウントダウン・プレイ・回避・一時停止・ボス・撃墜・
リザルトを撮影して `Saved/ModernFlightHUD/Standard/` に保存します。`-HudWide -ResX=1680 -ResY=720 -ForceRes`
を併用すると横長画面を `Wide/` に保存します。リザルトの得点は表示確認用のサンプルです。
回避の再使用制限、防御、移動範囲、一時停止中の凍結も検証し、プレイ記録は変更しません。

`-FlightModelReview -ResX=1600 -ResY=900 -ForceRes` は自機の前後、敵ドローンの前後、
砲台、大型機の前後を近景で撮影し、`Saved/ModernAirframes/` に保存します。
ゲームと同じ形状・素材を使い、HUDと進行・記録保存を止めた表示確認です。
自動テスト `SpresenseMaze.Flight.AirframeGeometry` は敵の形状について有限座標・頂点属性・
三角形の面積・表裏と法線の整合を検証します。

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

エフェクトと効果音は**すべて C++ 側で実装済み**です。音は外部アセットを持たず、
`USoundWaveProcedural` に波形を書き込んで `PlaySoundAtLocation` で鳴らしています
(Content にサウンドアセットはありません)。

拡張用に以下の BlueprintImplementableEvent も公開してあります。**Blueprint 側は
未実装**(Content に Blueprint が1つもない)なので、Blueprint サブクラスを作れば
C++ を触らずに演出を足せます。

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

`sprboxlib-imuadv` は現在 **v0.3.5** を使用しています。v0.3.2 以降は
`TARGET_OWN_POS` ではなく `TARGET_IMU` でIMUストリームを取得します。現行ファーム
ウェアは `V3_FEATURE_OWN_POS_STREAM` を一切advertiseしないため(`sprbox-SDK` の
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
