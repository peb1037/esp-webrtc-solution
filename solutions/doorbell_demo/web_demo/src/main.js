import './style.css';

import { Room, RoomEvent, Track } from 'livekit-client';

function $(id) {
  return document.getElementById(id);
}

function setStatus(el, text) {
  el.textContent = text;
}

async function fetchJson(url, options) {
  const res = await fetch(url, {
    headers: { 'Content-Type': 'application/json' },
    ...options,
  });

  const body = await res.json().catch(() => ({}));
  if (!res.ok) {
    throw new Error(body?.error || `HTTP ${res.status}`);
  }
  return body;
}

const liveStatus = $('live-status');
const recordStatus = $('record-status');
const photoStatus = $('photo-status');
const deviceCmdStatus = $('device-cmd-status');
const videoControlStatus = $('video-control-status');

const btnRecordStart = $('btn-record-start');
const btnRecordStop = $('btn-record-stop');
const btnPhoto = $('btn-photo');
const btnPingDevice = $('btn-device-ping');
const btnVideoStart = $('btn-video-start');
const btnVideoStop = $('btn-video-stop');

const liveVideo = $('live-video');
const photos = $('photos');
const videos = $('videos');

let room = null;
let activeEgressId = null;
let videoStreamRequested = false;

let subscribed = {
  video: 0,
  audio: 0,
};

let videoDiag = {
  w: 0,
  h: 0,
  t: 0,
  rs: 0,
  trackState: '',
  trackMuted: false,
  trackEnabled: false,
  pcConn: '',
  pcIce: '',
  rxKb: 0,
  packets: 0,
  framesDecoded: 0,
  framesReceived: 0,
  pli: 0,
  nack: 0,
  codec: '',
};

let videoDiagTimer = null;

function findSubscriberPeerConnection(room) {
  // LiveKit doesn't expose this as a stable public API; this is best-effort
  // diagnostics only.
  const candidates = [
    room?.engine?.pcManager?.subscriber?.pc,
    room?.engine?.pcManager?.subscriber?._pc,
    room?.engine?.pcManager?.subscriber?.transport?._pc,
    room?.engine?.pcManager?.subscriber?._pcTransport?._pc,
    room?.engine?.pcManager?.subscriber?._transport?._pc,
  ];

  for (const pc of candidates) {
    if (pc && typeof pc.getStats === 'function') return pc;
  }
  return null;
}

async function updateInboundVideoStats(room, mediaStreamTrack) {
  try {
    const pc = findSubscriberPeerConnection(room);
    if (!pc) return;

    videoDiag.pcConn = pc.connectionState || '';
    videoDiag.pcIce = pc.iceConnectionState || '';

    const report = await pc.getStats(mediaStreamTrack || null);
    let inbound = null;
    const byId = new Map();
    for (const s of report.values()) byId.set(s.id, s);

    for (const s of report.values()) {
      if (s.type !== 'inbound-rtp') continue;
      const kind = s.kind || s.mediaType;
      if (kind !== 'video') continue;
      inbound = s;
      break;
    }
    if (!inbound) return;

    const codec = inbound.codecId ? byId.get(inbound.codecId) : null;
    const mimeType = codec?.mimeType || '';

    videoDiag.rxKb = Math.round(((inbound.bytesReceived || 0) / 1024) * 10) / 10;
    videoDiag.packets = inbound.packetsReceived || 0;
    videoDiag.framesDecoded = inbound.framesDecoded || 0;
    videoDiag.framesReceived = inbound.framesReceived || 0;
    videoDiag.pli = inbound.pliCount || 0;
    videoDiag.nack = inbound.nackCount || 0;
    videoDiag.codec = mimeType;
  } catch {
    // Ignore stats failures; some browsers may restrict access.
  }
}

function updateLiveStatus() {
  if (!room) return;
  const remoteCount = room.remoteParticipants?.size ?? 0;
  const diag = subscribed.video
    ? ` vw=${videoDiag.w} vh=${videoDiag.h} t=${videoDiag.t.toFixed(1)} rs=${videoDiag.rs} track=${videoDiag.trackState} muted=${videoDiag.trackMuted ? 1 : 0} en=${videoDiag.trackEnabled ? 1 : 0} pc=${videoDiag.pcConn || '?'} ice=${videoDiag.pcIce || '?'} rx=${videoDiag.rxKb}k fd=${videoDiag.framesDecoded} fr=${videoDiag.framesReceived} p=${videoDiag.packets} pli=${videoDiag.pli} codec=${videoDiag.codec || 'unknown'}`
    : '';
  setStatus(liveStatus, `Connected (remotes=${remoteCount}, video=${subscribed.video}, audio=${subscribed.audio})${diag}`);
}

