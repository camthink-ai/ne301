import {
    playerStateIdle,
    specialDuration,
    previewFrameMs,
    maxFrameMs,
} from './constant'
import browser from './utils/myBrowser.js';
import MsMediaSource from './media'
import { sleep } from '@/utils/index.js';

type SaveAsFn = (data: Blob | File | MediaSource | string, filename?: string, options?: any) => void;
let saveAs: SaveAsFn | null = null;

// Dynamically load FileSaver
const loadFileSaver = async () => {
    try {
        const script = document.createElement('script');
        script.src = '/libs/FileSaver.min.js';
        script.async = true;
        document.head.appendChild(script);

        return new Promise<void>((resolve, reject) => {
            script.onload = () => {
                // eslint-disable-next-line @typescript-eslint/ban-ts-comment
                // @ts-expect-error
                saveAs = window.saveAs;
                resolve();
            };
            script.onerror = reject;
        });
    } catch (err) {
        console.warn('FileSaver load failed:', err);
    }
};

loadFileSaver();
/**
 * @TODO
 * Player log panel
 * - WebSocket packet transmission rate
 * - Video stream frame rate
 * - Packet loss rate
 * - Latency
 * - Buffer data amount
 * - 
 */
// Type definitions
interface CallbackEvent {
    t: string;
}

type CallbackFunction = (event: CallbackEvent) => void;

interface FrameData {
    cmd: string;
    data: Uint8Array;
    videoTime: number;
    iChannelId: number;
    // userData: {
    //     iframe: number | number[];
    //     timeUsec: number;
    //     index: number;
    //     duration: number;
    //     size: number;
    //     realDuration: number;
    // };
}

interface ReturnResult {
    e: number;
    m: string;
}

interface WsWorkerMessage {
    type: 'connecting' | 'open' | 'close' | 'error' | 'video-data';
    error?: string;
    payload?: ArrayBuffer;
    code?: number;
    reason?: string;
}

interface DecWorkerMessage {
    type: 'ready' | 'error';
    error?: string;
}

/**
 * @description: H264 player class
 */
export default class H264Player {
    private videoPlayer: MsMediaSource | null = null;

    private decodeWorker: Worker | null = null;

    private webSocketWorker: Worker | null = null;

    private decoderWorker: Worker | null = null;

    private firstFrame: number = 0;

    private frameIndex: number = 0;

    private lastVideoTime: number = 0;

    private isPlayback: boolean = false;

    private playSpeed: number = 1;

    private direction: number = 0;

    private cb: CallbackFunction;

    private type: string = 'video';

    private snapshotFlag: number = 0;

    private lastSec: number = 0;

    private videoFrameCnt: number = 0;

    private playerState: number = playerStateIdle;

    private iChannelId: number = 0;

    private videoElement: HTMLVideoElement | null = null;

    private wsRetryCount: number = 3;

    private wsUrl: string = '';

    private isStarted: boolean = false;

    private isConnected: boolean = false;

    // private hederBits: Uint8Array = new Uint8Array(0);

    public packetCount: number = 0;

    public packetsPerSecond: number = 0;

    private lastPacketSec: number = 0;

    public currentTime: number = 0;

    public currentFrame: number = 0;

    public totalBytesLoaded: number = 0;

    // Adaptive buffer management
    private readonly MAX_BUFFER_ENTRIES: number = 10;

    private bufferTimes: number[] = [];

    private bufferIndex: number = 0;

    private lastJumpTime: number = 0;

    private readonly BUFFERING_COOLDOWN_TIMEOUT: number = 5000;

    // Latency tracking
    public latency: number = 0;

    // Bandwidth tracking
    private lastBandwidthCheck: number = 0;

    private lastBytesLoaded: number = 0;

    public bandwidth: number = 0; // kBps

    // H264 raw stream capture
    private captureActive: boolean = false;

    private captureStartMs: number = 0;

