import { useState, useEffect, useRef, useCallback } from 'preact/hooks';
import { getWebSocketUrl } from '@/utils';
import deviceTool from '@/services/api/deviceTool';
import cameraApi, { type UvcStatus } from '@/services/api/camera';

/* WSFS frame types (firmware websocket_stream_server.h) */
const WS_FRAME_TYPE_METADATA = 7;
const WS_HEADER_SIZE = 64;

type Detection = {
  index?: number;
  class_name?: string;
  confidence?: number;
  x: number;
  y: number;
  width: number;
  height: number;
};

type AiResult = {
  type: number;
  detections?: Detection[];
  poses?: unknown[];
};

type MjpegPlayerProps = {
  /* stream status for the corner badge */
  status?: { width: number; height: number; fps: number };
};

/**
 * UVC (MJPEG) preview: the camera stream itself is the /uvc.mjpg HTTP
 * multipart stream; this component adds the AI result overlay by listening
 * to the WebSocket metadata frames (JSON) the continuous AI task pushes.
 * The two paths are fully decoupled on the device: slow inference never
 * back-pressures the MJPEG stream.
 */
/* Walk JPEG segment structure from the SOI at buf[0..1] and return the
 * index just past the terminating EOI, or -1 when the frame is still
 * incomplete. Handles: standalone markers, length-prefixed segments, and
 * entropy-coded scan data (FF00 stuffing) after SOS. */
function findJpegEoi(buf: Uint8Array): number {
  let i = 2;
  while (i + 1 < buf.length) {
    if (buf[i] !== 0xFF) { i++; continue; }
    const m = buf[i + 1];
    if (m === 0xFF) { i++; continue; }
    if (m === 0xD8) { i += 2; continue; }
    if (m === 0x01 || (m >= 0xD0 && m <= 0xD7)) { i += 2; continue; }
    if (m === 0xD9) { return i + 2; }
    if (m === 0xDA) {
      /* SOS: scan entropy data; FF00 is stuffing, FFD9 ends the frame */
      let j = i + 4;
      while (j + 1 < buf.length) {
        if (buf[j] !== 0xFF) { j++; continue; }
        const e = buf[j + 1];
        if (e === 0x00) { j += 2; continue; }
        if (e === 0xD9) { return j + 2; }
        if (e === 0xFF) { j++; continue; }
        j += 2;
      }
      return -1;
    }
    if (i + 3 >= buf.length) { return -1; }
    i += 2 + ((buf[i + 2] << 8) | buf[i + 3]);
  }
  return -1;
}

