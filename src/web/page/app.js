// SPDX-License-Identifier: GPL-3.0-only
// The host's web page (src/web/web_server.cpp serves it and the API under /api). Plain JavaScript, no build step.
// Every value from the host goes into the page as text (textContent), never as HTML.
'use strict';

const $ = (id) => document.getElementById(id);
let currentTab = 'overview';
let pollTimer = null;

class HttpError extends Error {
  constructor(status, body) {
    super((body && body.error) || `HTTP ${status}`);
    this.status = status;
    this.body = body;
  }
}

// The header tells the host the request comes from this page: another site can't send it without asking first.
async function api(path, body) {
  const options = { credentials: 'same-origin', headers: { 'X-Prospero': '1' } };
  if (body !== undefined) {
    options.method = 'POST';
    options.headers['Content-Type'] = 'application/json';
    options.body = JSON.stringify(body);
  }
  const response = await fetch(path, options);
  const type = response.headers.get('Content-Type') || '';
  const data = type.includes('json') ? await response.json() : await response.text();
  if (response.status === 401 && path !== '/api/login') {
    showLogin();
    throw new HttpError(401, data);
  }
  if (!response.ok) throw new HttpError(response.status, data);
  return data;
}

function el(tag, props = {}, ...children) {
  const node = document.createElement(tag);
  for (const [key, value] of Object.entries(props)) {
    if (key === 'class') node.className = value;
    else if (key === 'text') node.textContent = value;
    else if (key.startsWith('on')) node.addEventListener(key.slice(2), value);
    else node.setAttribute(key, value);
  }
  for (const child of children) if (child != null) node.append(child);
  return node;
}

let noticeTimer = null;
// Shown at the top of the window, wherever the page is scrolled to; a tap closes it.
function notice(text, bad = false, sticky = false) {
  const box = $('notice');
  box.textContent = text;
  box.classList.toggle('bad', bad);
  box.hidden = false;
  clearTimeout(noticeTimer);
  if (!sticky) noticeTimer = setTimeout(() => { box.hidden = true; }, bad ? 8000 : 5000);
}
$('notice').addEventListener('click', () => { $('notice').hidden = true; });

function failed(error) {
  if (error instanceof HttpError && error.status === 401) return;
  notice(error.message || String(error), true);
}

// Login.
function showLogin() {
  stopPolling();
  $('app').hidden = true;
  $('login').hidden = false;
  $('login-user').focus();
}

function showApp() {
  $('login').hidden = true;
  $('app').hidden = false;
  selectTab(currentTab);
}

$('login-form').addEventListener('submit', async (event) => {
  event.preventDefault();
  const error = $('login-error');
  error.textContent = '';
  const button = event.submitter || $('login-form').querySelector('button');
  button.disabled = true;
  try {
    await api('/api/login', { user: $('login-user').value, password: $('login-password').value,
      remember: $('login-remember').checked });
    $('login-password').value = '';
    showApp();
    api('/api/session').then((s) => showPending(s.settings_pending), () => {});
  } catch (e) {
    error.textContent = e.body && e.body.wait ? `Too many failed logins. Try again in ${e.body.wait} s.` : e.message;
  } finally {
    button.disabled = false;
  }
});

$('logout').addEventListener('click', async () => {
  try { await api('/api/logout', {}); } catch (e) { /* signed out anyway */ }
  showLogin();
});

// Tabs.
function selectTab(name) {
  currentTab = name;
  for (const button of document.querySelectorAll('.tabs button')) {
    button.setAttribute('aria-selected', String(button.dataset.tab === name));
  }
  for (const section of document.querySelectorAll('.tab')) section.hidden = section.id !== `tab-${name}`;
  stopPolling();
  if (name === 'overview' || name === 'devices') startPolling();
  else if (name === 'settings') loadSettings();
  else if (name === 'logs') loadLog();
}

for (const button of document.querySelectorAll('.tabs button')) {
  button.addEventListener('click', () => selectTab(button.dataset.tab));
}

function startPolling() {
  refresh();
  pollTimer = setInterval(refresh, 2000);
}

function stopPolling() {
  clearInterval(pollTimer);
  pollTimer = null;
}

// Overview and devices: one request gives both.
let refreshing = false;
async function refresh() {
  if (refreshing || document.hidden) return;
  refreshing = true;
  try {
    const data = await api('/api/overview');
    renderHost(data.host, data.status);
    showPending(data.host.settings_pending);
    renderStatus(data.host, data.status);
    renderPairings(data.pairings);
    renderStreams(data.streams);
    renderDevices(data.devices, data.streams);
  } catch (e) {
    failed(e);
  } finally {
    refreshing = false;
  }
}