    private captureDurationMs: number = 0;

    private captureParts: Uint8Array[] = [];

    private captureTotalBytes: number = 0;

    // Add getter method for debugging
    getCurrentTime(): number {
        return this.currentTime;
    }

    constructor(cb: CallbackFunction) {
        this.cb = cb;
        this.creatVideoPlayer();
        this.webSocketWorker = null;
        this.decodeWorker = null;
        this.wsRetryCount = 3;
        this.wsUrl = '';
        this.isStarted = false;
        this.isConnected = false;
        // this.hederBits = new Uint8Array(0);
        this.packetCount = 0;
        this.packetsPerSecond = 0;
        this.lastPacketSec = 0;
        this.currentTime = 0;
        this.currentFrame = 0;
        this.totalBytesLoaded = 0;
        this.bufferTimes = [];
        this.bufferIndex = 0;
        this.lastJumpTime = 0;
        this.latency = 0;
        this.lastBandwidthCheck = Date.now();
        this.lastBytesLoaded = 0;
        this.bandwidth = 0;
    }

    /**
     * Setup WebSocket worker message handler
     */
    private setupWebSocketWorkerHandler(): void {
        if (!this.webSocketWorker) return;
        
        this.webSocketWorker.onmessage = (ev: MessageEvent<WsWorkerMessage>) => {
            const msg = ev.data;
            if (!msg || typeof msg !== 'object') return;
            
            switch (msg.type) {
                case 'connecting':
                    // Notify external to show loading during the connection phase
                    window.dispatchEvent(new CustomEvent('wv_work', { detail: false }));
                    break;
                case 'open':
                    // Connected: hide loading
                    window.dispatchEvent(new CustomEvent('wv_work', { detail: true }));
                    window.dispatchEvent(new CustomEvent('wv_open'));
                    break;
                case 'close':
                    window.dispatchEvent(new CustomEvent('wv_close', { 
                        detail: { code: msg.code, reason: msg.reason } 
                    }));
                    break;
                case 'error':
                    window.dispatchEvent(new CustomEvent('wv_error', { detail: msg.error }));
                    // Keep loading visible while retrying
                    window.dispatchEvent(new CustomEvent('wv_work', { detail: false }));
                    this.wsRetryCount -= 1;
                    if (this.wsRetryCount > 0) {
                        this.resetStartState().start(this.wsUrl);
                    }
                    break;
                case 'video-data': {
                    // Count packets per second
                    const nowSec = Math.floor(Date.now() / 1000);
                    if (this.lastPacketSec !== nowSec) {
                        if (this.lastPacketSec > 0) {
                            this.packetsPerSecond = this.packetCount;
                        }
                        this.lastPacketSec = nowSec;
                        this.packetCount = 0;
                    }

                    if (msg.payload instanceof ArrayBuffer) {
                        this.totalBytesLoaded += msg.payload.byteLength;

                        const frameData = this.dealVideoData(msg.payload);
                        if (frameData) {
                            this.packetCount++;
                            this.decoderWorker?.postMessage(frameData);
                        }
                    }
                    break;
                }
                default:
                    break;
            }
        };
    }

    /**
     * Setup decoder worker message handler
     */
    private setupDecoderWorkerHandler(): void {
        if (!this.decoderWorker) return;
        
        this.decoderWorker.onmessage = async (ev: MessageEvent<DecWorkerMessage>) => {
            this.videoMsgCallback(ev as unknown as MessageEvent);
        };
    }

