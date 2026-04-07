import crypto from 'crypto';
import dotenv from 'dotenv';
import express from 'express';
import path from 'path';
import { fileURLToPath } from 'url';

import {
  AccessToken,
  EgressClient,
  EncodedFileOutput,
  EncodedFileType,
  EncodingOptionsPreset,
  IngressAudioEncodingPreset,
  IngressClient,
  IngressInput,
  IngressVideoEncodingPreset,
  RoomServiceClient,
  S3Upload,
} from 'livekit-server-sdk';

import {
  GetObjectCommand,
  ListObjectsV2Command,
  PutObjectCommand,
  S3Client,
} from '@aws-sdk/client-s3';
import { getSignedUrl } from '@aws-sdk/s3-request-presigner';
import {
  IoTDataPlaneClient,
  PublishCommand,
} from '@aws-sdk/client-iot-data-plane';

const __filename = fileURLToPath(import.meta.url);
const __dirname = path.dirname(__filename);
dotenv.config({ path: path.join(__dirname, '.env') });

function env(name, { required = false, defaultValue } = {}) {
  const value = process.env[name] ?? defaultValue;
  if (required && (!value || value.trim() === '')) {
    throw new Error(`Missing required env var: ${name}`);
  }
  return value;
}

function okJson(res, body) {
  res.status(200).json(body);
}

function errorJson(res, status, message) {
  res.status(status).json({ error: message });
}

function toHttpsEndpoint(hostOrUrl) {
  if (!hostOrUrl) return hostOrUrl;
  if (hostOrUrl.startsWith('http://') || hostOrUrl.startsWith('https://')) return hostOrUrl;
  return `https://${hostOrUrl}`;
}

function toWebsocketUrl(httpUrl) {
  if (!httpUrl) return httpUrl;
  if (httpUrl.startsWith('ws://') || httpUrl.startsWith('wss://')) return httpUrl;
  if (httpUrl.startsWith('https://')) return `wss://${httpUrl.slice('https://'.length)}`;
  if (httpUrl.startsWith('http://')) return `ws://${httpUrl.slice('http://'.length)}`;
  return httpUrl;
}

function randomId(prefix) {
  return `${prefix}-${crypto.randomBytes(8).toString('hex')}`;
}

function nowIsoCompact() {
  return new Date().toISOString().replace(/[:.]/g, '-');
}

const app = express();
app.use(express.json({ limit: '1mb' }));

const PORT = Number(env('PORT', { defaultValue: '5174' }));

const AWS_REGION = env('AWS_REGION', { defaultValue: '' });
const S3_BUCKET = env('S3_BUCKET', { defaultValue: '' });
const S3_PHOTOS_PREFIX = env('S3_PHOTOS_PREFIX', { defaultValue: 'photos/' });
const S3_VIDEOS_PREFIX = env('S3_VIDEOS_PREFIX', { defaultValue: 'recordings/' });

const AWS_IOT_DATA_ENDPOINT = env('AWS_IOT_DATA_ENDPOINT', { defaultValue: '' });
const AWS_IOT_TOPIC_CMD = env('AWS_IOT_TOPIC_CMD', { defaultValue: 'birdfeeder/cmd' });

const LIVEKIT_URL = env('LIVEKIT_URL', { defaultValue: '' });
const LIVEKIT_API_KEY = env('LIVEKIT_API_KEY', { defaultValue: '' });
const LIVEKIT_API_SECRET = env('LIVEKIT_API_SECRET', { defaultValue: '' });
const LIVEKIT_ROOM = env('LIVEKIT_ROOM', { defaultValue: 'birdfeeder' });
const LIVEKIT_INGRESS_NAME = env('LIVEKIT_INGRESS_NAME', { defaultValue: 'birdfeeder-device' });
const LIVEKIT_INGRESS_IDENTITY = env('LIVEKIT_INGRESS_IDENTITY', { defaultValue: 'device' });

const s3Client = new S3Client({ region: AWS_REGION || undefined });

function requireAwsBasics() {
  if (!AWS_REGION) throw new Error('AWS_REGION is required');
  if (!S3_BUCKET) throw new Error('S3_BUCKET is required');
}

function requireLiveKit() {
  if (!LIVEKIT_URL) throw new Error('LIVEKIT_URL is required');
  if (!LIVEKIT_API_KEY) throw new Error('LIVEKIT_API_KEY is required');
  if (!LIVEKIT_API_SECRET) throw new Error('LIVEKIT_API_SECRET is required');
}

function requireIoT() {
  if (!AWS_REGION) throw new Error('AWS_REGION is required');
  if (!AWS_IOT_DATA_ENDPOINT) throw new Error('AWS_IOT_DATA_ENDPOINT is required');
  if (!AWS_IOT_TOPIC_CMD) throw new Error('AWS_IOT_TOPIC_CMD is required');
}

function makeIotClient() {
  return new IoTDataPlaneClient({
    region: AWS_REGION,
    endpoint: toHttpsEndpoint(AWS_IOT_DATA_ENDPOINT),
  });
}

let activeEgressId = null;

app.get('/api/health', (req, res) => {
  okJson(res, { ok: true });
});

