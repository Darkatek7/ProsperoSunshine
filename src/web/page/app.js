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
function notice(text, bad = false) {
  const box = $('notice');
  box.textContent = text;
  box.classList.toggle('bad', bad);
  box.hidden = false;
  clearTimeout(noticeTimer);
  noticeTimer = setTimeout(() => { box.hidden = true; }, bad ? 8000 : 4000);
}

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
    await api('/api/login', { user: $('login-user').value, password: $('login-password').value });
    $('login-password').value = '';
    showApp();
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
    el('span', { class: 'badge', text: 'waiting for PIN' }))));
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
    const parts = [s.codec, s.hdr === '1' ? 'HDR' : null, s.mode, s.kbps !== '-' ? `${s.kbps} kbps` : null,
      s.sent_kbps && s.sent_kbps !== '-' ? `sending ${s.sent_kbps} kbps` : null,
      s.rtt_ms && s.rtt_ms !== '-' ? `${s.rtt_ms} ms` : null, `${s.pads} pad${s.pads === '1' ? '' : 's'}`,
      duration(s.secs)].filter(Boolean);
    return el('li', {},
      el('div', { class: 'grow' },
        el('div', { class: 'title', text: s.name || s.address }),
        el('div', { class: 'sub', text: `${s.address} · ${parts.join(' · ')}` })),
      el('span', { class: 'badge live', text: s.role === 'joined' ? 'joined' : 'live' }),
      s.uuid ? el('button', { class: 'danger', text: 'End stream', onclick: () => endStream(s) }) : null);
  }));
}

function duration(secs) {
  const total = Number(secs) || 0;
  const h = Math.floor(total / 3600), m = Math.floor((total % 3600) / 60), s = total % 60;
  return h ? `${h} h ${m} min` : m ? `${m} min ${s} s` : `${s} s`;
}

function renderDevices(devices) {
  const list = $('devices');
  if (!devices.length) {
    list.replaceChildren(el('li', { class: 'empty', text: 'No device is paired yet.' }));
    return;
  }
  // A rename in progress keeps its field while the list refreshes.
  if (list.querySelector('input:focus')) return;
  list.replaceChildren(...devices.map((d) => el('li', {},
    el('div', { class: 'grow' },
      el('div', { class: 'title', text: d.name || d.uuid }),
      el('div', { class: 'sub', text: d.mode ? `last mode ${d.mode}` : 'not streamed yet' })),
    d.streaming === '1' ? el('span', { class: 'badge live', text: 'streaming' }) : null,
    el('button', { text: 'Rename', onclick: () => rename(d) }),
    el('button', { class: 'danger', text: 'Unpair', onclick: () => unpair(d) }))));
}

async function endStream(stream) {
  if (!confirm(`End the stream to ${stream.name || stream.address}? The game and other streams go on.`)) return;
  try {
    await api('/api/streams/end', { uuid: stream.uuid });
    notice('The stream was ended.');
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
  if (!confirm(`Unpair ${device.name || device.uuid}? It has to pair again with a PIN to stream.`)) return;
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
    if (restartAfter) await restartHost(true);
    else notice('Saved. The settings apply when the host starts again.');
  } catch (e) { failed(e); }
}

$('settings-form').addEventListener('submit', (event) => {
  event.preventDefault();
  saveSettings(false);
});
$('save-restart').addEventListener('click', () => saveSettings(true));

// Restart: the host stops, sends itself to the ELF loader again and comes back after some seconds.
async function restartHost(asked) {
  if (!asked && !confirm('Restart the host? Running streams end.')) return;
  try {
    await api('/api/restart', {});
  } catch (e) { failed(e); return; }
  stopPolling();
  notice('The host is restarting…');
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
api('/api/session').then(showApp, (e) => { if (!(e instanceof HttpError && e.status === 401)) showLogin(); });