    /**
     * Initialize workers (idempotent - safe to call multiple times)
     */
    initWorkers(): this {
        const needWsWorker = !this.webSocketWorker;
        const needDecoderWorker = !this.decoderWorker;
        
        // If both workers exist, just ensure handlers are set up
        if (!needWsWorker && !needDecoderWorker) {
            this.setupWebSocketWorkerHandler();
            this.setupDecoderWorkerHandler();
            return this;
        }

        // Start connection, notify external to show loading
        window.dispatchEvent(new CustomEvent('wv_work', { detail: false }));
        if (needWsWorker) {
            this.webSocketWorker = new Worker(new URL('./websocket-init.ts', import.meta.url), { type: 'module' });
        }
        
        if (needDecoderWorker) {
            this.decoderWorker = new Worker(new URL('./videoWorker.ts', import.meta.url));
        }
        this.setupWebSocketWorkerHandler();
        this.setupDecoderWorkerHandler();
        
        return this;
    }

    initPlayer(videoElement: HTMLVideoElement): this {
        this.videoElement = videoElement;
        this.videoPlayer = new MsMediaSource((msg: CallbackEvent) => {
            this.mediaSrcCallback(msg);
        });
        this.videoPlayer.setVideoElement(this.videoElement);
        this.setupProgressHandler();
        return this;
    }

    wsDisconnect(): this {
        // debugger;
        this.isConnected = false;
        this.webSocketWorker?.postMessage({ type: 'disconnect' });
        return this;
    }

    wsConnect(url: string): this {
        this.isConnected = true;
        this.webSocketWorker?.postMessage({ type: 'connect', url });
        return this;
    }

    resetStartState(): this {
        this.isStarted = false;
        return this;
    }

    async start(url: string): Promise<this> {
        this.wsUrl = url;
        if (this.isStarted) return this;
        if (this.isConnected) {
            this.wsDisconnect();
            await sleep(500);
        }
        this.isStarted = true;
        this.initWorkers();
        this.wsConnect(url);
        this.decodeWorker?.postMessage({ type: 'init' });
        return this;
    }

    destroy(): void {
        this.stopPlay();
        if (this.webSocketWorker) {
            this.wsDisconnect();
            try {
                this.webSocketWorker.terminate();
            } catch (error) {
                console.error('Error terminating websocket worker', error);
            }
            this.webSocketWorker = null;
        }
        if (this.decoderWorker) {
            try {
                this.decoderWorker.terminate();
            } catch (error) {
                console.error('Error terminating decoder worker', error);
            }
            this.decoderWorker = null;
        }
        if (this.videoPlayer) {
            this.videoPlayer.uninitMse();
            this.videoPlayer = null;
        }
        
        this.videoElement = null;
        this.isStarted = false;
        this.isConnected = false;
        this.wsUrl = '';
    }

   /**
    * Pause video stream
    * - close websocket connection
    * - don't destroy mse, just stop feed stream to mse
    */
    pause(): void {
        // Disconnect websocket
        if (this.webSocketWorker) {
            this.wsDisconnect();
            setTimeout(() => {
                this.webSocketWorker?.terminate();
                this.webSocketWorker = null;
            }, 100);
        }
        this.videoPlayer?.clearBuffer();
        if (this.videoElement && !this.videoElement.paused) {
            this.videoElement.pause();
        }
        this.isConnected = false;
        this.isStarted = false;
    }
    
    /**
     * Restart video stream after model upload / device pipeline reset
     */
    async hardRestart(): Promise<void> {
        if (!this.wsUrl || !this.videoElement) return;

        this.firstFrame = 0;
        this.frameIndex = 0;
        this.lastVideoTime = 0;
        this.lastSec = 0;
        this.videoFrameCnt = 0;
        this.packetCount = 0;
        this.packetsPerSecond = 0;
        this.lastPacketSec = 0;
        this.wsRetryCount = 3;

        if (this.webSocketWorker) {
            this.wsDisconnect();
            await sleep(300);
            try {
                this.webSocketWorker.terminate();
            } catch {
                // ignore
            }
            this.webSocketWorker = null;
        }

        if (this.decoderWorker) {
            try {
                this.decoderWorker.postMessage({ cmd: 'stop' });
            } catch {
                // ignore
            }
            await sleep(100);
            try {
                this.decoderWorker.terminate();
            } catch {
                // ignore
            }
            this.decoderWorker = null;
        }

        const video = this.videoElement;
        if (this.videoPlayer) {
            this.videoPlayer.resetLivePreview(video);
        }

        this.isConnected = false;
        this.isStarted = false;
        await this.start(this.wsUrl);
    }

