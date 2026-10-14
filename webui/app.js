const $ = id => document.getElementById(id);
const names = ['Left A', 'Right A', 'Left B', 'Right B', 'Left C', 'Right C'];
// Edit status / notice strings shown in #message here.
const messages = {
  idle: 'Connect to slidrr-psoc',
  disconnected: 'Disconnected.',
  connecting: 'Connecting…',
  connected: 'Connected.',
  saving: 'Saving… UART and USB will pause briefly.',
  saved: 'Settings saved to Pico flash.',
  calibrationStarted: 'Calibration started. Keep all beams clear for two seconds.',
  calibrationComplete: 'Calibration complete. Review and save.',
  calibrationFailed: 'Calibration failed: check that all towers receive light.',
  brightnessApplied: 'Brightness applied. Save to keep it after restart.',
  sensorApplied: name => ``,
  invalidNumber: 'Enter whole numbers in the displayed ranges.',
  noWebSerial: 'Web Serial requires desktop Chrome or Edge on localhost or HTTPS.',
  noWebSerialShort: 'Web Serial requires desktop over localhost or HTTPS.',
  invalidStatus: 'The selected port sent an invalid configuration status.',
  noStatus: 'Handshake succeeded, but no configuration status arrived',
  busy: 'A command is already in progress or the controller is disconnected.',
  disconnectPending: 'Disconnected',
  connectionLost: detail => `Serial connection lost: ${detail}`,
  connectionFailed: detail => `Connection failed: ${detail}. Select slidrr Config Serial and flash matching firmware.`,
  incompatibleHello: line => `Incompatible serial response: ${line}`,
  noHandshake: line => `No Web Serial handshake; selected port sent: ${line}`,
  noReply: verb => `No reply to ${verb} on the selected serial port`,
};
// Swap the positions of Left A (channel 0) and Left B (channel 2), keeping
// each sensor's live reading, status and settings bound to its real channel.
const displayOrder = [2, 1, 0, 3, 4, 5];
const channels = $('channels');
channels.innerHTML = displayOrder.map(i => `<div class="sensor-row" data-channel="${i}" role="meter" aria-label="${names[i]} air sensor" aria-valuemin="0" aria-valuemax="4095" aria-valuenow="0"><span class="lead-line" aria-hidden="true"></span><strong class="raw">—</strong><span class="signal-line" aria-hidden="true"></span><span class="sensor-state visually-hidden">Waiting</span><input class="baseline" type="number" min="32" max="4095" step="1" aria-label="${names[i]} baseline, ADC 32 to 4095" placeholder="Baseline" disabled><input class="trigger" type="number" min="1" max="99" step="1" aria-label="${names[i]} trigger drop, 1 to 99 percent" placeholder="Trigger" disabled><button class="setSensor" disabled>Apply</button></div>`).join('');
let port, reader, writer, readLoop, pending, savedStatus;
let intentionalDisconnect = false;
let heartbeat;
let buffer = '';
let handshakeReply = '';
let observedSerialLine = '';
let lastStatusError = '';
const encoder = new TextEncoder(), decoder = new TextDecoder();
const notice = (message, type = '') => { $('message').textContent = message; $('message').className = type; };
notice(messages.idle);
function enable(connected) {
  $('connect').textContent = connected ? 'Disconnect' : 'Connect controller';
  $('connection').textContent = connected ? 'Connected' : 'Not connected';
  for (const element of document.querySelectorAll('input, button:not(#connect)')) element.disabled = !connected;
  if (connected) {
    $('save').disabled = !savedStatus || savedStatus.saved || savedStatus.calibration === 1;
    $('calibrate').disabled = !savedStatus || savedStatus.calibration === 1;
    for (const button of channels.querySelectorAll('.setSensor')) {
      button.disabled = !savedStatus || savedStatus.calibration === 1;
    }
  }
}
function parseStatus(line) {
  const parts = line.split(' ');
  if (parts.length !== 23 || parts[0] !== 'STATUS' || !parts.slice(1).every(x => /^\d+$/.test(x))) return null;
  const v = parts.slice(1).map(Number);
  if (v[0] > 1 || v[1] > 3 || v[2] > 100 || v[3] > 63 ||
    v.slice(4, 10).some(x => x > 4095) ||
    v.slice(10, 16).some(x => x < 32 || x > 4095) ||
    v.slice(16).some(x => x < 1 || x > 99)) return null;
  return { saved: !!v[0], calibration: v[1], brightness: v[2], blocked: v[3], raw: v.slice(4, 10), baseline: v.slice(10, 16), trigger: v.slice(16, 22) };
}
function showStatus(status) {
  const previous = savedStatus;
  savedStatus = status;
  $('saveState').textContent = status.saved ? 'Saved on Pico' : 'Unsaved changes';
  $('save').disabled = status.saved || status.calibration === 1;
  $('calibrate').disabled = status.calibration === 1;
  for (const button of channels.querySelectorAll('.setSensor')) button.disabled = status.calibration === 1;
  if (document.activeElement !== $('brightness') &&
    (previous?.brightness === undefined || previous.brightness !== status.brightness)) {
    $('brightness').value = status.brightness;
  }
  $('brightnessLabel').textContent = `${$('brightness').value}%`;
  for (let i = 0; i < 6; i++) {
    const row = channels.querySelector(`[data-channel="${i}"]`);
    row.querySelector('.raw').textContent = status.raw[i];
    const blocked = Boolean(status.blocked & (1 << i));
    row.classList.toggle('blocked', blocked);
    row.querySelector('.sensor-state').textContent = blocked ? 'Blocked' : 'Clear';
    row.setAttribute('aria-valuenow', String(status.raw[i]));
    row.setAttribute('aria-valuetext', `${status.raw[i]} ADC, ${blocked ? 'blocked' : 'clear'}`);
    for (const [field, value] of [['baseline', status.baseline[i]], ['trigger', status.trigger[i]]]) {
      const input = row.querySelector(`.${field}`);
      // Do not overwrite a draft just because live telemetry arrived. Only
      // reset it when the last value reported by the device actually changed.
      const previousValue = previous?.[field][i];
      if (document.activeElement !== input && (previousValue === undefined || previousValue !== value)) {
        input.value = value;
      }
    }
  }
  if (previous?.calibration === 1 && status.calibration !== 1)
    notice(status.calibration === 2 ? messages.calibrationComplete : messages.calibrationFailed, status.calibration === 2 ? 'success' : 'error');
}
function onLine(line) {
  if (!line) return;
  if (line.startsWith('STATUS ')) {
    const status = parseStatus(line);
    if (status) showStatus(status);
    else {
      lastStatusError = messages.invalidStatus;
      observedSerialLine = line.slice(0, 120);
    }
    return;
  }
  observedSerialLine = line.slice(0, 120);
  if (!pending || (!line.startsWith('OK ') && !line.startsWith('ERR '))) return;
  if (pending.verb === 'HELLO') {
    if (line !== 'OK HELLO 1') {
      handshakeReply = line;
      return;
    }
  } else if (!line.startsWith('ERR ') && line !== `OK ${pending.verb}`) {
    return;
  }
  const request = pending; pending = null;
  if (line.startsWith('OK ')) request.resolve(line);
  else request.reject(new Error(line.substring(4)));
}
async function consume() {
  try {
    while (reader) {
      const { value, done } = await reader.read();
      if (done) break;
      buffer += decoder.decode(value, { stream: true });
      if (buffer.length > 4096) buffer = '';
      let newline;
      while ((newline = buffer.indexOf('\n')) >= 0) {
        const line = buffer.slice(0, newline).trim();
        buffer = buffer.slice(newline + 1);
        onLine(line);
      }
    }
  } catch (error) {
    if (!intentionalDisconnect) notice(messages.connectionLost(error.message), 'error');
  } finally {
    if (!intentionalDisconnect && port) void disconnect();
  }
}
async function command(text) {
  if (!writer || pending) throw new Error(messages.busy);
  const result = new Promise((resolve, reject) => {
    let timeout;
    const request = { verb: text.split(' ')[0], resolve: value => { clearTimeout(timeout); resolve(value); }, reject: error => { clearTimeout(timeout); reject(error); } };
    pending = request;
    timeout = setTimeout(() => {
      if (pending !== request) return;
      pending = null;
      if (request.verb === 'HELLO' && handshakeReply) {
        reject(new Error(messages.incompatibleHello(handshakeReply)));
      } else if (request.verb === 'HELLO' && observedSerialLine) {
        reject(new Error(messages.noHandshake(observedSerialLine)));
      } else {
        reject(new Error(messages.noReply(request.verb)));
      }
    }, text === 'SAVE' ? 8000 : 2500);
  });
  try { await writer.write(encoder.encode(text + '\n')); }
  catch (error) { pending?.reject(error); pending = null; }
  return result;
}
async function disconnect() {
  intentionalDisconnect = true;
  clearInterval(heartbeat);
  const activeReader = reader, activeWriter = writer, activePort = port;
  reader = null; writer = null; port = null;
  if (pending) { pending.reject(new Error(messages.disconnectPending)); pending = null; }
  try { await activeReader?.cancel(); } catch { }
  try { await readLoop; } catch { }
  try { activeReader?.releaseLock(); } catch { }
  try { activeWriter?.releaseLock(); } catch { }
  try { await activePort?.close(); } catch { }
  savedStatus = null;
  enable(false);
  intentionalDisconnect = false;
}
$('connect').addEventListener('click', async () => {
  if (port) { await disconnect(); notice(messages.disconnected); return; }
  if (!navigator.serial) { notice(messages.noWebSerial, 'error'); return; }
  try {
    port = await navigator.serial.requestPort({ filters: [{ usbVendorId: 0x0ca3, usbProductId: 0x0021 }] });
    await port.open({ baudRate: 115200 });
    reader = port.readable.getReader(); writer = port.writable.getWriter();
    buffer = '';
    savedStatus = null;
    handshakeReply = '';
    observedSerialLine = '';
    lastStatusError = '';
    readLoop = consume();
    enable(true);
    notice(messages.connecting);
    await command('HELLO');
    if (!savedStatus) {
      await new Promise((resolve, reject) => {
        let attempts = 0;
        const check = setInterval(() => {
          if (savedStatus) { clearInterval(check); resolve(); }
          else if (++attempts >= 15) {
            clearInterval(check);
            reject(new Error(lastStatusError || messages.noStatus));
          }
        }, 100);
      });
    }
    heartbeat = setInterval(() => {
      if (writer && !pending) void command('HELLO').catch(() => { });
    }, 1000);
    notice(messages.connected, 'success');
  } catch (error) {
    await disconnect();
    notice(messages.connectionFailed(error.message), 'error');
  }
});
async function act(text, message) {
  try { await command(text); notice(message, 'success'); }
  catch (error) { notice(error.message, 'error'); }
}
function numberFrom(input, min, max) {
  const text = input.value.trim();
  if (!/^\d+$/.test(text)) return null;
  const value = Number(text);
  return Number.isInteger(value) && value >= min && value <= max ? value : null;
}
channels.addEventListener('click', async event => {
  const row = event.target.closest('.sensor-row');
  if (!row || !event.target.matches('.setSensor')) return;
  const index = Number(row.dataset.channel);
  const baseline = numberFrom(row.querySelector('.baseline'), 32, 4095);
  const trigger = numberFrom(row.querySelector('.trigger'), 1, 99);
  if (baseline === null || trigger === null) { notice(messages.invalidNumber, 'error'); return; }
  try {
    await command(`BASELINE ${index} ${baseline}`);
    await command(`TRIGGER ${index} ${trigger}`);
    notice(messages.sensorApplied(names[index]), 'success');
  } catch (error) {
    notice(error.message, 'error');
  }
});
$('brightness').addEventListener('input', () => $('brightnessLabel').textContent = `${$('brightness').value}%`);
$('setBrightness').addEventListener('click', () => act(`BRIGHTNESS ${$('brightness').value}`, messages.brightnessApplied));
$('calibrate').addEventListener('click', () => act('CALIBRATE', messages.calibrationStarted));
$('save').addEventListener('click', async () => {
  $('save').disabled = true;
  notice(messages.saving);
  await act('SAVE', messages.saved);
});
if (!navigator.serial) { $('connect').disabled = true; notice(messages.noWebSerialShort, 'error'); }