function clearNode(node) {
  while (node.firstChild) node.removeChild(node.firstChild);
}

function setRecordingUi(recording) {
  btnRecordStart.disabled = recording;
  btnRecordStop.disabled = !recording;
}

function setVideoControlUi({ running, pending }) {
  btnVideoStart.disabled = pending || running;
  btnVideoStop.disabled = pending || !running;
}

async function sendDeviceCommand(path, body = {}) {
  return fetchJson(path, {
    method: 'POST',
    body: JSON.stringify(body),
  });
}

function nowLabel() {
  return new Date().toLocaleTimeString();
}

async function connectLive() {
  setStatus(liveStatus, 'Connecting…');

  try {
    const { token, url } = await fetchJson('/api/livekit/token');

    room = new Room({
      adaptiveStream: false,
      dynacast: false,
      autoSubscribe: true,
    });

    // Expose for devtools debugging.
    window.__lkRoom = room;

    subscribed = { video: 0, audio: 0 };

    room
      .on(RoomEvent.ParticipantConnected, () => {
        updateLiveStatus();
      })
      .on(RoomEvent.ParticipantDisconnected, () => {
        updateLiveStatus();
      })
      .on(RoomEvent.TrackSubscribed, (track, pub, participant) => {
        if (track.kind === Track.Kind.Video) {
          subscribed.video += 1;
          clearNode(liveVideo);

          console.log('[livekit] video subscribed', {
            participant: participant?.identity,
            name: participant?.name,
            sid: participant?.sid,
            trackSid: track?.sid,
            source: pub?.source,
            mimeType: pub?.mimeType,
          });

          const el = track.attach();
          el.autoplay = true;
          el.playsInline = true;
          el.muted = true;
          el.style.width = '100%';
          el.style.height = '100%';
          el.style.objectFit = 'contain';
          liveVideo.appendChild(el);
          // Avoid autoplay policy edge cases.
          el.play?.().catch(() => {});

          if (videoDiagTimer) {
            clearInterval(videoDiagTimer);
            videoDiagTimer = null;
          }
          videoDiagTimer = setInterval(() => {
            videoDiag.w = el.videoWidth || 0;
            videoDiag.h = el.videoHeight || 0;
            videoDiag.t = el.currentTime || 0;
            videoDiag.rs = el.readyState || 0;
            videoDiag.trackState = track?.mediaStreamTrack?.readyState || '';
            videoDiag.trackMuted = !!track?.mediaStreamTrack?.muted;
            videoDiag.trackEnabled = !!track?.mediaStreamTrack?.enabled;
            updateInboundVideoStats(room, track.mediaStreamTrack);
            updateLiveStatus();
          }, 500);

          updateLiveStatus();
        }

        if (track.kind === Track.Kind.Audio) {
          subscribed.audio += 1;

          console.log('[livekit] audio subscribed', {
            participant: participant?.identity,
            name: participant?.name,
            sid: participant?.sid,
            trackSid: track?.sid,
            source: pub?.source,
            mimeType: pub?.mimeType,
          });

          const el = track.attach();
          el.autoplay = true;
          el.style.display = 'none';
          document.body.appendChild(el);
          el.play?.().catch(() => {});
          updateLiveStatus();
        }
      })
      .on(RoomEvent.Disconnected, () => {
        setStatus(liveStatus, 'Disconnected');
        clearNode(liveVideo);
        if (videoDiagTimer) {
          clearInterval(videoDiagTimer);
          videoDiagTimer = null;
        }
        room = null;
      });

    await room.connect(url, token);

    // Ensure we subscribe even if publications already exist.
    for (const participant of room.remoteParticipants.values()) {
      for (const pub of participant.trackPublications.values()) {
        pub.setSubscribed(true);
      }
    }

    updateLiveStatus();
  } catch (err) {
    setStatus(liveStatus, `Error: ${err?.message ?? String(err)}`);
    room = null;
  }
}

async function refreshPhotos() {
  photoStatus.textContent = '';
  clearNode(photos);

  try {
    const { items } = await fetchJson('/api/photos');

    for (const item of items) {
      const wrap = document.createElement('div');
      wrap.className = 'photo';

      const img = document.createElement('img');
      img.src = item.url;
      img.alt = item.key;
      wrap.appendChild(img);

      const meta = document.createElement('div');
      meta.className = 'small';
      meta.textContent = item.key;
      wrap.appendChild(meta);

      photos.appendChild(wrap);
    }

    if (!items.length) {
      const empty = document.createElement('div');
      empty.className = 'small';
      empty.textContent = 'No photos found.';
      photos.appendChild(empty);
    }
  } catch (err) {
    photoStatus.textContent = `Error: ${err?.message ?? String(err)}`;
  }
}