    /**
     * Restart video stream
     * - init websocket connection
     * - close mse buffer and feed stream to mse
     */
    async reStart(): Promise<void> {
        await this.hardRestart();
    }

    videoMsgCallback(event: MessageEvent): void {
        this.videoPlayer?.processMp4VideoData(event, this.snapshotFlag);
    }

    mediaSrcCallback(msg: CallbackEvent): void {
        this.cb(msg);
    }

    creatVideoPlayer(): void {
        this.videoPlayer = new MsMediaSource((msg: CallbackEvent) => {
            this.mediaSrcCallback(msg);
        });
    }

    initDecodeWorker(): void {
        this.decodeWorker = new Worker(new URL('./videoWorker.ts', import.meta.url));
        this.decodeWorker.onmessage = (event: MessageEvent) => {
            this.videoMsgCallback(event);
        };
    }

    dealVideoData(frameData: ArrayBuffer): FrameData | false {
        const headSize = 64;
        if (frameData.byteLength <= headSize) {
            return false;
        }

        // Timestamp at bytes 12-15 in WS frame header (big-endian seconds)
        let timestamp = 0;
        try {
            timestamp = new DataView(frameData).getUint32(12, false);
            this.currentTime = timestamp;
        } catch (e) {
            console.error('Error getting timestamp from offset 8:', e);
        }

        if (timestamp === 0) {
            timestamp = Math.floor(Date.now() / 1000);
            this.currentTime = timestamp;
        }

        const h264Data = new Uint8Array(frameData, headSize);
        if (!H264Player.checkFrameData(h264Data)) {
            return false;
        }

        let duration = 0;
        const timeUsec = Date.now() * 1000;
        const nowSec = (timeUsec / 1000) | 0;
        if (this.lastSec !== nowSec) {
            this.lastSec = nowSec;
            this.videoFrameCnt = 0;
        }
        this.videoFrameCnt++;

        if (this.firstFrame === 0) {
            this.firstFrame = 1;
            duration = previewFrameMs;
        } else if (!this.isPlayback) {
            // Fixed frame interval keeps MSE timeline stable under network jitter
            duration = previewFrameMs;
        } else if (this.direction === 0) {
            if (this.playSpeed >= 4 || this.playSpeed <= 0.125) {
                duration = specialDuration * 1000;
            } else {
                duration = Math.round((timeUsec - this.lastVideoTime) / 1000);
                duration = Math.round(duration / this.playSpeed);
                if (duration <= 0 || duration > maxFrameMs) {
                    duration = maxFrameMs;
                }
            }
        } else {
            duration = specialDuration * 1000;
        }

        this.frameIndex++;
        this.lastVideoTime = timeUsec;

        if (this.captureActive) {
            this.appendH264Capture(frameData);
        }

        return {
            cmd: 'video',
            data: h264Data,
            videoTime: duration,
            iChannelId: this.iChannelId,
        };
    }

    // Start capturing H264 raw stream (Annex B), default 20 seconds
    public startH264Capture(durationSeconds: number = 20): void {
        this.captureActive = true;
        this.captureStartMs = Date.now();
        this.captureDurationMs = Math.max(1, durationSeconds) * 1000;
        this.captureParts = [];
        this.captureTotalBytes = 0;
    }

    // Immediately export and stop capture
    public stopAndExportH264Capture(): void {
        if (!this.captureActive) return;
        this.exportH264Capture();
    }