app.get('/api/config', (req, res) => {
  okJson(res, {
    livekit: {
      configured: Boolean(LIVEKIT_URL && LIVEKIT_API_KEY && LIVEKIT_API_SECRET),
      url: LIVEKIT_URL,
      room: LIVEKIT_ROOM,
    },
    aws: {
      configured: Boolean(AWS_REGION && S3_BUCKET),
      region: AWS_REGION,
      bucket: S3_BUCKET,
      photosPrefix: S3_PHOTOS_PREFIX,
      videosPrefix: S3_VIDEOS_PREFIX,
      iotConfigured: Boolean(AWS_REGION && AWS_IOT_DATA_ENDPOINT),
      iotTopicCmd: AWS_IOT_TOPIC_CMD,
    },
  });
});

app.get('/api/livekit/token', async (req, res) => {
  try {
    requireLiveKit();

    const identity = randomId('web');
    const token = new AccessToken(LIVEKIT_API_KEY, LIVEKIT_API_SECRET, {
      identity,
      name: 'Web Viewer',
    });

    token.addGrant({
      roomJoin: true,
      room: LIVEKIT_ROOM,
      canPublish: false,
      canSubscribe: true,
    });

    okJson(res, {
      token: await token.toJwt(),
      url: toWebsocketUrl(LIVEKIT_URL),
      room: LIVEKIT_ROOM,
    });
  } catch (err) {
    errorJson(res, 500, err?.message ?? String(err));
  }
});

app.get('/api/livekit/ingress', async (req, res) => {
  try {
    requireLiveKit();

    const ingressClient = new IngressClient(LIVEKIT_URL, LIVEKIT_API_KEY, LIVEKIT_API_SECRET);

    const existing = (await ingressClient.listIngress(LIVEKIT_ROOM)).find(
      (i) => i.name === LIVEKIT_INGRESS_NAME,
    );

    if (existing) {
      okJson(res, {
        ingressId: existing.ingressId,
        url: existing.url,
        streamKey: existing.streamKey,
        whipEndpoint: `${existing.url}/${existing.streamKey}`,
        room: existing.roomName,
      });
      return;
    }

    const info = await ingressClient.createIngress(IngressInput.WHIP_INPUT, {
      name: LIVEKIT_INGRESS_NAME,
      roomName: LIVEKIT_ROOM,
      participantIdentity: LIVEKIT_INGRESS_IDENTITY,
      participantName: 'ESP32 Device',
      video: { preset: IngressVideoEncodingPreset.H264_720P_30FPS_1_LAYER },
      audio: { preset: IngressAudioEncodingPreset.OPUS_MONO_64KBS },
    });

    okJson(res, {
      ingressId: info.ingressId,
      url: info.url,
      streamKey: info.streamKey,
      whipEndpoint: `${info.url}/${info.streamKey}`,
      room: info.roomName,
    });
  } catch (err) {
    errorJson(res, 500, err?.message ?? String(err));
  }
});

app.get('/api/livekit/ingress/list', async (req, res) => {
  try {
    requireLiveKit();

    const ingressClient = new IngressClient(LIVEKIT_URL, LIVEKIT_API_KEY, LIVEKIT_API_SECRET);
    const ingresses = await ingressClient.listIngress(LIVEKIT_ROOM);

    okJson(res, {
      room: LIVEKIT_ROOM,
      ingresses: (ingresses || []).map((i) => ({
        ingressId: i.ingressId,
        name: i.name,
        room: i.roomName,
        participantIdentity: i.participantIdentity,
        participantName: i.participantName,
        // Don't return streamKey by default; it's effectively a secret.
        url: i.url,
        inputType: i.inputType,
        state: i.state,
        createdAt: typeof i.createdAt === 'bigint' ? i.createdAt.toString() : i.createdAt,
        updatedAt: typeof i.updatedAt === 'bigint' ? i.updatedAt.toString() : i.updatedAt,
        // status/stats fields are SDK-version dependent; return them when present.
        status: i.status ?? null,
        stats: i.stats ?? null,
      })),
    });
  } catch (err) {
    errorJson(res, 500, err?.message ?? String(err));
  }
});

app.get('/api/livekit/room', async (req, res) => {
  try {
    requireLiveKit();

    const roomService = new RoomServiceClient(LIVEKIT_URL, LIVEKIT_API_KEY, LIVEKIT_API_SECRET);
    const rooms = await roomService.listRooms([LIVEKIT_ROOM]);
    const participants = await roomService.listParticipants(LIVEKIT_ROOM);

    okJson(res, {
      room: LIVEKIT_ROOM,
      rooms: rooms.map((r) => ({ name: r.name, numParticipants: r.numParticipants, activeRecording: r.activeRecording })),
      participants: participants.map((p) => ({
        identity: p.identity,
        name: p.name,
        state: p.state,
        joinedAt: typeof p.joinedAt === 'bigint' ? p.joinedAt.toString() : p.joinedAt,
        tracks: (p.tracks || []).map((t) => ({
          sid: t.sid,
          type: t.type,
          name: t.name,
          muted: t.muted,
          mimeType: t.mimeType,
          width: t.width,
          height: t.height,
          source: t.source,
        })),
      })),
    });
  } catch (err) {
    errorJson(res, 500, err?.message ?? String(err));
  }
});