function renderHost(host, status) {
  $('host-name').textContent = host.name || 'ProsperoSunshine';
  $('host-facts').textContent = `ProsperoSunshine ${host.version} · firmware ${host.firmware}`;
  document.title = `${host.name || 'ProsperoSunshine'} · ProsperoSunshine`;
}

function renderStatus(host, status) {
  const facts = [
    ['Stream', status.running === '1' ? `running${status.client ? ` to ${status.client}` : ''}` : 'none'],
    ['App', status.app || '–'],
    ['Pending pairings', status.pending || '0'],
    ['Version', `${host.version} (${host.commit})`],
    ['Firmware', host.firmware],
    ['Base port', String(host.port)],
  ];
  $('status').replaceChildren(...facts.flatMap(([key, value]) => [el('dt', { text: key }), el('dd', { text: value })]));
}

function renderPairings(pairings) {
  const list = $('pairings');
  if (!pairings.length) {
    list.replaceChildren(el('li', { class: 'empty', text: 'No device is waiting for a PIN.' }));
    return;
  }
  list.replaceChildren(...pairings.map((p) => el('li', {},
    el('div', { class: 'grow' },
      el('div', { class: 'title', text: p.name || 'Moonlight' }),
      el('div', { class: 'sub', text: p.address || '' })),
    el('span', { class: 'badge', text: 'waiting for PIN' }),
    el('button', { class: 'danger', text: 'Deny', onclick: () => denyPairing(p) }))));
  // With several waiting, the PIN goes to the one picked here.
  if (pairings.length > 1) {
    $('pin-form').dataset.ids = JSON.stringify(pairings.map((p) => p.id));
  } else {
    delete $('pin-form').dataset.ids;
  }
}

function renderStreams(streams) {
  const list = $('streams');
  if (!streams.length) {
    list.replaceChildren(el('li', { class: 'empty', text: 'Nothing is streaming.' }));
    return;
  }
  list.replaceChildren(...streams.map((s) => {
    const parts = [s.address, s.codec, s.hdr === '1' ? 'HDR' : null, s.mode, s.kbps !== '-' ? `${s.kbps} kbps` : null,
      sending(s), `${s.pads} pad${s.pads === '1' ? '' : 's'}`, duration(s.secs)].filter(Boolean);
    return el('li', {},
      el('div', { class: 'grow' },
        el('div', { class: 'title', text: s.name || s.address }),
        el('div', { class: 'sub' }, ...parts.flatMap((part, i) => (i ? [' · ', part] : [part])), latency(s.rtt_ms))),
      el('span', { class: 'badge live', text: s.role === 'joined' ? 'joined' : 'live' }),
      s.uuid ? el('button', { class: 'danger', text: 'End stream', onclick: () => endStream(s) }) : null);
  }));
}

// The round trip, coloured as cloud gaming services rate it: below 40 ms good, up to 80 ms playable, above that poor.
function latency(rtt) {
  const ms = Number(rtt);
  if (!rtt || rtt === '-' || !Number.isFinite(ms)) return null;
  const rating = ms < 40 ? 'good' : ms <= 80 ? 'fair' : 'poor';
  return el('span', { class: `latency ${rating}`, title: 'Round trip to the device', text: ` · ${ms} ms` });
}

// What goes out against what the stream asked for: from 85 % good, from 60 % fair, below that poor. No colour in the first
// seconds, while the rate is still settling, or without both values.
function sending(s) {
  if (!s.sent_kbps || s.sent_kbps === '-') return null;
  const sent = Number(s.sent_kbps), asked = Number(s.kbps);
  const text = `sending ${s.sent_kbps} kbps`;
  if (!Number.isFinite(sent) || !(asked > 0) || s.role === 'starting' || !(Number(s.secs) >= 10)) return text;
  const share = sent / asked;
  const rating = share >= 0.85 ? 'good' : share >= 0.6 ? 'fair' : 'poor';
  return el('span', { class: `rate ${rating}`, text,
    title: `${Math.round(share * 100)} % of the ${asked} kbps asked for. A still picture needs less, so a low share there is no fault.` });
}

function duration(secs) {
  const total = Number(secs) || 0;
  const h = Math.floor(total / 3600), m = Math.floor((total % 3600) / 60), s = total % 60;
  return h ? `${h} h ${m} min` : m ? `${m} min ${s} s` : `${s} s`;
}