    private appendH264Capture(frameData: ArrayBuffer): void {
        // Extract all bytes starting from the first 0x00000001 start code after 64 bytes
        const totalLen = frameData.byteLength;
        if (totalLen <= 68) return;
        const dv = new DataView(frameData);
        const start = 64;
        const end = totalLen - 4;
        let pos = -1;
        for (let i = start; i <= end; i += 1) {
            if (dv.getUint32(i, false) === 0x00000001) {
                pos = i;
                break;
            }
        }
        if (pos === -1) return;

        const raw = new Uint8Array(frameData, pos);
        // Make a copy to avoid data changes due to source buffer reuse
        const copy = new Uint8Array(raw.byteLength);
        copy.set(raw);
        this.captureParts.push(copy);
        this.captureTotalBytes += copy.byteLength;

        if (Date.now() - this.captureStartMs >= this.captureDurationMs) {
            this.exportH264Capture();
        }
    }

    private exportH264Capture(): void {
        // Merge into a single Uint8Array
        const out = new Uint8Array(this.captureTotalBytes);
        let offset = 0;
        for (let i = 0; i < this.captureParts.length; i += 1) {
            const part = this.captureParts[i];
            out.set(part, offset);
            offset += part.byteLength;
        }

        const blob = new Blob([out], { type: 'application/octet-stream' });
        const filename = `capture_${new Date().toISOString().replace(/[:.]/g, '-')}.h264`;
        if (saveAs) {
            saveAs(blob, filename);
        } else {
            const url = URL.createObjectURL(blob);
            const a = document.createElement('a');
            a.href = url;
            a.download = filename;
            document.body.appendChild(a);
            a.click();
            document.body.removeChild(a);
            URL.revokeObjectURL(url);
        }

        // Reset capture state
        this.captureActive = false;
        this.captureStartMs = 0;
        this.captureDurationMs = 0;
        this.captureParts = [];
        this.captureTotalBytes = 0;
    }

    static checkFrameData(h264Data: Uint8Array): boolean {
        const len = h264Data.byteLength;
        if (len < 4) return false;

        // Annex-B: 00 00 01 or 00 00 00 01
        if (h264Data[0] === 0 && h264Data[1] === 0) {
            if (h264Data[2] === 1) return true;
            if (len >= 4 && h264Data[2] === 0 && h264Data[3] === 1) return true;
        }

        // Fallback: non-empty payload
        const scanLen = Math.min(len, 64);
        for (let i = 0; i < scanLen; i += 1) {
            if (h264Data[i] !== 0) return true;
        }
        return false;
    }

    doSnapshot() {
        let interval = 0;
        this.snapshotFlag = 1
        if (this.snapshotFlag === 0) {
            return 0;
        }

        if (this.snapshotFlag === 1) {
            this.snapshotFlag = 2;
            const video = this.videoElement as HTMLVideoElement;
            const canvas = document.createElement("canvas");
            const canvasCtx = canvas.getContext("2d");
            if (!canvasCtx) {
                console.error("Failed to get canvas 2D context");
                this.snapshotFlag = 0;
                return 0;
            }
            canvas.width = video?.videoWidth || 0;
            canvas.height = video?.videoHeight || 0;
            if (browser.isBrowserFirefox()) {
                const size = canvas.width * canvas.height;
                if (size >= 3000 * 3000) {
                    interval = 1500;
                } else if (size >= 2560 * 2560) {
                    interval = 1200;
                } else if (size >= 1920 * 1920) {
                    interval = 800;
                } else {
                    interval = 200;
                }
            }
            window.setTimeout(() => {
                canvasCtx.drawImage(video, 0, 0, canvas.width, canvas.height);
                canvas.toBlob((blob) => {
                    this.snapshotFlag = 0;
                    const snapshotName = `snapshot_${Date.now()}.jpeg`;
                    if (saveAs) {
                        saveAs(blob as Blob, snapshotName);
                    } else {
                        // Fallback: create download link
                        const url = URL.createObjectURL(blob as Blob);
                        const a = document.createElement('a');
                        a.href = url;
                        a.download = snapshotName;
                        document.body.appendChild(a);
                        a.click();
                        document.body.removeChild(a);
                        URL.revokeObjectURL(url);
                    }
                });
            }, interval);
        }

        return 1;
    }