async function refreshVideos() {
  clearNode(videos);

  try {
    const { items } = await fetchJson('/api/videos');

    for (const item of items) {
      const block = document.createElement('div');

      const player = document.createElement('video');
      player.controls = true;
      player.src = item.url;
      player.style.width = '100%';
      block.appendChild(player);

      const meta = document.createElement('div');
      meta.className = 'small';
      meta.textContent = item.key;
      block.appendChild(meta);

      videos.appendChild(block);
    }

    if (!items.length) {
      const empty = document.createElement('div');
      empty.className = 'small';
      empty.textContent = 'No recordings found.';
      videos.appendChild(empty);
    }
  } catch (err) {
    const msg = document.createElement('div');
    msg.className = 'small';
    msg.textContent = `Error: ${err?.message ?? String(err)}`;
    videos.appendChild(msg);
  }
}

async function capturePhoto() {
  photoStatus.textContent = '';
  btnPhoto.disabled = true;

  try {
    await fetchJson('/api/photo', { method: 'POST', body: '{}' });
    photoStatus.textContent = 'Photo requested. Refresh in a moment.';

    setTimeout(() => {
      refreshPhotos();
    }, 3000);
  } catch (err) {
    photoStatus.textContent = `Error: ${err?.message ?? String(err)}`;
  } finally {
    btnPhoto.disabled = false;
  }
}

async function startRecording() {
  setRecordingUi(true);
  setStatus(recordStatus, 'Starting…');

  try {
    const { egressId } = await fetchJson('/api/record/start', {
      method: 'POST',
      body: '{}',
    });

    activeEgressId = egressId;
    setStatus(recordStatus, `Recording (egressId=${egressId})`);
    setRecordingUi(true);
    btnRecordStop.disabled = false;
  } catch (err) {
    activeEgressId = null;
    setStatus(recordStatus, `Error: ${err?.message ?? String(err)}`);
    setRecordingUi(false);
  }
}

async function stopRecording() {
  setRecordingUi(true);
  setStatus(recordStatus, 'Stopping…');

  try {
    await fetchJson('/api/record/stop', {
      method: 'POST',
      body: JSON.stringify({ egressId: activeEgressId }),
    });

    activeEgressId = null;
    setStatus(recordStatus, 'Idle');
    setRecordingUi(false);

    await refreshVideos();
  } catch (err) {
    setStatus(recordStatus, `Error: ${err?.message ?? String(err)}`);
    btnRecordStop.disabled = false;
    btnRecordStart.disabled = true;
  }
}

async function pingDevice() {
  btnPingDevice.disabled = true;
  setStatus(deviceCmdStatus, 'Sending ping command…');

  try {
    await sendDeviceCommand('/api/device/ping');
    setStatus(deviceCmdStatus, `Ping command sent at ${nowLabel()}`);
  } catch (err) {
    setStatus(deviceCmdStatus, `Error: ${err?.message ?? String(err)}`);
  } finally {
    btnPingDevice.disabled = false;
  }
}

async function requestVideoStart() {
  setVideoControlUi({ running: videoStreamRequested, pending: true });
  setStatus(videoControlStatus, 'Sending start-video command…');

  try {
    await sendDeviceCommand('/api/device/video/start');
    videoStreamRequested = true;
    setStatus(videoControlStatus, `Start command sent at ${nowLabel()}`);
  } catch (err) {
    setStatus(videoControlStatus, `Error: ${err?.message ?? String(err)}`);
  } finally {
    setVideoControlUi({ running: videoStreamRequested, pending: false });
  }
}

async function requestVideoStop() {
  setVideoControlUi({ running: videoStreamRequested, pending: true });
  setStatus(videoControlStatus, 'Sending stop-video command…');

  try {
    await sendDeviceCommand('/api/device/video/stop');
    videoStreamRequested = false;
    setStatus(videoControlStatus, `Stop command sent at ${nowLabel()}`);
  } catch (err) {
    setStatus(videoControlStatus, `Error: ${err?.message ?? String(err)}`);
  } finally {
    setVideoControlUi({ running: videoStreamRequested, pending: false });
  }
}

btnPhoto.addEventListener('click', capturePhoto);
btnRecordStart.addEventListener('click', startRecording);
btnRecordStop.addEventListener('click', stopRecording);
btnPingDevice.addEventListener('click', pingDevice);
btnVideoStart.addEventListener('click', requestVideoStart);
btnVideoStop.addEventListener('click', requestVideoStop);

setRecordingUi(false);
setVideoControlUi({ running: videoStreamRequested, pending: false });

connectLive();
refreshPhotos();
refreshVideos();