function renderDevices(devices, streams = []) {
  const list = $('devices');
  if (!devices.length) {
    list.replaceChildren(el('li', { class: 'empty', text: 'No device is paired yet.' }));
    return;
  }
  // A rename in progress keeps its field while the list refreshes.
  if (list.querySelector('input:focus')) return;
  list.replaceChildren(...devices.map((d) => {
    const stream = streams.find((s) => s.uuid === d.uuid);
    return el('li', {},
      el('div', { class: 'grow' },
        el('div', { class: 'title', text: d.name || d.uuid }),
        el('div', { class: 'sub', text: d.mode ? `last mode ${d.mode}` : 'not streamed yet' })),
      d.streaming === '1' ? el('span', { class: 'badge live', text: 'streaming' }) : null,
      d.streaming === '1' ? el('button', { class: 'danger', text: 'End stream',
        onclick: () => endStream(stream || { uuid: d.uuid, name: d.name }) }) : null,
      el('button', { text: 'Rename', onclick: () => rename(d) }),
      el('button', { class: 'danger', text: 'Unpair', onclick: () => unpair(d) }));
  }));
}

// Ending a stream lets the device connect again right away; unpairing as well keeps it out until it pairs with a PIN.
// One dialog with three answers: 'unpair', 'end', or null for Cancel (also Esc).
function askEndStream(who) {
  const dialog = $('end-dialog');
  const question = `End the stream to ${who}? The game and other streams go on.`;
  if (typeof dialog.showModal !== 'function') {  // a browser without <dialog>
    if (!confirm(question)) return Promise.resolve(null);
    return Promise.resolve(confirm(`Also unpair ${who}, so it can't connect again without a new PIN?\n\nOK: end and unpair. Cancel: only end the stream.`) ? 'unpair' : 'end');
  }
  $('end-text').textContent = question;
  dialog.returnValue = '';
  return new Promise((resolve) => {
    dialog.addEventListener('close', () => resolve(dialog.returnValue || null), { once: true });
    dialog.showModal();
    $('end-only').focus();
  });
}

async function endStream(stream) {
  const who = stream.name || stream.address || 'this device';
  const choice = await askEndStream(who);
  if (!choice) return;
  const alsoUnpair = choice === 'unpair';
  try {
    if (alsoUnpair) {
      await api('/api/devices/unpair', { uuid: stream.uuid });
      notice('The stream was ended and the device unpaired.');
    } else {
      await api('/api/streams/end', { uuid: stream.uuid });
      notice('The stream was ended.');
    }
    refresh();
  } catch (e) { failed(e); }
}

async function denyPairing(pairing) {
  if (!confirm(`Deny the pairing request from ${pairing.name || 'Moonlight'}${pairing.address ? ` (${pairing.address})` : ''}?`)) return;
  try {
    await api('/api/pin/cancel', { id: pairing.id });
    notice('The pairing request was denied.');
    refresh();
  } catch (e) { failed(e); }
}

async function rename(device) {
  const name = prompt('New name for this device:', device.name);
  if (name === null || !name.trim() || name.trim() === device.name) return;
  try {
    await api('/api/devices/rename', { uuid: device.uuid, name: name.trim() });
    notice('The device was renamed.');
    refresh();
  } catch (e) { failed(e); }
}

async function unpair(device) {
  if (!confirm(`Unpair ${device.name || device.uuid}? A running stream of it ends, and it has to pair again with a PIN to stream.`)) return;
  try {
    await api('/api/devices/unpair', { uuid: device.uuid });
    notice('The device was unpaired.');
    refresh();
  } catch (e) { failed(e); }
}

$('pin-form').addEventListener('submit', async (event) => {
  event.preventDefault();
  const body = { pin: $('pin').value.trim(), name: $('pin-name').value.trim() };
  const ids = $('pin-form').dataset.ids ? JSON.parse($('pin-form').dataset.ids) : null;
  if (ids) {
    const index = Number(prompt(`${ids.length} devices are waiting. Which one (1 to ${ids.length}, in the list's order)?`, '1'));
    if (!(index >= 1 && index <= ids.length)) return;
    body.id = ids[index - 1];
  }
  try {
    await api('/api/pin', body);
    $('pin').value = '';
    $('pin-name').value = '';
    notice('Paired.');
    refresh();
  } catch (e) { failed(e); }
});