    stopPlay(): ReturnResult {
        const ret = {
            e: 0,
            m: "Stop h264 Success"
        };

        this.firstFrame = 0;
        this.frameIndex = 0;
        this.lastVideoTime = 0;
        this.isPlayback = false;
        this.playSpeed = 1;
        // this.isPause = 0;
        this.direction = 0;// 0=forward, 1=reverse
        this.playerState = playerStateIdle
        this.snapshotFlag = 0;
        this.lastSec = 0;
        this.videoFrameCnt = 0;
        this.videoPlayer?.uninitMse();
        this.videoPlayer = null;
        if (this.type === 'audio') {
            if (this.videoElement) {
                document.body.removeChild(this.videoElement);
                this.videoElement = null;
            }
        }
        this.type = 'video';
        if (this.decodeWorker !== null) {
            this.decodeWorker.postMessage({ cmd: 'stop' });
            this.decodeWorker.terminate();
            this.decodeWorker = null
        }
        return ret;
    }

    displayLoop(opt: number): void {
        if (opt && this.playerState !== playerStateIdle) {
            requestAnimationFrame(this.displayLoop.bind(this));
        }
        this.videoPlayer?.updateSourceBuffer();
    }

    setPlayMode(opt: boolean): void {
        this.isPlayback = opt;
    }

    /**
     * Get buffered time (how much video is buffered ahead of current playback position)
     */
    private getBufferedTime(): number {
        if (!this.videoElement || this.videoElement.buffered.length === 0) return 0;
        return this.videoElement.buffered.end(this.videoElement.buffered.length - 1) - this.videoElement.currentTime;
    }

    /**
     * Calculate adaptive buffer threshold based on rolling average
     */
    private calculateAdaptiveBufferThreshold(): number {
        const filledEntries = this.bufferTimes.length;
        const sum = this.bufferTimes.reduce((a, b) => a + b, 0);
        const averageBufferTime = filledEntries ? sum / filledEntries : 0;

        // Safari/iOS needs higher threshold due to playback rate issues
        const isSafariOrIOS = browser.isBrowserSafari() || /iPad|iPhone|iPod/.test(navigator.userAgent);
        return averageBufferTime * (isSafariOrIOS ? 3 : 1.5);
    }

    /**
     * Calculate adaptive playback rate to catch up to live edge
     */
    private calculateAdaptivePlaybackRate(bufferTime: number, bufferThreshold: number): number {
        const alpha = 0.2; // aggressiveness of playback rate increase
        const beta = 0.5; // steepness of exponential growth

        // Don't adjust playback rate if we're close enough to live
        // or if we just started streaming
        if (
            ((bufferTime <= bufferThreshold && bufferThreshold < 3) || bufferTime < 3)
            && this.bufferTimes.length <= this.MAX_BUFFER_ENTRIES
        ) {
            return 1;
        }

        const rate = 1 + alpha * Math.exp(beta * (bufferTime - bufferThreshold));
        return Math.min(rate, 2);
    }

    /**
     * Jump to live edge (for Safari/iOS)
     */
    private jumpToLive(): void {
        if (!this.videoElement) return;

        const { buffered } = this.videoElement;
        if (buffered.length > 0) {
            const liveEdge = buffered.end(buffered.length - 1);
            // Jump to the live edge minus a small buffer
            this.videoElement.currentTime = liveEdge - 0.75;
            this.lastJumpTime = Date.now();
        }
    }

