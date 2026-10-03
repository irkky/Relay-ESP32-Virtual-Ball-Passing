(() => {
  'use strict';
  const $ = id => document.getElementById(id);
  let players = window.PLAYERS, snapshot = null, master = false, connected = false, busy = false, lastMessage = 0, requestedAt = 0, channel = 1;
  const cards = new Map();
  const name = id => players.find(p => p.id === id)?.name || 'No player';
  const text = (tag, className, value) => { const e = document.createElement(tag); e.className = className; e.textContent = value; return e; };
  function notice(message, error = false) { $('notice').textContent = message; $('notice').classList.toggle('error', error); }
  function log(message, kind = '') {
    $('events').querySelector('.empty-log')?.remove();
    const li = text('li', kind, ''); const stamp = document.createElement('time');
    stamp.dateTime = new Date().toISOString(); stamp.textContent = new Date().toLocaleTimeString([], { hour12: false });
    li.append(stamp, text('span', '', message)); $('events').prepend(li);
    while ($('events').children.length > 200) $('events').lastElementChild.remove();
  }
  function buildPlayers() {
    $('chain').replaceChildren(); $('master-node').replaceChildren(); $('starting-player').replaceChildren(); cards.clear();
    const slaves = players.filter(p => p.role === 'SLAVE');
    const ordered = []; const visited = new Set(); let next = slaves.find(p => !p.left);
    while (next && !visited.has(next.id)) { ordered.push(next); visited.add(next.id); next = slaves.find(p => p.id === next.right); }
    for (const p of slaves) if (!visited.has(p.id)) ordered.push(p);
    const masterPlayer = players.find(p => p.role === 'MASTER');
    for (const p of [masterPlayer, ...ordered].filter(Boolean)) {
      const card = text('article', 'player' + (p.role === 'MASTER' ? ' master-card' : ''), '');
      card.dataset.id = p.id;
      const head = text('div', 'player-head', '');
      const initials = p.name.split(/\s+/).slice(0, 2).map(n => n[0]).join('');
      head.append(text('span', 'avatar', p.role === 'MASTER' ? '↔' : initials));
      const title = document.createElement('div'); title.append(text('h3', '', p.name), text('div', 'role', `PLAYER ${String(p.id).padStart(2, '0')} / ${p.role}`)); head.append(title);
      const foot = text('div', 'player-foot', ''); const status = text('span', 'status', ''); status.append(document.createElement('i'), document.createTextNode('UNKNOWN'));
      const state = text('span', 'player-state', 'WAITING'); foot.append(status, state);
      card.append(text('span', 'ball-badge', '● BALL'), head, text('div', 'mac', p.mac), foot);
      cards.set(p.id, { card, status, state });
      if (p.role === 'MASTER') $('master-node').append(card);
      else {
        if ($('chain').childElementCount) { const link = text('div', 'chain-link', '↕'); link.dataset.left = p.left; link.dataset.right = p.id; $('chain').append(link); }
        $('chain').append(card); const option = document.createElement('option'); option.value = p.id; option.textContent = `${p.name} · Player ${p.id}`; $('starting-player').append(option);
      }
    }
    $('starting-player').value = '4';
  }
  const ago = ms => !ms || !snapshot ? '—' : `${Math.floor(((snapshot.uptimeMs - ms) >>> 0) / 1000)}s ago`;
  function render() {
    const fresh = connected && master && Date.now() - lastMessage < 4500;
    const s = fresh ? snapshot : null;
    $('connection').classList.toggle('connected', fresh);
    $('connection').replaceChildren(document.createElement('i'), document.createTextNode(fresh ? 'Master connected' : connected ? 'Waiting for Master' : 'Master disconnected'));
    $('connect').textContent = busy ? 'Connecting…' : connected ? 'Disconnect Master' : 'Connect Master ↗'; $('connect').disabled = busy;
    $('live').textContent = fresh ? '● LIVE' : '○ OFFLINE'; $('live').classList.toggle('online', fresh);
    const online = s?.players.filter(p => p.online === 'ONLINE').length || 0;
    const confirmed = s && s.state === 'ACTIVE' && !s.paused && s.players.some(p => p.id === s.currentPlayer && p.online === 'ONLINE');
    $('holder').textContent = confirmed ? name(s.currentPlayer) : s?.currentPlayer ? 'Transfer / state uncertain' : 'Waiting to play';
    $('holder-note').textContent = confirmed ? `Player ${s.currentPlayer} · Center, then move your joystick` : s?.currentPlayer ? `Designated holder: ${name(s.currentPlayer)}` : 'No confirmed ball holder';
    document.querySelector('.metric.current').classList.toggle('has-ball', Boolean(confirmed));
    $('online').textContent = online; $('online-note').textContent = s ? `${Math.max(0, online - 1)} of 7 slaves reachable` : 'Awaiting Master';
    $('game-status').textContent = s ? s.paused ? 'Needs attention' : ({ IDLE: 'Ready to start', RESETTING: 'Resetting', ACTIVE: 'In play', PASSING: 'Passing', GRANTING: 'Confirming', ERROR: 'Error' }[s.state] || s.state) : connected ? 'Connecting' : 'Disconnected';
    $('session').textContent = `Session ${s?.gameId || '—'} · Pass ${s ? Math.max(0, s.sequence - 1) : '—'}`;
    $('start').disabled = !s || s.state !== 'IDLE' || online !== 8;
    $('reset').disabled = !s; $('retry').disabled = !s; $('starting-player').disabled = !s || s.state !== 'IDLE';
    const awaiting = s?.players.filter(p => p.id !== 1 && !p.resetAck).map(p => name(p.id)) || [];
    $('reset-status').textContent = !s ? 'Waiting for a live Master connection.' : s.state === 'RESETTING' ? `Waiting for reset ACK: ${awaiting.join(', ') || 'completing…'}` : s.paused ? 'Retry repeats the same transfer. Reset clears the game after every slave acknowledges.' : s.state === 'IDLE' ? 'All players cleared. Select a player to start.' : 'Reset the current game before starting another.';
    $('espnow').textContent = s?.espnow ? `Initialized · channel ${channel}` : '—'; $('last-packet').textContent = s ? ago(s.lastPacketMs) : '—'; $('last-ack').textContent = s ? ago(s.lastAckMs) : '—';
    $('errors').textContent = s?.errors ?? '—'; $('radio-errors').textContent = s ? `${s.sendFailures} / ${s.invalidPackets}` : '—'; $('queue-drops').textContent = s?.queueDrops ?? '—';
    for (const [id, refs] of cards) {
      const status = s?.players.find(p => p.id === id); const availability = status?.online || 'UNKNOWN';
      const active = confirmed && id === s.currentPlayer;
      const pending = s && ['PASSING', 'GRANTING'].includes(s.state) && id === s.targetPlayer;
      refs.card.classList.toggle('active', Boolean(active)); refs.card.classList.toggle('pending', Boolean(pending));
      refs.card.classList.toggle('online', availability === 'ONLINE'); refs.card.classList.toggle('offline', availability === 'OFFLINE');
      refs.status.replaceChildren(document.createElement('i'), document.createTextNode(availability));
      refs.state.textContent = active ? 'ACTIVE · BALL' : pending ? 'PENDING' : id === 1 ? s?.state === 'ACTIVE' ? 'GAME LIVE' : s?.state || 'GATEWAY' : availability === 'OFFLINE' ? 'UNREACHABLE' : s?.state === 'RESETTING' ? status?.resetAck ? 'CLEARED' : 'RESET PENDING' : status?.state === 'ACTIVE' ? 'LAST REPORTED ACTIVE' : status?.state || 'WAITING';
    }
    document.querySelectorAll('.chain-link').forEach(link => link.classList.toggle('lit', Boolean(confirmed && (Number(link.dataset.left) === s.currentPlayer || Number(link.dataset.right) === s.currentPlayer))));
  }
  function receive(message) {
    if (message.type === 'CONFIG') {
      if (message.role !== 'MASTER' || message.deviceId !== 1) { notice('This is a slave board. Connect the USB cable to Ramya’s Master.', true); link.disconnect('Wrong board selected'); return; }
      master = true;
      channel = message.channel || 1;
      if (Array.isArray(message.players) && message.players.length === 8) { players = message.players; buildPlayers(); }
      notice('Master connected. The game uses the configured physical chain.'); log('Master connected · ESP-NOW gateway ready', 'success');
    } else if (message.type === 'GAME_STATE' && master && Array.isArray(message.players)) {
      snapshot = message; lastMessage = Date.now();
      if (message.paused) notice('Acknowledgement timed out. Retry the pending transfer, or reset after reconnecting every slave.', true);
      else if (message.state === 'IDLE') notice('All seven slaves acknowledged reset. Choose a starting player and press Start game.');
      else if (message.state === 'ACTIVE') notice(`${name(message.currentPlayer)} has the ball. Center the joystick, then move left or right.`);
      else if (message.state === 'RESETTING') notice('Clearing all seven slaves. A new game starts only after every reset acknowledgement.');
      else notice('Transfer in progress. Waiting for the receiver and Master to confirm.');
    } else if (message.type !== 'IDENTITY') {
      const route = message.from && message.to ? `${name(message.from)} → ${name(message.to)} · ` : '';
      log(route + (message.message || message.type), message.type === 'ERROR' ? 'error' : message.type === 'BALL_RECEIVED' ? 'success' : '');
      if (message.type === 'ERROR') notice(message.message, true);
      if (master && ['BALL_RECEIVED', 'BALL_PASS', 'RESET_COMPLETE'].includes(message.type)) send({ type: 'GET_STATUS' });
    }
    render();
  }
  const link = new RelaySerial.SerialLink(receive, reason => {
    connected = master = busy = false; snapshot = null; notice(reason); log(reason); render();
  }, message => log(message));
  async function send(message) {
    try { await link.send(message); } catch (error) { notice(`Serial write failed: ${error.message}`, true); await link.disconnect('Serial write failed'); }
  }
  $('connect').addEventListener('click', async () => {
    if (connected) { await link.disconnect(); return; }
    busy = true; render();
    try { await link.connect(); connected = true; requestedAt = 0; log('USB port opened. Waiting for Master identity…'); await send({ type: 'GET_CONFIG' }); }
    catch (error) { notice(error.name === 'NotFoundError' ? 'No serial port selected.' : error.message, error.name !== 'NotFoundError'); }
    finally { busy = false; render(); }
  });
  $('start').addEventListener('click', () => { $('start').disabled = true; send({ type: 'START_GAME', player: Number($('starting-player').value) }); });
  $('reset').addEventListener('click', () => send({ type: 'RESET_GAME' }));
  $('retry').addEventListener('click', () => send({ type: 'RETRY' }));
  $('clear-log').addEventListener('click', () => $('events').replaceChildren(text('li', 'empty-log', 'Timeline cleared.')));
  setInterval(() => {
    if (connected && Date.now() - requestedAt > 1800) { requestedAt = Date.now(); send({ type: master ? 'GET_STATUS' : 'GET_CONFIG' }); }
    if (connected && master && Date.now() - lastMessage >= 4500) notice('Master status is stale. Controls are disabled until communication recovers.', true);
    render();
  }, 500);
  buildPlayers(); render();
})();