// Settings.
let settings = [];
async function loadSettings() {
  try {
    settings = (await api('/api/settings')).options;
    $('settings').replaceChildren(...settings.map(settingRow));
  } catch (e) { failed(e); }
}

function settingRow(option) {
  const id = `setting-${option.key}`;
  let control;
  if (option.kind === 'toggle') {
    control = el('input', { id, type: 'checkbox' });
    control.checked = option.value === '1';
  } else if (option.kind === 'choice') {
    control = el('select', { id },
      el('option', { value: '', text: `Default (${option.default})` }),
      ...option.choices.map((c) => el('option', { value: c.value, text: c.label })));
    // A value written by hand that the list doesn't offer stays as it is unless changed here.
    if (option.value != null && !option.choices.some((c) => c.value === option.value)) {
      control.append(el('option', { value: option.value, text: `${option.value} (set by hand)` }));
    }
    control.value = option.value ?? '';
  } else if (option.kind === 'number') {
    control = el('input', { id, type: 'number', min: option.min, max: option.max, step: option.step,
      placeholder: `Default: ${option.default}` });
    control.value = option.value ?? '';
  } else {
    control = el('input', { id, maxlength: '63', placeholder: `Default: ${option.default}` });
    control.value = option.value ?? '';
  }
  return el('div', { class: 'setting' },
    el('label', { for: id, class: 'title', text: option.label }),
    el('div', { class: 'help', text: option.help }),
    el('div', { class: 'control' }, control,
      option.kind === 'toggle' ? el('span', { class: 'default', text: `Default: ${option.default}` }) : null));
}

function settingValues() {
  const values = {};
  for (const option of settings) {
    const control = $(`setting-${option.key}`);
    if (option.kind === 'toggle') values[option.key] = control.checked ? '1' : null;
    else values[option.key] = control.value.trim() === '' ? null : control.value.trim();
  }
  return values;
}

async function saveSettings(restartAfter) {
  try {
    await api('/api/settings', { values: settingValues() });
    showPending(true);
    if (restartAfter || confirm('Saved. The host reads its settings when it starts.\n\nRestart the host now to apply them? Running streams end.')) {
      await restartHost(true);
    } else {
      notice('Saved. The settings apply when the host starts again.');
    }
  } catch (e) { failed(e); }
}

// The banner stays while sunshine.conf differs from what the running host read at its start.
function showPending(pending) {
  $('pending').hidden = !pending;
}
$('pending-restart').addEventListener('click', () => restartHost(false));

$('settings-form').addEventListener('submit', (event) => {
  event.preventDefault();
  saveSettings(false);
});
$('save-restart').addEventListener('click', () => saveSettings(true));

// Restart: the host hands a small helper to the ELF loader and stops; once it is gone, the helper starts it again.
async function restartHost(asked) {
  if (!asked && !confirm('Restart the host? Running streams end.')) return;
  try {
    await api('/api/restart', {});
  } catch (e) { failed(e); return; }
  stopPolling();
  notice('The host is restarting…', false, true);
  const started = Date.now();
  await new Promise((resolve) => setTimeout(resolve, 6000));
  while (Date.now() - started < 60000) {
    try {
      await api('/api/session');
      break;
    } catch (e) {
      if (e instanceof HttpError && e.status === 401) return;  // back, with the sessions of the old run gone
      await new Promise((resolve) => setTimeout(resolve, 2000));
    }
  }
  notice('The host is back.');
  selectTab(currentTab);
}
$('restart').addEventListener('click', () => restartHost(false));

// Logs.
async function loadLog() {
  const file = $('log-file').value;
  const path = file === 'report' ? '/api/diagnostics' : `/api/logs?file=${file}`;
  $('log-download').href = path;
  $('log-download').download = { report: 'diagnostics.txt', diag: 'host_diag.log' }[file] || 'sunshine.log';
  $('report-hint').hidden = file !== 'report';
  try {
    const text = await api(path);
    const box = $('log');
    box.textContent = text || '(empty)';
    box.scrollTop = box.scrollHeight;
  } catch (e) { failed(e); }
}
$('log-file').addEventListener('change', loadLog);
$('log-refresh').addEventListener('click', loadLog);

document.addEventListener('visibilitychange', () => { if (!document.hidden && pollTimer) refresh(); });

// Start: signed in already (a cookie from before), or the login.
api('/api/session').then((s) => { showApp(); showPending(s.settings_pending); },
  (e) => { if (!(e instanceof HttpError && e.status === 401)) showLogin(); });