app.post('/api/photo', async (req, res) => {
  try {
    requireAwsBasics();
    requireIoT();

    const key = `${S3_PHOTOS_PREFIX}${nowIsoCompact()}-${crypto.randomBytes(4).toString('hex')}.jpg`;
    const contentType = 'image/jpeg';

    const putCmd = new PutObjectCommand({
      Bucket: S3_BUCKET,
      Key: key,
      ContentType: contentType,
    });

    const uploadUrl = await getSignedUrl(s3Client, putCmd, { expiresIn: 60 * 10 });

    const getCmd = new GetObjectCommand({ Bucket: S3_BUCKET, Key: key });
    const publicUrl = await getSignedUrl(s3Client, getCmd, { expiresIn: 60 * 60 });

    const iot = makeIotClient();
    const payload = JSON.stringify({
      cmd: 'photo',
      upload_url: uploadUrl,
      content_type: contentType,
      public_url: publicUrl,
    });
    await iot.send(
      new PublishCommand({
        topic: AWS_IOT_TOPIC_CMD,
        qos: 1,
        payload: new TextEncoder().encode(payload),
      }),
    );

    okJson(res, { ok: true, key, publicUrl });
  } catch (err) {
    errorJson(res, 500, err?.message ?? String(err));
  }
});

async function listSignedObjects(prefix) {
  requireAwsBasics();

  const list = await s3Client.send(
    new ListObjectsV2Command({
      Bucket: S3_BUCKET,
      Prefix: prefix,
      MaxKeys: 50,
    }),
  );

  const contents = (list.Contents ?? [])
    .filter((o) => o.Key)
    .sort((a, b) => (b.LastModified?.getTime?.() ?? 0) - (a.LastModified?.getTime?.() ?? 0));

  const items = [];
  for (const obj of contents) {
    const getCmd = new GetObjectCommand({ Bucket: S3_BUCKET, Key: obj.Key });
    const url = await getSignedUrl(s3Client, getCmd, { expiresIn: 60 * 60 });
    items.push({
      key: obj.Key,
      url,
      lastModified: obj.LastModified?.toISOString?.() ?? null,
      size: obj.Size ?? null,
    });
  }
  return items;
}

app.get('/api/photos', async (req, res) => {
  try {
    const items = await listSignedObjects(S3_PHOTOS_PREFIX);
    okJson(res, { items });
  } catch (err) {
    errorJson(res, 500, err?.message ?? String(err));
  }
});

app.get('/api/videos', async (req, res) => {
  try {
    const items = await listSignedObjects(S3_VIDEOS_PREFIX);
    okJson(res, { items });
  } catch (err) {
    errorJson(res, 500, err?.message ?? String(err));
  }
});

app.post('/api/record/start', async (req, res) => {
  try {
    requireLiveKit();
    requireAwsBasics();

    if (activeEgressId) {
      errorJson(res, 409, `Recording already active: ${activeEgressId}`);
      return;
    }

    const accessKey = env('AWS_ACCESS_KEY_ID', { required: true });
    const secret = env('AWS_SECRET_ACCESS_KEY', { required: true });

    const egressClient = new EgressClient(LIVEKIT_URL, LIVEKIT_API_KEY, LIVEKIT_API_SECRET);

    const filepath = `${S3_VIDEOS_PREFIX}${LIVEKIT_ROOM}-${nowIsoCompact()}.mp4`;

    const fileOutput = new EncodedFileOutput({
      fileType: EncodedFileType.MP4,
      filepath,
      output: {
        case: 's3',
        value: new S3Upload({
          accessKey,
          secret,
          region: AWS_REGION,
          bucket: S3_BUCKET,
        }),
      },
    });

    const info = await egressClient.startRoomCompositeEgress(
      LIVEKIT_ROOM,
      { file: fileOutput },
      {
        layout: 'speaker',
        encodingOptions: EncodingOptionsPreset.H264_720P_30,
      },
    );

    activeEgressId = info.egressId;

    okJson(res, { ok: true, egressId: info.egressId, filepath });
  } catch (err) {
    errorJson(res, 500, err?.message ?? String(err));
  }
});

app.post('/api/record/stop', async (req, res) => {
  try {
    requireLiveKit();

    const egressId = req.body?.egressId ?? activeEgressId;
    if (!egressId) {
      errorJson(res, 400, 'No active recording to stop');
      return;
    }

    const egressClient = new EgressClient(LIVEKIT_URL, LIVEKIT_API_KEY, LIVEKIT_API_SECRET);
    const info = await egressClient.stopEgress(egressId);

    if (activeEgressId === egressId) {
      activeEgressId = null;
    }

    okJson(res, { ok: true, egressId: info.egressId, status: info.status });
  } catch (err) {
    errorJson(res, 500, err?.message ?? String(err));
  }
});

app.listen(PORT, () => {
  console.log(`[web_demo] server listening on http://localhost:${PORT}`);
});
