export const visitorPage = `<!doctype html>
<html lang="ja">
<head>
  <meta charset="utf-8">
  <meta name="viewport" content="width=device-width, initial-scale=1">
  <meta name="theme-color" content="#f5f4ee">
  <meta name="description" content="自作ファミコンゲームの .nes ファイルを確認し、展示用カートリッジの受付リストに登録します。">
  <title>Famicom Game Drop | ファミコンゲームの受付</title>
  <style>
    :root { color-scheme:light; --canvas:#f5f4ee; --paper:#fffefa; --ink:#183128; --deep:#123b2e; --muted:#52645c; --line:#d7dfd5; --mint:#d8eee1; --gold:#f8cc6b; --danger:#9b332a; }
    * { box-sizing:border-box; }
    html { scroll-behavior:smooth; }
    body { margin:0; background:var(--canvas); color:var(--ink); font:16px/1.6 system-ui,-apple-system,"Segoe UI",sans-serif; }
    button,input { font:inherit; }
    button { cursor:pointer; }
    a { color:inherit; text-underline-offset:.18em; }
    :focus-visible { outline:3px solid #d77b28; outline-offset:3px; }
    .shell { width:min(1160px,calc(100% - 40px)); margin-inline:auto; }
    .topbar { display:flex; align-items:center; justify-content:space-between; gap:20px; padding:22px 0; }
    .brand { display:flex; align-items:center; gap:12px; text-decoration:none; font-weight:800; letter-spacing:-.025em; font-size:1.05rem; }
    .brand-short { display:none; }
    .brand-mark { display:grid; place-items:center; width:42px; height:42px; border-radius:12px; color:var(--deep); background:var(--gold); font-weight:900; letter-spacing:-.09em; }
    .top-actions { display:flex; align-items:center; gap:20px; }
    .about-link { color:var(--muted); font-size:.9rem; font-weight:650; }
    .language { display:flex; gap:3px; padding:4px; border:1px solid var(--line); border-radius:999px; background:var(--paper); }
    .language button { border:0; border-radius:999px; background:transparent; color:var(--muted); padding:5px 12px; font-size:.86rem; font-weight:750; white-space:nowrap; }
    .language button[aria-pressed="true"] { background:var(--deep); color:white; }
    .hero { padding:34px 0 27px; }
    .eyebrow { display:inline-flex; align-items:center; gap:9px; margin:0 0 15px; font-size:.78rem; font-weight:850; letter-spacing:.13em; text-transform:uppercase; color:#316d52; }
    .eyebrow::before { content:""; width:9px; height:9px; border-radius:50%; background:#3baa73; }
    h1,h2,h3,p,figure { margin-top:0; }
    h1 { margin-bottom:14px; font-size:clamp(2.5rem,5vw,4.5rem); line-height:1.09; letter-spacing:-.065em; font-weight:850; }
    .lead { max-width:62ch; margin-bottom:18px; color:#344b3f; font-size:clamp(1.02rem,1.5vw,1.2rem); line-height:1.65; }
    .hero-points { display:flex; flex-wrap:wrap; gap:8px; margin:0; padding:0; list-style:none; }
    .hero-points li { border:1px solid #cddbd0; border-radius:999px; padding:6px 12px; color:#28533b; background:#e6f1e9; font-size:.82rem; font-weight:750; }
    .notice { display:flex; gap:14px; align-items:flex-start; margin:0 0 22px; padding:13px 17px; border:1px solid #e9d391; border-radius:16px; background:#fff8dc; color:#624e1e; }
    .notice strong { white-space:nowrap; }
    .notice p { margin:0; }
    .section-head { display:flex; justify-content:space-between; align-items:baseline; gap:16px; margin-bottom:18px; }
    .section-head h2 { margin:0; font-size:clamp(1.5rem,3vw,2.05rem); line-height:1.25; letter-spacing:-.04em; }
    .section-head p { margin:0; color:var(--muted); font-size:.9rem; }
    .steps { display:grid; grid-template-columns:repeat(3,minmax(0,1fr)); gap:14px; margin-bottom:52px; }
    .step { min-height:166px; padding:23px; border:1px solid var(--line); border-radius:20px; background:var(--paper); }
    .step-number { display:grid; place-items:center; width:34px; height:34px; margin-bottom:15px; border-radius:10px; background:var(--mint); color:#226344; font-size:.82rem; font-weight:900; }
    .step h3 { margin-bottom:6px; font-size:1.08rem; letter-spacing:-.02em; }
    .step p { margin:0; color:var(--muted); font-size:.9rem; }
    .work-area { padding-bottom:24px; }
    .upload-card,.help-card { border:1px solid var(--line); border-radius:24px; background:var(--paper); box-shadow:0 12px 34px #1831280a; }
    .upload-card { padding:clamp(22px,4vw,36px); }
    .upload-card h2 { margin-bottom:6px; font-size:clamp(1.5rem,3vw,2rem); letter-spacing:-.045em; }
    .upload-card .intro { margin-bottom:24px; color:var(--muted); }
    .field-label { display:block; margin-bottom:8px; font-size:.94rem; font-weight:800; }
    .file-input { position:absolute; width:1px; height:1px; opacity:0; overflow:hidden; }
    .file-picker { display:flex; align-items:center; gap:13px; min-height:78px; padding:14px; border:2px dashed #9cb5a5; border-radius:16px; background:#f5faf5; cursor:pointer; }
    .file-input:focus-visible + .file-picker { outline:3px solid #d77b28; outline-offset:3px; }
    .file-action { flex:none; padding:8px 13px; border:1px solid #9cb5a5; border-radius:9px; background:white; color:var(--deep); font-size:.88rem; font-weight:750; }
    .file-name { min-width:0; overflow-wrap:anywhere; color:var(--muted); font-size:.88rem; }
    .field-hint { margin:8px 0 18px; color:var(--muted); font-size:.85rem; }
    .gallery-consent { margin-top:19px; padding:17px; border:1px solid var(--line); border-radius:14px; background:#f7faf5; }
    .gallery-consent .check { margin-bottom:5px; }
    .gallery-consent p { margin:0 0 0 28px; color:var(--muted); font-size:.82rem; }
    .gallery-title { display:block; max-width:430px; margin:14px 0 0 28px; }
    .gallery-title input { display:block; width:100%; margin-top:6px; padding:10px 12px; border:1px solid #9cb5a5; border-radius:9px; background:white; }
    .gallery-title:has(input:disabled) { display:none; }
    .check { display:flex; align-items:flex-start; gap:10px; margin:0 0 20px; font-size:.92rem; }
    .check input { width:18px; height:18px; margin:4px 0 0; accent-color:#1c704f; flex:none; }
    .primary { width:100%; min-height:52px; border:0; border-radius:12px; background:var(--deep); color:white; font-weight:850; box-shadow:0 8px 16px #123b2e25; }
    .primary:hover { background:#1c624a; }
    .primary:disabled { opacity:.55; cursor:wait; }
    .feedback { margin:18px 0 0; padding:12px 14px; border-radius:12px; background:#eaf3eb; color:#215839; }
    .feedback.error { background:#fff0ea; color:var(--danger); }
    .feedback[hidden],.job-panel[hidden] { display:none; }
    .job-panel { margin-top:24px; padding:19px; border:1px solid #bedac4; border-radius:16px; background:#eff8f0; }
    .job-top { display:flex; justify-content:space-between; gap:12px; align-items:center; }
    .job-panel h3 { margin:0; font-size:1rem; }
    .state-badge { display:inline-block; padding:4px 9px; border-radius:999px; background:#d1e9d5; color:#205a38; font-size:.77rem; font-weight:800; }
    .job-panel p { margin:11px 0 0; color:#31493b; font-size:.9rem; }
    .job-extra { font-weight:750; }
    .link-button { display:inline-block; margin-top:12px; padding:0; border:0; background:none; color:#145c41; text-decoration:underline; text-underline-offset:.2em; font-size:.85rem; font-weight:750; }
    .help-card { padding:26px; }
    .gallery { margin:0 0 35px; padding:clamp(22px,4vw,35px); border:1px solid var(--line); border-radius:24px; background:#eaf3eb; }
    .gallery-head { display:flex; align-items:flex-start; justify-content:space-between; gap:14px; }
    .gallery-head h2 { margin:0 0 4px; font-size:clamp(1.4rem,3vw,1.9rem); letter-spacing:-.04em; }
    .gallery-head p { margin:0 0 17px; color:var(--muted); font-size:.9rem; }
    .gallery-refresh { border:1px solid #9cb5a5; border-radius:9px; background:white; padding:7px 12px; color:var(--deep); font-size:.82rem; font-weight:750; }
    .gallery-grid { display:grid; grid-template-columns:repeat(auto-fill,minmax(220px,1fr)); gap:12px; }
    .gallery-item { padding:17px; border:1px solid #c6dacb; border-radius:15px; background:var(--paper); }
    .gallery-item h3 { margin:0 0 5px; font-size:1.04rem; overflow-wrap:anywhere; }
    .gallery-item p { margin:0 0 13px; color:var(--muted); font-size:.82rem; }
    .gallery-item button { border:0; border-radius:9px; background:var(--deep); color:white; padding:9px 13px; font-size:.88rem; font-weight:750; }
    .gallery-item button:disabled { opacity:.55; cursor:wait; }
    .gallery-empty { margin:0; padding:22px; border:1px dashed #a8c0ac; border-radius:14px; color:var(--muted); background:#fffefa9c; }
    .details-area { display:grid; grid-template-columns:minmax(0,1.2fr) minmax(280px,.8fr); gap:20px; align-items:start; padding-bottom:70px; }
    .help-card h2 { margin-bottom:16px; font-size:1.25rem; letter-spacing:-.035em; }
    .help-item { padding:17px 0; border-top:1px solid var(--line); }
    .help-item h3 { margin-bottom:5px; font-size:.94rem; }
    .help-item p { margin:0; color:var(--muted); font-size:.88rem; }
    details { border-top:1px solid var(--line); padding-top:17px; }
    summary { color:#145c41; font-size:.89rem; font-weight:750; cursor:pointer; }
    details p { margin:10px 0 0; color:var(--muted); font-size:.86rem; }
    footer { border-top:1px solid var(--line); padding:24px 0 38px; color:var(--muted); font-size:.82rem; }
    footer .shell { display:flex; justify-content:space-between; flex-wrap:wrap; gap:10px 20px; }
    @media (max-width:800px) { .hero { padding:25px 0; } .details-area { grid-template-columns:1fr; } }
    @media (max-width:600px) { .shell { width:min(100% - 28px,1160px); } .topbar { padding:14px 0; } .top-actions { gap:8px; } .about-link { display:none; } .brand { font-size:.95rem; } .brand-mark { width:38px; height:38px; } .language button { padding:5px 9px; } h1 { font-size:clamp(2.5rem,10vw,4rem); } .notice { display:block; } .notice strong { display:block; margin-bottom:5px; } .section-head { display:block; } .section-head p { margin-top:5px; } .steps { grid-template-columns:1fr; gap:10px; margin-bottom:0; } .step { min-height:0; padding:17px; } .step-number { display:inline-grid; margin:0 10px 0 0; vertical-align:middle; } .step h3 { display:inline; } .step p { margin-top:6px; } .file-picker { align-items:flex-start; flex-direction:column; } .gallery-head { display:block; } .gallery-refresh { margin-bottom:15px; } }
    @media (max-width:360px) { .brand-full { display:none; } .brand-short { display:inline; } }
    @media (prefers-reduced-motion:reduce) { html { scroll-behavior:auto; } }
  </style>
</head>
<body>
  <header class="topbar shell">
    <a class="brand" href="/" aria-label="Famicom Game Drop"><span class="brand-mark" aria-hidden="true">FC</span><span class="brand-full">Famicom Game Drop</span><span class="brand-short">Game Drop</span></a>
    <div class="top-actions"><a class="about-link" href="#how" data-i18n="navHow">使い方</a><div class="language" role="group" aria-label="Language / 言語"><button id="langJa" type="button" lang="ja" aria-pressed="true">日本語</button><button id="langEn" type="button" lang="en" aria-pressed="false">EN</button></div></div>
  </header>
  <main class="shell">
    <section class="hero" aria-labelledby="page-title">
      <div><p class="eyebrow" data-i18n="eyebrow">ファミコン実機を使った展示プロジェクト</p><h1 id="page-title">ゲームを、<br>ファミコンへ。</h1><p class="lead" data-i18n="heroLead">自作の .nes ファイルを選ぶと、対応形式か確かめて展示用カートリッジの受付リストに登録します。</p><ul class="hero-points"><li data-i18n="point1">スマホから使える</li><li data-i18n="point2">アカウント不要</li><li data-i18n="point3">ファイルは一時保存</li></ul></div>
    </section>
    <aside class="notice" aria-label="現在の公開状況" data-i18n-aria="noticeTitle"><strong data-i18n="noticeTitle">現在の公開状況</strong><p data-i18n="noticeBody">ファイルの受付と順番待ちは試せます。カートリッジがこのクラウドから自動で受信する機能は準備中のため、今はアップロードだけで実機のゲームは切り替わりません。</p></aside>
    <section class="work-area" aria-label="ゲームファイルの登録" data-i18n-aria="uploadAria">
      <div class="upload-card" id="upload-area"><h2 data-i18n="uploadTitle">ゲームファイルを登録</h2><p class="intro" data-i18n="uploadIntro">ファイルを選び、利用できることを確認して送信してください。</p>
        <form id="upload"><label class="field-label" for="rom" data-i18n="fileLabel">ゲームファイル（.nes）</label><input class="file-input" id="rom" name="rom" type="file" accept=".nes,application/octet-stream" aria-describedby="fileHint" required><label class="file-picker" for="rom"><span class="file-action" data-i18n="chooseFile">ファイルを選ぶ</span><span class="file-name" id="fileName">選択されていません</span></label><p id="fileHint" class="field-hint" data-i18n="fileHint">対応形式は下の「使えるファイル」をご覧ください。</p><label class="check"><input name="authorized" type="checkbox" required><span data-i18n="rights">このファイルを使用・送信する権利があります。</span></label><div class="gallery-consent"><label class="check"><input id="galleryConsent" name="galleryConsent" type="checkbox"><span data-i18n="galleryConsent">このゲームを公開ギャラリーに載せてもよい（任意）</span></label><p data-i18n="galleryConsentDetail">公開すると、作品名が表示され、ほかの来場者がこのROMをプレイ待ちに追加できます。ファイル本体のダウンロードは公開せず、約1時間で削除します。</p><label class="gallery-title field-label" for="galleryTitle"><span data-i18n="galleryTitleLabel">公開する作品名</span><input id="galleryTitle" name="galleryTitle" type="text" maxlength="60" autocomplete="off" disabled></label></div><button class="primary" id="submitButton" type="submit" data-i18n="submit">ファイルを確認して登録</button></form>
        <p class="feedback" id="feedback" role="alert" hidden></p>
        <section class="job-panel" id="jobPanel" aria-live="polite" aria-atomic="true" hidden><div class="job-top"><h3 data-i18n="jobTitle">受付状況</h3><span class="state-badge" id="jobState"></span></div><p id="jobDescription"></p><p class="job-extra" id="jobExtra"></p><p data-i18n="bookmarkHint">このページのURLを保存すると、後から状況を確認できます。</p><button type="button" class="link-button" id="copyLink" data-i18n="copyLink">確認リンクをコピー</button></section>
      </div>
    </section>
    <section class="gallery" id="gallery" aria-labelledby="gallery-heading"><div class="gallery-head"><div><h2 id="gallery-heading" data-i18n="galleryHeading">公開ゲームから選ぶ</h2><p data-i18n="galleryIntro">公開に同意された作品を選んで、プレイ待ちに追加できます。</p></div><button class="gallery-refresh" id="galleryRefresh" type="button" data-i18n="galleryRefresh">一覧を更新</button></div><div class="gallery-grid" id="galleryGrid" aria-live="polite"><p class="gallery-empty" data-i18n="galleryLoading">ギャラリーを読み込み中…</p></div></section>
    <section class="details-area" id="how"><div aria-labelledby="how-title"><div class="section-head"><h2 id="how-title" data-i18n="howTitle">使い方は3ステップ</h2></div><div class="steps"><article class="step"><span class="step-number" aria-hidden="true">01</span><h3 data-i18n="step1Title">ゲームを選ぶ</h3><p data-i18n="step1Body">自作、または送信の許可がある .nes ファイルを選びます。</p></article><article class="step"><span class="step-number" aria-hidden="true">02</span><h3 data-i18n="step2Title">形式を確認</h3><p data-i18n="step2Body">このカートリッジに合う形式か、ページが自動で調べます。</p></article><article class="step"><span class="step-number" aria-hidden="true">03</span><h3 data-i18n="step3Title">受付状況を見る</h3><p data-i18n="step3Body">登録後はこのページで待ち順や結果を確認できます。</p></article></div></div><aside class="help-card" aria-labelledby="help-title"><h2 id="help-title" data-i18n="helpTitle">送信する前に</h2><div class="help-item"><h3 data-i18n="compatibleTitle">どんなファイルが使える？</h3><p data-i18n="compatibleBody">現行の公開版は、拡張子 .nes の一部の自作ファミコンゲームに対応します。一般的な市販ゲームはここにアップロードしないでください。</p></div><div class="help-item"><h3 data-i18n="afterTitle">送信後はどうなる？</h3><p data-i18n="afterBody">ファイルを検査し、管理者の承認後に実機へ送ります。実機での自動受信は検証中です。</p></div><div class="help-item"><h3 data-i18n="privacyTitle">ファイルは残る？</h3><p data-i18n="privacyBody">ファイルは非公開で一時保存し、約1時間後に削除します。公開は任意です。</p></div><details><summary data-i18n="technicalTitle">対応形式の詳細を見る</summary><p data-i18n="technicalBody">iNES形式・Mapper 0（NROM）、PRG 16 KiBまたは32 KiB、CHR 8 KiB。CHR-RAM、NES 2.0、4画面ミラーリングには未対応です。</p></details></aside></section>
  </main>
  <footer><div class="shell"><span data-i18n="footerLine">オープンなハードウェア実験 · Keitark</span><span><a href="https://github.com/Keitark/fc-rom-vomitter" target="_blank" rel="noopener noreferrer" data-i18n="sourceLink">設計とソースコードを見る</a> · <a href="https://github.com/Keitark/fc-rom-vomitter/blob/main/docs/licensing.md" target="_blank" rel="noopener noreferrer" data-i18n="licenseLink">画像のライセンス</a> · <a href="/operator" data-i18n="staffLink">展示スタッフ</a></span></div></footer>
  <script>
    const copy = {
      ja: {
        chooseFile:"ファイルを選ぶ",noFile:"選択されていません",uploadAria:"ゲームファイルの登録",metaDescription:"自作ファミコンゲームの .nes ファイルを確認し、展示用カートリッジの受付リストに登録します。",
        title:"Famicom Game Drop | ファミコンゲームの受付",navHow:"使い方",eyebrow:"ファミコン実機を使った展示プロジェクト",heroTitle:"あなたのゲームを、ファミコンへ。",heroLead:"自作の .nes ファイルを選ぶと、対応形式か確かめて展示用カートリッジの受付リストに登録します。",point1:"スマホから使える",point2:"アカウント不要",point3:"ファイルは一時保存",boardAlt:"実際のWi-Fiファミコンカートリッジ基板の3Dレンダリング",boardCaption:"展示用Wi-Fiカートリッジの基板レンダリング（実際の設計データ）",noticeTitle:"現在の公開状況",noticeBody:"ファイルの受付と順番待ちは試せます。管理者画面から1件ずつ実機へ送る機能は開発中で、まだ実機接続の確認が済んでいません。現時点ではアップロードだけでゲームは切り替わりません。",howTitle:"使い方は3ステップ",howSub:"まずは対応ファイルを1つご用意ください。",step1Title:"ゲームを選ぶ",step1Body:"自作、または送信の許可がある .nes ファイルを選びます。",step2Title:"形式を確認",step2Body:"このカートリッジに合う形式か、ページが自動で調べます。",step3Title:"受付状況を見る",step3Body:"登録後はこのページで待ち順や結果を確認できます。",uploadTitle:"ゲームファイルを登録",uploadIntro:"ファイルを選び、利用できることを確認して送信してください。",fileLabel:"ゲームファイル（.nes）",fileHint:"対応形式は下の「使えるファイル」をご覧ください。",rights:"このファイルを使用・送信する権利があります。",submit:"ファイルを確認して登録",jobTitle:"受付状況",bookmarkHint:"このページのURLを保存すると、後から状況を確認できます。",copyLink:"確認リンクをコピー",copied:"確認リンクをコピーしました。",copyFailed:"コピーできませんでした。ブラウザのURLを保存してください。",helpTitle:"送信する前に",compatibleTitle:"どんなファイルが使える？",compatibleBody:"現行の公開版は、拡張子 .nes の一部の自作ファミコンゲームに対応します。一般的な市販ゲームはここにアップロードしないでください。",afterTitle:"送信後はどうなる？",afterBody:"ファイルを検査し、管理者が選ぶまで受付リストで待機します。実機への転送は現在開発中です。",privacyTitle:"ファイルは残る？",privacyBody:"ファイルは非公開で一時保存し、約1時間後に削除します。ログインは不要です。",technicalTitle:"対応形式の詳細を見る",technicalBody:"iNES形式・Mapper 0（NROM）、PRG 16 KiBまたは32 KiB、CHR 8 KiB。CHR-RAM、NES 2.0、4画面ミラーリングには未対応です。",footerLine:"オープンなハードウェア実験 · Keitark",sourceLink:"設計とソースコードを見る",licenseLink:"画像のライセンス",staffLink:"展示スタッフ",validating:"ファイルを確認しています…",loading:"受付状況を確認しています…",networkError:"通信が途切れました。数秒後に再試行します。",errorGeneric:"登録できませんでした。別のファイルをお試しください。",position:"待ち順：",until:"受付期限：",waitingOperator:"管理者が次に進めるまで待機しています。",releasedToCart:"管理者が送信を指示しました。実機への自動受信は検証中です。",states:{queued:["受付済み","管理者が次に進めるまで待機しています。"],claimed:["カートリッジが確認中","カートリッジがファイルの受信を始めています。"],downloaded:["転送中","カートリッジがファイルを受け取りました。"],deferred:["安全な状態を待っています","実機が安全に切り替えられるまで待機しています。"],installed:["準備完了","カートリッジに反映されました。会場スタッフの案内に従って実機を操作してください。"],unchanged:["すでに登録済み","このゲームはすでにカートリッジで使用されています。"],failed:["処理できませんでした","カートリッジでの処理に失敗しました。会場スタッフにお知らせください。"],expired:["受付期限切れ","時間内に処理されなかったため、ファイルを削除しました。"],cancelled:["受付を取り消しました","会場スタッフがこの受付を取り消しました。"]},errors:{authorization_required:"送信する権利があることを確認してください。",missing_file:".nes ファイルを選んでください。",too_large:"ファイルが大きすぎます。",short_header:"ファイルの内容を読み取れませんでした。",bad_magic:"これは .nes 形式のファイルではありません。",nes2_unsupported:"この形式のファイルは現在対応していません（NES 2.0）。",mapper_unsupported:"このゲームの方式は現在対応していません（Mapper 0のみ）。",prg_geometry:"ゲームデータの大きさが対応範囲外です。",chr_geometry:"画像データの大きさが対応範囲外です（CHR 8 KiBが必要）。",four_screen_unsupported:"このゲームの画面方式は現在対応していません。",length_mismatch:"ファイルの長さが正しくありません。",duplicate:"このファイルはすでに受付されています。",cooldown:"続けて送信する前に少しお待ちください。",queue_full:"現在受付がいっぱいです。後でもう一度お試しください。",upload_invalid:"ファイルを読み取れませんでした。",not_found:"この確認リンクの受付は見つかりませんでした。"}
      },
      en: {
        chooseFile:"Choose file",noFile:"No file selected",uploadAria:"Game file upload",metaDescription:"Check a homebrew .nes game file and add it to the exhibition cartridge queue.",
        title:"Famicom Game Drop | Share a homebrew game",navHow:"How it works",eyebrow:"A real Famicom exhibition project",heroTitle:"Your game, on a Famicom.",heroLead:"Choose a homebrew .nes file. We check its format and add it to the exhibition cartridge's queue.",point1:"Works on your phone",point2:"No account needed",point3:"Temporary storage",boardAlt:"3D render of the actual Wi-Fi Famicom cartridge board",boardCaption:"Wi-Fi exhibition cartridge, rendered from the real board design",noticeTitle:"Current demo status",noticeBody:"File validation and the queue are available. Operator-controlled delivery is in development and has not yet been verified on a physical cartridge. Uploading alone does not change the game on the console.",howTitle:"Three simple steps",howSub:"Have one compatible game file ready.",step1Title:"Choose a game",step1Body:"Select a .nes file you made or have permission to share.",step2Title:"We check the format",step2Body:"The page checks whether the file works with this cartridge.",step3Title:"Follow its progress",step3Body:"After submitting, see its place in the queue and the result here.",uploadTitle:"Submit a game file",uploadIntro:"Choose your file and confirm you are allowed to send it.",fileLabel:"Game file (.nes)",fileHint:"See “Which files work?” for the supported format.",rights:"I have permission to use and send this file.",submit:"Check file and join queue",jobTitle:"Your submission",bookmarkHint:"Save this page's URL to check the result later.",copyLink:"Copy status link",copied:"Status link copied.",copyFailed:"Could not copy. Please save the URL from your browser.",helpTitle:"Before you submit",compatibleTitle:"Which files work?",compatibleBody:"The current public version accepts some homebrew Famicom games in .nes files. Do not upload commercial game copies here.",afterTitle:"What happens next?",afterBody:"We check the file and keep compatible games waiting until the operator sends the next one. Delivery to the physical cartridge is still being developed.",privacyTitle:"Is my file kept?",privacyBody:"Your file is stored privately and deleted after about one hour. No sign-in is needed.",technicalTitle:"Show exact file requirements",technicalBody:"iNES format, Mapper 0 (NROM), 16 or 32 KiB PRG, and 8 KiB CHR. CHR-RAM, NES 2.0, and four-screen mirroring are not supported.",footerLine:"Open hardware experiment · Keitark",sourceLink:"View the design and source",licenseLink:"Image license",staffLink:"Exhibition staff",validating:"Checking your file…",loading:"Checking your submission…",networkError:"Connection lost. We will try again in a few seconds.",errorGeneric:"We could not submit the file. Please try another file.",position:"Queue position: ",until:"Submission expires: ",waitingOperator:"Waiting for exhibition staff to send the next game.",releasedToCart:"The operator has sent this game. Automatic cartridge pickup is still being verified.",states:{queued:["In the queue","Waiting for exhibition staff to send the next game."],claimed:["Cartridge is checking","The cartridge has started receiving the file."],downloaded:["Transferring","The cartridge has received the file."],deferred:["Waiting for a safe moment","Waiting until the console can safely switch games."],installed:["Ready on the cartridge","The cartridge has installed the game. Follow the exhibition staff's instructions to use the console."],unchanged:["Already active","This game is already active on the cartridge."],failed:["Could not install","The cartridge could not process the file. Please ask the exhibition staff."],expired:["Time expired","The file was not processed in time and has been deleted."],cancelled:["Submission cancelled","The exhibition staff cancelled this submission."]},errors:{authorization_required:"Please confirm you have permission to send this file.",missing_file:"Choose a .nes file.",too_large:"This file is too large.",short_header:"We could not read this file.",bad_magic:"This is not a valid .nes file.",nes2_unsupported:"This file format is not supported yet (NES 2.0).",mapper_unsupported:"This game's mapping is not supported yet (Mapper 0 only).",prg_geometry:"The game data is outside the supported size.",chr_geometry:"The graphics data is outside the supported size (8 KiB CHR required).",four_screen_unsupported:"This screen mode is not supported yet.",length_mismatch:"The file length does not match its header.",duplicate:"This file has already been submitted.",cooldown:"Please wait a moment before submitting again.",queue_full:"The queue is full. Please try later.",upload_invalid:"We could not read the file.",not_found:"We could not find this submission link."}
      }
    };
    Object.assign(copy.ja,{uploadTitle:"自分のROMをアップロード",uploadIntro:"自作、または使用を許可された .nes ファイルを送って、プレイ待ちに追加します。",galleryConsent:"このゲームを公開ギャラリーに載せてもよい（任意）",galleryConsentDetail:"公開すると作品名が表示され、ほかの来場者もこのROMをプレイ待ちに追加できます。ファイル本体は公開ダウンロードされず、約1時間で削除されます。",galleryTitleLabel:"公開する作品名",galleryHeading:"公開ゲームから選ぶ",galleryIntro:"公開に同意された作品を選んで、プレイ待ちに追加できます。",galleryRefresh:"一覧を更新",galleryLoading:"ギャラリーを読み込み中…",galleryEmpty:"公開中のゲームはまだありません。自分のROMをアップロードするとき、公開を選ぶとここに表示されます。",galleryUnavailable:"ギャラリーを読み込めませんでした。少し待ってから更新してください。",gallerySelect:"このゲームをプレイ待ちに追加",galleryExpiry:"公開期限",galleryQueued:"プレイ待ちに追加しました。",galleryTitleInvalid:"公開する場合は作品名を1〜60文字で入力してください。",afterBody:"ファイルを検査し、管理者が次に進めると実機へ自動送信します。実機での動作は検証中です。",privacyBody:"通常は非公開で約1時間後に削除します。ギャラリーへの掲載は任意です。",noticeBody:"受付と順番待ちを試せます。管理者の承認後に自動で実機へ送る機能は、まだ実機での確認が済んでいません。アップロードだけではゲームは切り替わりません。"});
    Object.assign(copy.en,{uploadTitle:"Upload your own ROM",uploadIntro:"Send a .nes file you created or have permission to use and add it to the play queue.",galleryConsent:"List this game in the public gallery (optional)",galleryConsentDetail:"Its title will be shown and other visitors can add this ROM to the play queue. The ROM itself is not publicly downloadable and is deleted after about one hour.",galleryTitleLabel:"Public game title",galleryHeading:"Choose a public game",galleryIntro:"Pick a game its uploader agreed to share and add it to the play queue.",galleryRefresh:"Refresh list",galleryLoading:"Loading gallery…",galleryEmpty:"No games are public yet. You can opt in when uploading your own ROM.",galleryUnavailable:"Could not load the gallery. Please try refreshing in a moment.",gallerySelect:"Add to play queue",galleryExpiry:"Listed until",galleryQueued:"Added to the play queue.",galleryTitleInvalid:"Enter a public title of 1–60 characters.",afterBody:"We check the file; after operator approval, the cartridge should receive it automatically. Physical verification is still pending.",privacyBody:"Files are private by default and deleted after about one hour. Gallery listing is optional.",noticeBody:"Submission and the queue are available. Automatic transfer after operator approval has not yet been verified on a physical cartridge. Uploading alone does not switch the console game."});
    copy.ja.errors.gallery_title_invalid=copy.ja.galleryTitleInvalid;
    copy.en.errors.gallery_title_invalid=copy.en.galleryTitleInvalid;
    copy.ja.errors.gallery_not_found="このゲームの公開期限が切れました。一覧を更新してください。";
    copy.en.errors.gallery_not_found="This game is no longer listed. Please refresh the gallery.";
    const form=document.getElementById("upload"),button=document.getElementById("submitButton"),feedback=document.getElementById("feedback"),panel=document.getElementById("jobPanel"),stateEl=document.getElementById("jobState"),description=document.getElementById("jobDescription"),extra=document.getElementById("jobExtra");
    const galleryGrid=document.getElementById("galleryGrid"),galleryConsent=document.getElementById("galleryConsent"),galleryTitle=document.getElementById("galleryTitle");
    let lang="ja",timer=null,currentUrl="",lastJob=null,galleryItems=[];
    try { lang=localStorage.getItem("fc-game-drop-lang")==="en"?"en":"ja"; } catch (_) {}
    const requestedLang=new URL(location.href).searchParams.get("lang");
    if(requestedLang==="ja"||requestedLang==="en")lang=requestedLang;
    function t(key){return copy[lang][key]||key;}
    function showFeedback(message,error){feedback.textContent=message;feedback.className="feedback"+(error?" error":"");feedback.hidden=false;}
    function clearFeedback(){feedback.hidden=true;feedback.textContent="";delete feedback.dataset.key;delete feedback.dataset.errorCode;delete feedback.dataset.error;}
    function showError(code,fallback){feedback.dataset.key="";feedback.dataset.errorCode=code||"";feedback.dataset.error="true";showFeedback(copy[lang].errors[code]||fallback||t("errorGeneric"),true);}
    function renderJob(job){lastJob=job;panel.hidden=false;const state=copy[lang].states[job.state]||[job.state,t("errorGeneric")];stateEl.textContent=state[0];description.textContent=job.state==="queued"?t(job.dispatch_released?"releasedToCart":"waitingOperator"):state[1];const parts=[];if(Number.isInteger(job.queue_position))parts.push(t("position")+job.queue_position);if(job.expires_at){const date=new Date(job.expires_at);if(!Number.isNaN(date.getTime()))parts.push(t("until")+new Intl.DateTimeFormat(lang==="ja"?"ja-JP":"en-US",{hour:"2-digit",minute:"2-digit"}).format(date));}extra.textContent=parts.join(" · ");}
    function setLanguage(next){
      lang=next;
      document.documentElement.lang=next;
      document.title=t("title");
      document.querySelector('meta[name="description"]').content=t("metaDescription");
      document.querySelectorAll("[data-i18n]").forEach(el=>{el.textContent=t(el.dataset.i18n)});
      document.querySelectorAll("[data-i18n-alt]").forEach(el=>{el.alt=t(el.dataset.i18nAlt)});
      document.querySelectorAll("[data-i18n-aria]").forEach(el=>{el.setAttribute("aria-label",t(el.dataset.i18nAria))});
      const lines=next==="ja"?["ゲームを、","ファミコンへ。"]:["Your game,","on a Famicom."];
      document.getElementById("page-title").replaceChildren(document.createTextNode(lines[0]),document.createElement("br"),document.createTextNode(lines[1]));
      if(!document.getElementById("rom").files.length)document.getElementById("fileName").textContent=t("noFile");
      document.getElementById("langJa").setAttribute("aria-pressed",String(next==="ja"));
      document.getElementById("langEn").setAttribute("aria-pressed",String(next==="en"));
      try{localStorage.setItem("fc-game-drop-lang",next)}catch(_){}
      const url=new URL(location.href);
      url.searchParams.set("lang",next);
      history.replaceState(null,"",url.pathname+url.search+url.hash);
      if(lastJob)renderJob(lastJob);
      renderGallery(galleryItems);
      if(feedback.dataset.errorCode)showError(feedback.dataset.errorCode);
      else if(feedback.dataset.key)showFeedback(t(feedback.dataset.key),feedback.dataset.error==="true");
    }
    document.getElementById("langJa").addEventListener("click",()=>setLanguage("ja"));document.getElementById("langEn").addEventListener("click",()=>setLanguage("en"));setLanguage(lang);
    document.getElementById("rom").addEventListener("change",event=>{document.getElementById("fileName").textContent=event.target.files[0]?.name||t("noFile");});
    galleryConsent.addEventListener("change",()=>{galleryTitle.disabled=!galleryConsent.checked;galleryTitle.required=galleryConsent.checked;if(!galleryConsent.checked)galleryTitle.value="";});
    function notice(key,error){feedback.dataset.key=key;delete feedback.dataset.errorCode;feedback.dataset.error=String(Boolean(error));showFeedback(t(key),error);}
    function renderGallery(items){
      galleryGrid.replaceChildren();
      if(!items.length){const empty=document.createElement("p");empty.className="gallery-empty";empty.textContent=t("galleryEmpty");galleryGrid.append(empty);return;}
      for(const item of items){
        const card=document.createElement("article");card.className="gallery-item";
        const title=document.createElement("h3");title.textContent=item.title;
        const detail=document.createElement("p");const expiry=new Date(item.expires_at);detail.textContent="Mapper "+item.mapper+" · "+item.prg_kib+" KiB PRG · "+t("galleryExpiry")+" "+new Intl.DateTimeFormat(lang==="ja"?"ja-JP":"en-US",{hour:"2-digit",minute:"2-digit"}).format(expiry);
        const select=document.createElement("button");select.type="button";select.textContent=t("gallerySelect");
        select.addEventListener("click",async()=>{select.disabled=true;try{const response=await fetch("/api/public/gallery/"+encodeURIComponent(item.id)+"/queue",{method:"POST",headers:{"X-RV-Queue":"true"}});const data=await response.json();if(!response.ok){const error=new Error(data.message||t("errorGeneric"));error.code=data.error;throw error;}const url=new URL(location.href);url.hash="job="+data.status_token;history.replaceState(null,"",url.pathname+url.search+url.hash);notice("galleryQueued",false);await poll(data.status_url);document.getElementById("jobPanel").scrollIntoView({behavior:"smooth",block:"nearest"});}catch(error){showError(error.code,error.message);if(error.code==="gallery_not_found")loadGallery();}finally{select.disabled=false;}});
        card.append(title,detail,select);galleryGrid.append(card);
      }
    }
    async function loadGallery(){try{const response=await fetch("/api/public/gallery",{cache:"no-store"});if(!response.ok)throw new Error();const data=await response.json();galleryItems=Array.isArray(data.items)?data.items:[];renderGallery(galleryItems);}catch(_){galleryGrid.replaceChildren();const message=document.createElement("p");message.className="gallery-empty";message.textContent=t("galleryUnavailable");galleryGrid.append(message);}}
    document.getElementById("galleryRefresh").addEventListener("click",loadGallery);
    async function poll(url){clearTimeout(timer);currentUrl=url;try{const response=await fetch(url,{cache:"no-store"});const job=await response.json();if(currentUrl!==url)return;if(!response.ok){showError(job.error);if(response.status!==404)timer=setTimeout(()=>poll(url),10000);return;}clearFeedback();renderJob(job);if(!["installed","unchanged","failed","expired","cancelled"].includes(job.state))timer=setTimeout(()=>poll(url),5000);}catch(_){if(currentUrl!==url)return;notice("networkError",true);timer=setTimeout(()=>poll(url),10000);}}
    form.addEventListener("submit",async event=>{event.preventDefault();button.disabled=true;notice("validating",false);try{const response=await fetch("/api/public/jobs",{method:"POST",body:new FormData(form)});const data=await response.json();if(!response.ok){const error=new Error(data.message||t("errorGeneric"));error.code=data.error;throw error;}const url=new URL(location.href);url.hash="job="+data.status_token;history.replaceState(null,"",url.pathname+url.search+url.hash);await poll(data.status_url);if(data.gallery_published)await loadGallery();}catch(error){showError(error.code,error.message);}finally{button.disabled=false;}});
    document.getElementById("copyLink").addEventListener("click",async()=>{try{await navigator.clipboard.writeText(location.href);notice("copied",false)}catch(_){notice("copyFailed",true)}});
    const token=location.hash.startsWith("#job=")?location.hash.slice(5):"";if(/^[A-Za-z0-9_-]{24,96}$/.test(token)){notice("loading",false);poll("/api/public/jobs/"+token);}
    loadGallery();
  </script>
</body>
</html>`;
