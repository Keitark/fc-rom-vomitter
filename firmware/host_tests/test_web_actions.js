// SPDX-License-Identifier: MIT
const assert = require('node:assert/strict');
const fs = require('node:fs');
const path = require('node:path');
const test = require('node:test');
const vm = require('node:vm');

const source = fs.readFileSync(path.join(__dirname, '../main/web_server.c'), 'utf8');
const htmlBlock = source.match(/static const char INDEX_HTML\[\] =([\s\S]*?);\s*\n/)[1];
const html = [...htmlBlock.matchAll(/"(?:\\.|[^"\\])*"/g)]
    .map(match => JSON.parse(match[0])).join('');
const script = html.match(/<script>([\s\S]*?)<\/script>/)[1];

function createPage() {
    const nodes = {
        '#status': { textContent: '' },
        '#file': { files: [{ name: 'demo.nes' }] },
        '#upload': { disabled: false },
        '#nesdemo': { disabled: false },
        '#nesrf': { disabled: false },
    };
    const requests = [];
    const timers = new Map();
    const intervals = [];
    const alerts = [];
    const confirmations = [];
    let timerId = 0;
    let confirmResult = true;
    const context = {
        AbortController,
        document: { querySelector: selector => nodes[selector] },
        alert: message => alerts.push(message),
        confirm: message => { confirmations.push(message); return confirmResult; },
        setTimeout: (callback, delay) => {
            const id = ++timerId;
            timers.set(id, { callback, delay });
            return id;
        },
        clearTimeout: id => timers.delete(id),
        setInterval: callback => intervals.push(callback),
        fetch: (url, options = {}) => new Promise((resolve, reject) => {
            requests.push({ url, options, resolve, reject });
            options.signal?.addEventListener('abort', () => {
                const error = new Error('aborted');
                error.name = 'AbortError';
                reject(error);
            }, { once: true });
        }),
    };
    vm.runInNewContext(script, context);
    return {
        nodes, requests, timers, alerts, confirmations,
        setConfirm: value => { confirmResult = value; },
        poll: () => intervals[0](),
        click: id => nodes['#' + id].onclick(),
        expire: delay => {
            const timer = [...timers.values()].find(value => value.delay === delay);
            assert.ok(timer, 'expected request timeout');
            timer.callback();
        },
    };
}

function response(ok = true, text = 'OK', data = { mode: 'ready' }) {
    return { ok, status: ok ? 200 : 500, text: async () => text, json: async () => data };
}

async function flush() {
    await new Promise(resolve => setImmediate(resolve));
}

async function readyPage() {
    const page = createPage();
    page.requests[0].resolve(response());
    await flush();
    assert.equal(page.timers.size, 0);
    return page;
}

function assertControls(page, disabled) {
    for (const id of ['upload', 'nesdemo', 'nesrf']) {
        assert.equal(page.nodes['#' + id].disabled, disabled, id);
    }
}

test('pending POST disables actions and blocks duplicate POSTs and status polls', async () => {
    const page = await readyPage();
    const operation = page.click('upload');
    assertControls(page, true);
    assert.equal(page.nodes['#status'].textContent, 'Uploading and verifying ROM...');
    assert.equal(page.requests[1].url, '/api/upload');
    assert.equal(page.requests[1].options.body, page.nodes['#file'].files[0]);
    for (const id of ['upload', 'nesdemo', 'nesrf']) await page.click(id);
    await page.poll();
    assert.equal(page.requests.length, 2);
    assert.equal(page.confirmations.length, 1);
    page.requests[1].reject(new Error('network lost'));
    await operation;
    assertControls(page, false);
    assert.equal(page.timers.size, 0);
    assert.match(page.nodes['#status'].textContent, /Request failed: network lost/);
    assert.equal(page.requests.length, 2, 'failure must wait for the scheduled poll');
    const poll = page.poll();
    assert.equal(page.requests[2].url, '/api/status');
    page.requests[2].resolve(response());
    await poll;
});

test('successful normal actions immediately resume status polling', async () => {
    for (const id of ['upload', 'nesdemo']) {
        const page = await readyPage();
        const operation = page.click(id);
        page.requests[1].resolve(response());
        await flush();
        assertControls(page, false);
        assert.equal(page.requests[2].url, '/api/status');
        page.requests[2].resolve(response(true, 'OK', { sequence: 12 }));
        await operation;
        assert.match(page.nodes['#status'].textContent, /"sequence": 12/);
        assert.equal(page.timers.size, 0);
    }
});

test('all POST timeouts abort and restore normal controls', async () => {
    for (const [id, timeout] of [['upload', 45000], ['nesdemo', 15000], ['nesrf', 15000]]) {
        const page = await readyPage();
        const operation = page.click(id);
        page.expire(timeout);
        await operation;
        assert.equal(page.requests[1].options.signal.aborted, true);
        assertControls(page, false);
        assert.equal(page.timers.size, 0);
        assert.match(page.nodes['#status'].textContent, /Request timed out/);
        assert.equal(page.requests.length, 2);
    }
});

test('failed SDR request shows server error and allows another normal action', async () => {
    const page = await readyPage();
    const operation = page.click('nesrf');
    page.requests[1].resolve(response(false, 'Console power is off'));
    await operation;
    assertControls(page, false);
    assert.deepEqual(page.alerts, ['Console power is off']);
    assert.match(page.nodes['#status'].textContent, /Console power is off/);
    assert.equal(page.timers.size, 0);
    const retry = page.click('nesdemo');
    assert.equal(page.requests[2].url, '/api/nes-sdr/demo-frame');
    page.requests[2].reject(new Error('disconnected'));
    await retry;
});

test('accepted SDR leaves controls disabled and polls suspended', async () => {
    const page = await readyPage();
    const operation = page.click('nesrf');
    page.requests[1].resolve(response());
    await operation;
    assertControls(page, true);
    assert.equal(page.timers.size, 0);
    assert.equal(page.nodes['#status'].textContent, 'SDR mode starting; Wi-Fi will disconnect.');
    await page.poll();
    for (const id of ['upload', 'nesdemo', 'nesrf']) await page.click(id);
    assert.equal(page.requests.length, 2);
});

test('status already in progress cannot overwrite a later action or action failure', async () => {
    const page = createPage();
    const operation = page.click('upload');
    page.requests[1].reject(new Error('network lost'));
    await operation;
    page.requests[0].resolve(response(true, 'OK', { message: 'old status' }));
    await flush();
    assert.match(page.nodes['#status'].textContent, /Request failed: network lost/);
    assert.equal(page.timers.size, 0);
});

test('file validation and cancelled confirmations do not start a POST', async () => {
    const page = await readyPage();
    page.nodes['#file'].files = [];
    await page.click('upload');
    assert.deepEqual(page.alerts, ['Choose a .nes file']);
    page.nodes['#file'].files = [{ name: 'demo.nes' }];
    page.setConfirm(false);
    for (const id of ['upload', 'nesdemo', 'nesrf']) await page.click(id);
    assert.equal(page.requests.length, 1);
    assertControls(page, false);
});