export default function MjpegPlayer({ status }: MjpegPlayerProps) {
  const [imgLoaded, setImgLoaded] = useState(false);
  const [imgError, setImgError] = useState(false);
  const [aiCount, setAiCount] = useState(0);
  const [liveFps, setLiveFps] = useState<number | null>(null);
  const [webFps, setWebFps] = useState<number | null>(null);
  const [imgSrc, setImgSrc] = useState<string | null>(null);
  const imgRef = useRef<HTMLImageElement>(null);
  const frameCountRef = useRef(0);
  /* last decoded frame dimensions: naturalWidth reads 0 transiently while
   * each blob loads, and the 16/9 fallback then breaks the pillarbox
   * mapping for 4:3 content (boxes flashed onto the bars) */
  const naturalRef = useRef({ w: 0, h: 0 });
  const canvasRef = useRef<HTMLCanvasElement>(null);
  const detectionsRef = useRef<Detection[]>([]);
  const rafRef = useRef<number | null>(null);

  const drawOverlay = useCallback(() => {
    const img = imgRef.current;
    const canvas = canvasRef.current;
    if (!img || !canvas || !imgLoaded) {
      rafRef.current = requestAnimationFrame(drawOverlay);
      return;
    }
    const w = img.clientWidth;
    const h = img.clientHeight;
    if (w === 0 || h === 0) {
      rafRef.current = requestAnimationFrame(drawOverlay);
      return;
    }
    if (canvas.width !== w || canvas.height !== h) {
      canvas.width = w;
      canvas.height = h;
    }
    const ctx = canvas.getContext('2d');
    if (!ctx) {
      rafRef.current = requestAnimationFrame(drawOverlay);
      return;
    }
    ctx.clearRect(0, 0, w, h);

    /* MJPEG letterboxes inside the element when aspect ratios differ: map
     * the normalized (0-1) boxes onto the contained video rect. */
    const naturalAspect = (naturalRef.current.w && naturalRef.current.h)
      ? naturalRef.current.w / naturalRef.current.h
      : 16 / 9;
    const boxAspect = w / h;
    let vw = w;
    let vh = h;
    let vx = 0;
    let vy = 0;
    if (boxAspect > naturalAspect) {
      vw = h * naturalAspect;
      vx = (w - vw) / 2;
    } else {
      vh = w / naturalAspect;
      vy = (h - vh) / 2;
    }

    ctx.lineWidth = 2;
    ctx.font = '12px monospace';
    for (const det of detectionsRef.current) {
      /* edge-clamped detections can extend past the frame (each field is
       * clamped to [0,1] independently by the postprocess), so intersect
       * the box with the actual video rect - never draw on the bars */
      let x = vx + det.x * vw;
      let y = vy + det.y * vh;
      let bw = det.width * vw;
      let bh = det.height * vh;
      const rx1 = Math.min(vx + vw, x + bw);
      const ry1 = Math.min(vy + vh, y + bh);
      x = Math.max(vx, x);
      y = Math.max(vy, y);
      bw = rx1 - x;
      bh = ry1 - y;
      if (bw <= 0 || bh <= 0) {
        continue;
      }
      ctx.strokeStyle = '#ff4444';
      ctx.strokeRect(x, y, bw, bh);
      const label = `${det.class_name ?? ''} ${Math.round((det.confidence ?? 0) * 100)}%`;
      if (label.trim()) {
        const tw = ctx.measureText(label).width;
        ctx.fillStyle = 'rgba(0,0,0,0.6)';
        ctx.fillRect(x, Math.max(0, y - 16), tw + 6, 16);
        ctx.fillStyle = '#ffffff';
        ctx.fillText(label, x + 3, Math.max(12, y - 4));
      }
    }
    rafRef.current = requestAnimationFrame(drawOverlay);
  }, [imgLoaded]);

  useEffect(() => {
    rafRef.current = requestAnimationFrame(drawOverlay);
    return () => {
      if (rafRef.current !== null) cancelAnimationFrame(rafRef.current);
    };
  }, [drawOverlay]);

  useEffect(() => {
    let ws: WebSocket | null = null;
    let cancelled = false;

    const consumeFrame = (frame: ArrayBuffer) => {
      const dv = new DataView(frame);
      if (dv.byteLength <= WS_HEADER_SIZE) return;
      if (dv.getUint32(0, false) !== 0x57534653) return; /* "WSFS" */
      const frameType = dv.getUint8(5);
      if (frameType !== WS_FRAME_TYPE_METADATA) return; /* only AI JSON */
      /* the device leaves header->frame_size unfilled: the payload spans
       * the rest of the WS message (same convention as the h264 player) */
      const payloadLen = dv.byteLength - WS_HEADER_SIZE;
      if (payloadLen <= 0 || payloadLen > 64 * 1024) return;
      try {
        const text = new TextDecoder().decode(
          new Uint8Array(frame, WS_HEADER_SIZE, payloadLen),
        );
        const result = JSON.parse(text) as AiResult;
        const dets = result.detections ?? [];
        detectionsRef.current = dets;
        setAiCount(dets.length);
      } catch {
        /* partial JSON: keep the previous overlay */
      }
    };

    const connect = () => {
      if (cancelled) return;
      ws = new WebSocket(getWebSocketUrl());
      ws.binaryType = 'arraybuffer';
      ws.onopen = () => {
        /* (re)arm the device-side stream session: after a device reboot the
         * page stays mounted, so the mount-time call alone is not enough */
        deviceTool.startVideoStreamReq().catch(() => { /* already active */ });
      };
      ws.onmessage = (ev: MessageEvent) => {
        if (!(ev.data instanceof ArrayBuffer)) return;
        consumeFrame(ev.data);
      };
      ws.onclose = () => {
        if (!cancelled) window.setTimeout(connect, 2000);
      };
      ws.onerror = () => {
        try { ws?.close(); } catch { /* noop */ }
      };
    };
    connect();

    return () => {
      cancelled = true;
      try { ws?.close(); } catch { /* noop */ }
    };
  }, []);
  /* Mirror the H264 Player lifecycle: start/stop the device-side stream
   * session so the metadata frames flow (the MJPEG stream itself is
   * independent and always available while the source is UVC). */
  useEffect(() => {
    deviceTool.startVideoStreamReq().catch(() => { /* stream may already be active */ });
    return () => {
      deviceTool.stopVideoStreamReq().catch(() => { /* noop */ });
    };
  }, []);

  /* live frame-rate: poll the device-side measured fps (assembled intact
   * frames per second) - an <img> MJPEG stream exposes no frame events */
  useEffect(() => {
    let stop = false;
    const poll = async () => {
      try {
        const res = await cameraApi.getCameraList();
        const s: UvcStatus | undefined = res.data?.uvc_status;
        if (!stop) setLiveFps(s?.streaming ? (s.fps_measured ?? null) : null);
      } catch {
        /* keep the last reading */
      }
    };
    /* web-side received-fps: fetch the MJPEG stream ourselves and split
     * frames on SOI/EOI markers - Chromium fires <img> load only once for
     * multipart streams, so per-frame counting needs explicit parsing */
    const ctrl = new AbortController();
    (async () => {
      let buf = new Uint8Array(0);
      let curUrl: string | null = null;
      let prevSig = -1;
      try {
        const res = await fetch('/uvc.mjpg', { signal: ctrl.signal });
        const reader = res.body?.getReader();
        if (!reader) return;
        /* eslint-disable no-await-in-loop -- sequential stream reads */
        for (;;) {
          const { done, value } = await reader.read();
          if (done) break;
          const merged = new Uint8Array(buf.length + value.length);
          merged.set(buf);
          merged.set(value, buf.length);
          buf = merged;
          for (;;) {
            let soi = -1;
            for (let i = 0; i + 1 < buf.length; i++) {
              if (buf[i] === 0xFF && buf[i + 1] === 0xD8) { soi = i; break; }
            }
            if (soi < 0) { buf = new Uint8Array(0); break; }
            if (soi > 0) buf = buf.slice(soi);
            /* marker-aware EOI: a raw FFD9 can legitimately appear inside
             * DQT/DHT segment bytes - walking the segment structure (and
             * FF00 stuffing inside the scan) splits frames exactly once */
            const eoi = findJpegEoi(buf);
            if (eoi < 0) break;
            const frame = buf.slice(0, eoi);
            buf = buf.slice(eoi);
            /* dedupe: the device resends the last frame at low
             * resolutions (web fps showed 2x dev) - identical frames are
             * neither counted nor re-rendered (saves a decode) */
            let sig = frame.length;
            for (let i = 0; i < 8 && i < frame.length; i++) {
              sig = (sig * 33 + frame[i]) >>> 0;
            }
            for (let i = frame.length - 8; i < frame.length; i++) {
              if (i >= 0) sig = (sig * 33 + frame[i]) >>> 0;
            }
            if (sig === prevSig) continue;
            prevSig = sig;
            frameCountRef.current += 1;
            const url = URL.createObjectURL(new Blob([frame], { type: 'image/jpeg' }));
            setImgSrc(url);
            if (curUrl) URL.revokeObjectURL(curUrl);
            curUrl = url;
          }
        }
      } catch {
        /* aborted or stream failed - imgError badge covers it */
      }
    })();
    /* meter: 1s window over the local frame counter, no network */
    const meter = window.setInterval(() => {
      setWebFps(frameCountRef.current);
      frameCountRef.current = 0;
    }, 1000);
    /* fps badge refresh: pause entirely while the tab is hidden so
     * backgrounded/other-tab sessions stop generating traffic */
    let timer: number | undefined;
    const start = () => {
      if (timer === undefined) {
        poll();
        timer = window.setInterval(poll, 5000);
      }
    };
    const halt = () => {
      if (timer !== undefined) {
        window.clearInterval(timer);
        timer = undefined;
      }
    };
    const onVis = () => (document.hidden ? halt() : start());
    document.addEventListener('visibilitychange', onVis);
    start();
    return () => {
      stop = true;
      halt();
      window.clearInterval(meter);
      ctrl.abort();
      document.removeEventListener('visibilitychange', onVis);
    };
  }, []);

  return (
    <div className="relative w-full h-full bg-black flex items-center justify-center">
      <div className="relative w-full">
        <img
          ref={imgRef}
          src={imgSrc ?? '/uvc.mjpg'}
          alt="UVC MJPEG preview"
          className="block w-full aspect-video object-contain"
          onLoad={() => {
            /* readiness + stable frame dims for the overlay mapping
             * (counting happens in the stream parser) */
            setImgLoaded(true);
            setImgError(false);
            naturalRef.current = { w: imgRef.current?.naturalWidth || 0,
                                   h: imgRef.current?.naturalHeight || 0 };
          }}
          onError={() => setImgError(true)}
        />
        <canvas
          ref={canvasRef}
          className="absolute inset-0 w-full h-full pointer-events-none"
        />
        {status && status.width > 0 && (
          <div className="absolute md:top-4 top-2 md:left-4 left-2 bg-gray-800/50 text-white px-3 py-1 rounded text-sm font-mono">
            MJPEG {status.width}x{status.height}
            {webFps != null ? ` · web ${webFps}` : ''}
            {liveFps != null ? ` · dev ${liveFps} fps` : ''}
          </div>
        )}
        {aiCount > 0 && (
          <div className="absolute md:top-4 top-2 md:right-4 right-2 bg-gray-800/50 text-white px-3 py-1 rounded text-sm font-mono">
            AI: {aiCount}
          </div>
        )}
        {imgError && (
          <div className="absolute left-1/2 top-1/2 -translate-x-1/2 -translate-y-1/2 text-white/80 text-sm">
            MJPEG stream unavailable
          </div>
        )}
      </div>
    </div>
  );
}
