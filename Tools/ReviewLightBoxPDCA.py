"""Combine measured inference costs, image reviews and paired PDCA artifacts; no inference."""
import argparse, csv, html, json, pathlib, statistics

ROOT = pathlib.Path(__file__).resolve().parents[1]


def read(path, default=None):
    try:
        return json.loads(path.read_text(encoding='utf-8-sig'))
    except (OSError, ValueError):
        return default


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('directory', type=pathlib.Path)
    args = parser.parse_args()
    root = args.directory.resolve()
    rounds = {name: read(root / name / 'summary.json') for name in ['baseline', 'cycle1', 'cycle2', 'final']}
    rounds = {k: v for k, v in rounds.items() if v}
    notes = read(root / 'visual-reviews.json', {})
    cases = read(ROOT / 'Tools/Benchmarks/lightbox-100.json')['cases']
    calls = []
    for name in rounds:
        for file in sorted((root / name).glob('???.result.json')):
            result = read(file)
            for attempt in result['attempts']:
                meter = attempt.get('meter') or {}
                calls.append(dict(round=name, case=result['id'], words=result['words'],
                                  id=attempt['id'], attempt=attempt['attempt'], seconds=attempt.get('seconds'),
                                  **{k: meter.get(k) for k in ['model', 'input', 'output', 'cacheRead',
                                                               'cacheWrite', 'total', 'credits', 'pricingDate']}))
    with (root / 'credits.csv').open('w', encoding='utf-8-sig', newline='') as stream:
        writer = csv.DictWriter(stream, fieldnames=list(calls[0]) if calls else ['round'])
        writer.writeheader()
        writer.writerows(calls)
    summary = {'commands': len(cases), 'inferenceCalls': len(calls),
               'creditsKnown': sum(c['credits'] for c in calls if c['credits'] is not None),
               'creditsUnknown': sum(c['credits'] is None for c in calls), 'rounds': {}}
    by_round = {}
    for name, data in rounds.items():
        by_round[name] = {r['id']: r for r in data['rows']}
        ratings = [r['visual']['rating'] for r in data['rows'] if r.get('visual')]
        summary['rounds'][name] = {k: data[k] for k in ['completed', 'accepted', 'calls', 'repairs', 'creditsKnown', 'creditsUnknown']}
        summary['rounds'][name].update(reviewed=len(ratings), visualMean=statistics.mean(ratings) if ratings else None)
    for name in ['cycle1', 'cycle2', 'final']:
        pairs = [(notes['baseline'][str(n)]['rating'], notes[name][str(n)]['rating'])
                 for n in by_round.get(name, {}) if str(n) in notes.get(name, {}) and str(n) in notes.get('baseline', {})]
        summary['rounds'].get(name, {}).update(pairedCount=len(pairs),
            pairedBefore=statistics.mean(a for a, b in pairs) if pairs else None,
            pairedAfter=statistics.mean(b for a, b in pairs) if pairs else None,
            improved=sum(b > a for a, b in pairs), unchanged=sum(b == a for a, b in pairs), regressed=sum(b < a for a, b in pairs))
    summary['device'] = read(root / 'device-review.json', {})
    summary['finalReplay'] = read(root / 'final-replay-summary.json', {})
    (root / 'pdca-summary.json').write_text(json.dumps(summary, ensure_ascii=False, indent=2), encoding='utf-8')
    table = ''.join(f'<tr><td><a href="{html.escape(n)}/index.html">{html.escape(n)}</a></td><td>{d["completed"]}</td><td>{d["accepted"]}</td><td>{d["calls"]}（再送{d["repairs"]}）</td><td>{d["creditsKnown"]:.4f} cr</td><td>{d["reviewed"]}</td></tr>' for n, d in summary['rounds'].items())
    payload = json.dumps({'rounds': rounds, 'cases': cases}, ensure_ascii=False).replace('</', '<\\/')
    document = '''<!doctype html><html lang="ja"><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1"><title>ひかりの箱・100指示と改善実験</title>
<style>body{background:#0a1520;color:#dbe9ee;font:16px/1.8 system-ui;margin:0}main{max-width:1500px;margin:auto;padding:32px}h1{font-size:34px}h2{margin-top:40px}a{color:#8edbce}table{border-collapse:collapse;width:100%}td,th{padding:10px;text-align:left;border-bottom:1px solid #3a505d}.note,article{background:#16303e;border-radius:12px;padding:20px;margin:16px 0}button,input,select{font:inherit;color:#deeeef;background:#203e4e;border:1px solid #496573;padding:7px;border-radius:7px}#compare{display:grid;grid-template-columns:repeat(auto-fit,minmax(350px,1fr));gap:15px}img{width:100%}.bad{color:#ffbe85}.muted{color:#b0c5d0}</style><main>
<h1>100の指示を実行し、景色を改善する</h1>
<p>実際のAndroid内OpenClaw＋GitHub Copilot（gpt-6-luna）に100種類の日本語指示を送り、UEの本番と同じ読み取り・描画処理で実行。問題を直し、50件、40件の比較実験を行いました。最後に40件を仕上げ確認（36件は同じ返答の再生、4件は再生成）しています。</p>
<div class="note"><strong>この実験の推定消費：TOTAL cr / CALLS呼び出し / 利用量未取得 UNKNOWN件</strong><br>以前の試験分は含みません。<a href="credits.csv">全呼び出しの利用量CSV</a> · <a href="pdca-summary.json">集計JSON</a></div>
<table><tr><th>段階</th><th>指示数</th><th>形式・要求の受付</th><th>AI呼び出し</th><th>推定消費</th><th>画像レビュー済み</th></tr>TABLE</table>
<p class="muted">受付は芸術的な成功率ではありません。再生成は非決定的です。各指示1サンプルの比較であり、統計的な品質保証ではありません。画像は0.2秒・2.5秒・6秒、中立姿勢で再生しています。振る操作・長時間発熱・SIMの電波変動は、この100件の画像試験には含みません。</p>
<p>問題例と比較用に選んだ同じ40件の画像評価は、初回2.08→仕上げ3.20/5でした。これは主観評価で、全100件の改善率ではありません。0は実行不可・要求未達、1は意図がほぼ伝わらない、2は重大な見た目の欠陥、3は意図と動きが伝わるが仕上げ不足、4は展示候補、5は展示の完成基準を満たす、という目安です。</p><p>最終の自動検証35件がすべて成功。Android代表12景色を各3時点で確認しました。</p><h2>改善前後を見る</h2><select id="case"></select><select id="time"><option value="0">0.2秒</option><option value="1">2.5秒</option><option value="2" selected>6秒</option></select><div id="compare"></div>
<h2>PDCAで変えたこと</h2>
<article><b>1回目：動きと接続を増やす</b><p>麺が消費されるflowと、注ぐ液体や光の輸送を保つstreamを分離。顔・器などへの接続点と、親の端に取り付ける関節を追加しました。蝶・クジラ・曲面の花びらを追加し、構図と材質、できないことの説明も指示しました。</p><p>50件で再実験。流れとクジラは改善しましたが、花の座標や歯車の軸をAIが取り違える例、初期粒を引き継ぐ例が残りました。</p></article>
<article><b>2回目：アプリが構造と動きを支える</b><p>接続済みの花・歯車・屋根を汎用部品として追加。不要な初期粒のコピーを検出し、限定編集では保護された項目を検査します。群れは近い粒同士を対応させ、変形先の速度も使って追従。形に沿って初期配置し、噴水・火の発生高度を指定位置に修正しました。</p><p>40件を再生成し、初回100件の最終返答も追加のAI通信なしで再検査・再生しました。Androidでも代表12景色を3時点で描画し、画像を保存しました。</p></article>
<h2>品質をさらに高めるには</h2>
<ol><li><b>美術監修した部品を増やす。</b>形と材質が整った人物・建築・植物・食器を用意し、AIは配置・色・動きの組み合わせを担当。今回の接続済みの花と歯車はこの方式の最初の実装です。</li>
<li><b>照明と構図の定型を設ける。</b>単体作品・箱庭・群れごとに主役の面積、背景との明暗、移動範囲を決める。今回は自動構図の拡大による切れを抑え、動く形の範囲も使います。暗い景色や大きな環境はさらに調整が必要です。</li>
<li><b>編集を、変更対象だけの指定へ移す。</b>毎回すべての景色を書き直すと意図しない変更が起こります。今回は背景・照明・カメラ・枠・粒数・速度の限定編集では、指定した項目だけを採用する処理まで実装しました。次は対象ID単位の編集へ広げるのが有効です。</li>
<li><b>展示向けの合格基準を継続して蓄積する。</b>認識しやすさ・接続・動作・構図・光を別々に評価し、PDT-FP1でフレーム時間と温度を測る。今回は画像・返答・利用量を保存する再実行可能な試験を用意しました。</li></ol>
<article><b>仕上げ：限定編集と動く構図を修正</b><p>背景だけの指示では、返答の他の項目を採用せず、飛行経路を元の値のまま保持します。花の部位別の色を修正し、花が閉じたときの高さも含めてカメラを合わせます。文字・流体実験などの未対応機能は、アプリからも制限を説明します。4指示を再生成し、40件と初回100件の返答を再生して確認しました。</p></article><h2>残る限界</h2><p>文字の正確な描画、透明なガラス、音声生成、流体物理、動的な経路探索、写実的な人物は未実装です。近い見た目のアニメーションが生成されても、その機能が実現したとは評価しません。すべての無茶振りが展示品質になる段階には達していません。</p>
<h2>実機と料金の読み方</h2><p>接続端末はHQ64CD0577（XQ-FS54、Android 16）です。PDT-FP1本体での性能確認は未実施。<a href="device-review.json">実機の確認記録</a>、<a href="device-contact.jpg">実機の代表画像</a>を参照してください。</p>
<p>crはOpenClawが保存したprovider usageを入力・出力・キャッシュ別に計算した推定で、請求額や残高ではありません。公表単価の設定日：2026-11-05。キャッシュ書き込み・再送・拒否された返答も含め、未取得を0と扱いません。価格設定はTools/OpenClaw/lightbox-pricing.jsonに保存しています。<a href="https://docs.github.com/en/copilot/reference/copilot-billing/models-and-pricing">GitHubのモデル料金</a></p>
<script>const data=PAYLOAD;const c=document.getElementById('case'),time=document.getElementById('time');for(const x of data.cases)c.add(new Option(String(x.id).padStart(3,'0')+' '+x.words,x.id));function show(){let n=+c.value;document.getElementById('compare').innerHTML=Object.entries(data.rounds).map(([name,s])=>{const r=s.rows.find(x=>x.id===n);if(!r)return `<article><b>${name}</b><p>この指示はこの回の再生成対象外です。</p></article>`;return `<article><b>${name} / 推定${r.credits.toFixed(4)}cr</b>${r.tag?`<a href="${name}/${r.tag}_t${time.value}.png"><img src="${name}/${r.tag}_t${time.value}.png"></a>`:'<p class="bad">受付で拒否され、未適用</p>'}<p>${r.visual?`${r.visual.rating}/5：${r.visual.comment}`:'レビュー未記入'}</p><p class="muted">${r.accepted?'受付':'拒否'} · ${r.attempts}呼び出し</p></article>`}).join('')}c.onchange=time.onchange=show;show();</script></main></html>'''
    document = document.replace('TOTAL', f'{summary["creditsKnown"]:.4f}').replace('CALLS', str(summary['inferenceCalls'])).replace('UNKNOWN', str(summary['creditsUnknown'])).replace('TABLE', table).replace('PAYLOAD', payload)
    (root / 'index.html').write_text(document, encoding='utf-8')
    print({k: v for k, v in summary.items() if k not in ['device', 'finalReplay']})


if __name__ == '__main__':
    main()