    /**
     * Calculate latency (how far behind live we are)
     */
    private calculateLatency(): number {
        if (!this.videoElement) return 0;

        const { seekable } = this.videoElement;
        if (seekable.length > 0) {
            return Math.max(0, seekable.end(seekable.length - 1) - this.videoElement.currentTime);
        }

        // Fallback: use buffered range
        const { buffered } = this.videoElement;
        if (buffered.length > 0) {
            return Math.max(0, buffered.end(buffered.length - 1) - this.videoElement.currentTime);
        }

        return 0;
    }

    /**
     * Calculate bandwidth in kBps
     */
    private calculateBandwidth(): void {
        const now = Date.now();
        const timeElapsed = (now - this.lastBandwidthCheck) / 1000; // seconds

        if (timeElapsed >= 1) {
            const bytesLoaded = this.totalBytesLoaded;
            this.bandwidth = (bytesLoaded - this.lastBytesLoaded) / timeElapsed / 1000; // kBps

            this.lastBytesLoaded = bytesLoaded;
            this.lastBandwidthCheck = now;
        }
    }

    /**
     * Setup progress handler for adaptive buffer management
     */
    private setupProgressHandler(): void {
        if (!this.videoElement) return;

        this.videoElement.addEventListener('progress', () => {
            this.onProgress();
        });

        // Also update stats and recover from playback stall
        setInterval(() => {
            this.calculateBandwidth();
            this.latency = this.calculateLatency();
            if (!this.isPlayback) {
                this.videoPlayer?.recoverIfNeeded();
            }
        }, 1000);
    }

    /**
     * Handle progress events for adaptive buffer management
     */
    private onProgress(): void {
        if (!this.videoElement) return;

        // Live preview: media.ts handles latency via buffer jump; avoid competing playbackRate changes
        if (!this.isPlayback) {
            this.latency = this.calculateLatency();
            return;
        }

        const bufferTime = this.getBufferedTime();

        // Track buffer times in rolling window (only when playback rate is normal or buffer is low)
        if (this.videoElement.playbackRate === 1 || bufferTime < 3) {
            if (this.bufferTimes.length < this.MAX_BUFFER_ENTRIES) {
                this.bufferTimes.push(bufferTime);
            } else {
                this.bufferTimes[this.bufferIndex] = bufferTime;
                this.bufferIndex = (this.bufferIndex + 1) % this.MAX_BUFFER_ENTRIES;
            }
        }

        const bufferThreshold = this.calculateAdaptiveBufferThreshold();

        // Calculate adaptive playback rate
        const playbackRate = this.calculateAdaptivePlaybackRate(bufferTime, bufferThreshold);

        // Apply adaptive strategy based on browser
        const isSafariOrIOS = browser.isBrowserSafari() || /iPad|iPhone|iPod/.test(navigator.userAgent);

        if (this.videoElement && !this.videoElement.paused) {
            if (
                isSafariOrIOS
                && bufferTime > 3
                && Date.now() - this.lastJumpTime > this.BUFFERING_COOLDOWN_TIMEOUT
            ) {
                // Jump to live on Safari/iOS due to playback rate changes causing re-buffering
                this.jumpToLive();
            } else if (!isSafariOrIOS) {
                // Adjust playback rate to compensate for drift - non Safari/iOS only
                if (this.videoElement.playbackRate !== playbackRate) {
                    this.videoElement.playbackRate = playbackRate;
                }
            }
        }

        // Update latency
        this.latency = this.calculateLatency();
    }

    /**
     * Get player statistics
     */
    public getStats(): {
        bandwidth: number;
        latency: number;
        bufferTime: number;
        playbackRate: number;
        packetsPerSecond: number;
    } {
        return {
            bandwidth: this.bandwidth,
            latency: this.latency,
            bufferTime: this.getBufferedTime(),
            playbackRate: this.videoElement?.playbackRate ?? 1,
            packetsPerSecond: this.packetsPerSecond,
        };
    }

    // setPlayPaused(opt: number): void {
    //     this.isPause = opt;
    // }
} 