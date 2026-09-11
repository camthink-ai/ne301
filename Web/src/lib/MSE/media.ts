import { logStreamError } from '@/services/request';

// Type definitions
interface FrameData {
    data: ArrayBuffer;
    codec?: string;
}

interface CallbackEvent {
    t: 'mseError' | 'startPlay';
}

type CallbackFunction = (event: CallbackEvent) => void;

interface Mp4EventData {
    data: ArrayBuffer;
    codec: string;
}

type MediaSourceConstructor = {
    new (): MediaSource;
    isTypeSupported?(codec: string): boolean;
};
declare global {
    interface Window {
        ManagedMediaSource?: MediaSourceConstructor;
    }
}
class MsMediaSource {
    private mediaSource: MediaSource | null = null;

    private mediaSourceCleanup: (() => void) | null = null;

    private mseGeneration: number = 0;

    private videoElement: HTMLVideoElement | null = null;

    private sourceBuffer: SourceBuffer | null = null;

    private frameBuffer: FrameData[] = [];

    private updateend: number = 1;

    private mimeCodec: string = "";

    private initFlag: number = 0;

    private cb: CallbackFunction;

    private currentSegmentIndex: number = 0;

    private isPlayback: boolean = false; // false: preview, true: playback

    // Live preview latency tuning
    private readonly LIVE_TARGET_LATENCY = 0.35;

    private readonly STARTUP_BUFFER_SECONDS = 0.35;

    private readonly REBUFFER_SECONDS = 0.45;

    private readonly LIVE_SYNC_COOLDOWN_MS = 150;

    // iOS WebKit renders playbackRate catch-up unreliably, so live preview uses
    // low-frequency seeks to the live edge and tolerates short rebuffer events.
    private readonly IOS_LIVE_TARGET_LATENCY = 0.4;

    private readonly IOS_SEEK_COOLDOWN_MS = 2500;

    private readonly IOS_STALL_REBUFFER_MS = 2000;

    // Preview keeps a short buffered history (live latency is ~0.4s); bounding it
    // stops the SourceBuffer from growing unbounded and exhausting iOS's small
    // media quota (desktop Chrome's ~150MB quota hides the same leak much longer).
    private readonly PREVIEW_BUFFER_WINDOW: number = 10;

    // On QuotaExceededError, trim to a tiny window to reclaim space fast.
    private readonly QUOTA_RECOVERY_WINDOW: number = 2;

    // Cap one appendBuffer at ~200KB: iOS WebKit stalls on very large appends,
    // and a stalled append backs frames up into an even larger next append.
    // Splitting only ever happens at fragment boundaries.
    private readonly MAX_APPEND_BYTES: number = 200 * 1024;

    private lastLiveSyncMs = 0;

    private lastPlaybackTime = 0;

    private lastPlaybackCheckMs = 0;

    private waitingForBuffer = true;

    private hasStartedPlayback = false;

    // iOS WebKit flag (auto-detected, overridable in tests). When true the player
    // prefers low-frequency seeks over playbackRate ramps for live catch-up.
    private isIOSWebKit: boolean = typeof navigator !== 'undefined'
        && (/iPad|iPhone|iPod/.test(navigator.userAgent)
            || (navigator.platform === 'MacIntel' && (navigator.maxTouchPoints ?? 0) > 1));

    private stallStartedMs: number | null = null;

    private boundOnVideoStall: (() => void) | null = null;

    private readonly boundVideoErrorCallback = (event: Event) => this.videoErrorCallback(event);

    // Buffer management optimization
    private readonly MAX_FRAME_BUFFER_SIZE: number = 60;

    private readonly BUFFER_WINDOW_SIZE: number = 15; // Keep 15 seconds of buffer (Frigate strategy)

    constructor(cb: CallbackFunction) {
        this.cb = cb;
    }

    static get statusIdel(): number { return 0; }

    static get statusWait(): number { return 1; }

    static get statusNormal(): number { return 2; }

    static get statusError(): number { return 3; }

    static get statusDestroy(): number { return 4; }

    static get skipCount(): number { return 5; } // Frame skip catch-up count

    private getLiveEdge(): number {
        if (!this.sourceBuffer) return 0;
        try {
            const { buffered } = this.sourceBuffer;
            if (buffered.length === 0) return 0;
            return buffered.end(buffered.length - 1);
        } catch {
            // SourceBuffer detached from its MediaSource during teardown;
            // treat as "no buffer" so recoverIfNeeded() backs off.
            return 0;
        }
    }

    private syncLivePreview(liveEdge: number, bufferTime: number): void {
        if (!this.videoElement || this.isPlayback) return;
        if (!Number.isFinite(bufferTime) || bufferTime < 0 || liveEdge <= 0) return;

        const now = Date.now();

        if (this.waitingForBuffer) {
            const requiredBuffer = this.hasStartedPlayback
                ? this.REBUFFER_SECONDS
                : this.STARTUP_BUFFER_SECONDS;
            if (bufferTime < requiredBuffer) return;

            this.videoElement.currentTime = Math.max(0, liveEdge - this.LIVE_TARGET_LATENCY);
            this.videoElement.playbackRate = 1;
            this.waitingForBuffer = false;
            this.videoElement.play();
            if (!this.hasStartedPlayback) {
                this.hasStartedPlayback = true;
                this.cb({ t: 'startPlay' });
            }
            return;
        }

        if (this.isIOSWebKit) {
            this.syncIOSLivePreview(liveEdge, bufferTime, now);
            return;
        }

        // Hard catch-up only for a real backlog; ordinary jitter is absorbed by
        // the live cache instead of causing repeated seeks.
        if (bufferTime > 1.2) {
            if (now - this.lastLiveSyncMs >= this.LIVE_SYNC_COOLDOWN_MS) {
                this.videoElement.currentTime = Math.max(0, liveEdge - this.LIVE_TARGET_LATENCY);
                this.lastLiveSyncMs = now;
                if (this.videoElement.paused) {
                    this.videoElement.play();
                }
            }
            if (this.videoElement.playbackRate !== 1) {
                this.videoElement.playbackRate = 1;
            }
            return;
        }

        if (bufferTime > 0.55) {
            const rate = Math.min(1.08, 1 + (bufferTime - 0.55) * 0.12);
            if (Math.abs(this.videoElement.playbackRate - rate) > 0.01) {
                this.videoElement.playbackRate = rate;
            }
        } else if (this.videoElement.playbackRate !== 1) {
            this.videoElement.playbackRate = 1;
        }
    }

    /**
     * iOS WebKit live catch-up: seek to the live edge instead of ramping the
     * playbackRate, and only promote a short stall to a full rebuffer once it has
     * persisted past the tolerance window.
     */
    private syncIOSLivePreview(liveEdge: number, bufferTime: number, now: number): void {
        if (!this.videoElement) return;

        if (this.stallStartedMs !== null && now - this.stallStartedMs >= this.IOS_STALL_REBUFFER_MS) {
            this.stallStartedMs = null;
            this.waitingForBuffer = true;
            return;
        }

        if (bufferTime > 0.55 && now - this.lastLiveSyncMs >= this.IOS_SEEK_COOLDOWN_MS) {
            this.videoElement.currentTime = Math.max(0, liveEdge - this.IOS_LIVE_TARGET_LATENCY);
            this.videoElement.playbackRate = 1;
            this.lastLiveSyncMs = now;
            this.videoElement.play();
        }

        if (this.videoElement.playbackRate !== 1) {
            this.videoElement.playbackRate = 1;
        }
    }

    /** Recover when video stalls but stream data is still arriving */
    recoverIfNeeded(): void {
        if (!this.videoElement || !this.sourceBuffer || this.isPlayback) return;

        const liveEdge = this.getLiveEdge();
        if (liveEdge <= 0) return;

        const bufferTime = liveEdge - this.videoElement.currentTime;
        const now = Date.now();
        const timeSinceAdvance = now - this.lastPlaybackCheckMs;
        const playbackStuck = timeSinceAdvance > 2000
            && Math.abs(this.videoElement.currentTime - this.lastPlaybackTime) < 0.05;

        if (playbackStuck || this.videoElement.paused) {
            this.waitingForBuffer = true;
        }

        if (bufferTime > 1.2) {
            this.videoElement.currentTime = Math.max(0, liveEdge - this.LIVE_TARGET_LATENCY);
            this.lastLiveSyncMs = now;
            this.waitingForBuffer = false;
            this.videoElement.play();
        }
    }

    private trackPlaybackAdvance(): void {
        const t = this.videoElement?.currentTime ?? 0;
        if (t !== this.lastPlaybackTime) {
            this.lastPlaybackTime = t;
            this.lastPlaybackCheckMs = Date.now();
            this.stallStartedMs = null;
        }
    }

    initMse(codec: string): boolean {
        // Unified selection of available MediaSource constructor (prefer ManagedMediaSource)
        const MediaSourceCtor = (window.ManagedMediaSource ?? window.MediaSource) as MediaSourceConstructor | undefined;
        if (!MediaSourceCtor) {
            console.error("MediaSource API is not supported!");
            return false;
        }

        // if (!window.MediaSource.isTypeSupported(codec)) {
        //     console.log(codec);
        //     console.error("Unsupported MIME type or codec: ", codec);
        //     return false;
        // }
        this.mimeCodec = codec;

        try {
            // create video
            this.videoElement?.addEventListener("error", this.boundVideoErrorCallback);

            // Bind every callback to the exact MediaSource that created it. An
            // older sourceopen can arrive after a watchdog restart on Chrome.
            const mediaSource = new MediaSourceCtor();
            const generation = ++this.mseGeneration;
            this.mediaSource = mediaSource;

            // video url
            if (this.videoElement) {
                this.videoElement.src = window.URL.createObjectURL(mediaSource);
            }

            // mse event
            const onSourceOpen = () => {
                if (generation !== this.mseGeneration
                    || this.mediaSource !== mediaSource
                    || mediaSource.readyState !== 'open') {
                    return;
                }

                this.uninitSourceBuffer(mediaSource);
                if (this.initSourceBuffer(mediaSource) !== 0) return;
                this.updateSourceBuffer();
            };

            const onSourceClose = () => {
                console.log("ms mse close.");
            };

            const onSourceEnded = () => {
                console.log("ms mse ended.");
            };

            const onError = () => {
                console.log("ms mse error.");
            };

            const onAbort = () => {
                console.log("ms mse abort.");
            };

            mediaSource.addEventListener("sourceopen", onSourceOpen);
            mediaSource.addEventListener("sourceclose", onSourceClose);
            mediaSource.addEventListener("sourceended", onSourceEnded);
            mediaSource.addEventListener("error", onError);
            mediaSource.addEventListener("abort", onAbort);
            this.mediaSourceCleanup = () => {
                mediaSource.removeEventListener("sourceopen", onSourceOpen);
                mediaSource.removeEventListener("sourceclose", onSourceClose);
                mediaSource.removeEventListener("sourceended", onSourceEnded);
                mediaSource.removeEventListener("error", onError);
                mediaSource.removeEventListener("abort", onAbort);
            };
        } catch (e) {
            console.log((e as Error).message);
            return false;
        }

        this.videoElement?.pause();
        return true;
    }

    videoErrorCallback(e: Event): void {
        try {
            const target = e.target as HTMLVideoElement;
            if (target?.error) {
                // Suppress errors during cleanup (when src is empty or element is being destroyed)
                if (target.src === '' || !this.videoElement) {
                    return;
                }
                
                switch (target.error.code) {
                    case target.error.MEDIA_ERR_ABORTED:
                        // Suppress abort errors during cleanup
                        return;
                    case target.error.MEDIA_ERR_NETWORK:
                        console.error("video tag error : A network error caused the media download to fail.");
                        break;
                    case target.error.MEDIA_ERR_DECODE:
                        console.error("video tag error : The media playback was aborted due to a corruption problem or because the media used features your browser did not support.");
                        break;
                    case target.error.MEDIA_ERR_SRC_NOT_SUPPORTED:
                        console.error("video tag error : The media could not be loaded, either because the server or network failed or because the format is not supported.");
                        break;
                    default:
                        console.error(`video tag error : An unknown media error occurred.${target.error.code}`);
                        break;
                }
            }

            // Only try to reinitialize if videoElement still exists and is not being destroyed
            if (!this.videoElement || this.initFlag === MsMediaSource.statusDestroy) {
                return;
            }

            // Signal fatal pipeline loss. In-place MSE rebuilds reliably re-error
            // on WebKit (soak logs: every quick recovery fails, every full stream
            // restart with a fresh WS connection succeeds) - H264Player restarts
            // the whole stream instead.
            logStreamError('VIDEO', 'video-error', String(target?.error?.code ?? ''), target?.error?.message || 'video element error');
            this.initFlag = MsMediaSource.statusDestroy;
            this.cb({ t: 'mseError' });
        } catch {
            // Ignore errors during cleanup
        }
    }

    static makeBuffer(buffer1: Uint8Array, buffer2: Uint8Array): Uint8Array {
        const tmp = new Uint8Array(buffer1.byteLength + buffer2.byteLength);
        tmp.set(new Uint8Array(buffer1), 0);
        tmp.set(new Uint8Array(buffer2), buffer1.byteLength);
        return tmp;
    }

    initSourceBuffer(mediaSource: MediaSource | null = this.mediaSource): number {
        if (this.sourceBuffer !== null) {
            return -1;
        }

        if (!mediaSource
            || mediaSource !== this.mediaSource
            || mediaSource.readyState !== 'open') {
            return -1;
        }

        let sourceBuffer: SourceBuffer;
        try {
            sourceBuffer = mediaSource.addSourceBuffer(this.mimeCodec);
        } catch (error) {
            console.error('Failed to initialize MediaSource SourceBuffer', error);
            this.initFlag = MsMediaSource.statusError;
            return -1;
        }

        if (mediaSource !== this.mediaSource || mediaSource.readyState !== 'open') {
            return -1;
        }

        this.sourceBuffer = sourceBuffer;
        this.currentSegmentIndex = 0;
        const curMode = sourceBuffer.mode;
        if (curMode === 'segments') {
            sourceBuffer.mode = 'sequence';
        }
        
        sourceBuffer.addEventListener("updateend", () => {
            if (this.mediaSource !== mediaSource || this.sourceBuffer !== sourceBuffer) return;
            try {
                if (this.sourceBuffer !== null && this.mediaSource?.readyState === 'open' && this.videoElement) {
                    const { buffered } = this.sourceBuffer;
                    if (buffered.length === 0) {
                        this.updateend = 1;
                        this.updateSourceBuffer();
                        return;
                    }

                    const liveEdge = buffered.end(buffered.length - 1);
                    const { currentTime } = this.videoElement;
                    this.trackPlaybackAdvance();

                    if (!this.isPlayback) {
                        this.syncLivePreview(liveEdge, liveEdge - currentTime);
                    }

                    // Bound the buffered window. Playback trims to a 15s history;
                    // live preview keeps ~10s behind the playhead. Without this
                    // the preview SourceBuffer grows unbounded and exhausts iOS's
                    // small media quota (desktop Chrome's ~150MB quota just hides
                    // it much longer).
                    this.trimBuffered(this.isPlayback ? this.BUFFER_WINDOW_SIZE : this.PREVIEW_BUFFER_WINDOW);
                }
            } catch (error) {
                console.error(error);
            }
            this.updateend = 1;
            this.updateSourceBuffer();
        });

        return 0;
    }

    handleTimeUpdate(): void {
        if (!this.sourceBuffer || !this.videoElement) return;
        
        const { buffered } = this.sourceBuffer;
        if (buffered.length === 0 || this.currentSegmentIndex === buffered.length - 1 || this.isPlayback) {
            return;
        }
        if (buffered.length && this.currentSegmentIndex >= buffered.length) {
            this.currentSegmentIndex = buffered.length - 1;
            return;
        }
        const nextSegmentIndex = this.currentSegmentIndex + 1;
        const currentEnd = buffered.end(this.currentSegmentIndex);
        const nextStart = buffered.start(nextSegmentIndex);

        // Playback mode only: advance across buffered segments
        this.currentSegmentIndex += 1;
        this.videoElement.currentTime = nextStart;
        this.sourceBuffer.remove(0, currentEnd);
        this.videoElement.play();
    }

    uninitSourceBuffer(mediaSource: MediaSource | null = this.mediaSource): void {
        const { sourceBuffer } = this;
        if (sourceBuffer === null) {
            return;
        }

        this.sourceBuffer = null;
        if (!mediaSource || mediaSource.readyState !== 'open') return;

        try {
            if (Array.from(mediaSource.sourceBuffers).includes(sourceBuffer)) {
                mediaSource.removeSourceBuffer(sourceBuffer);
            }
        } catch (error) {
            console.warn('Failed to remove MediaSource SourceBuffer', error);
        }
    }

    /**
     * Remove buffered data older than `windowSeconds` behind the playhead.
     * Bounds the SourceBuffer so it never grows unbounded (iOS's media quota is
     * small enough to exhaust in minutes of live preview). Never removes past
     * (currentTime - 1s) since the decoder may still be reading there.
     */
    private trimBuffered(windowSeconds: number): void {
        if (!this.sourceBuffer || this.sourceBuffer.updating || !this.videoElement) return;

        let bufferStart: number;
        let bufferEnd: number;
        try {
            const { buffered } = this.sourceBuffer;
            if (buffered.length === 0) return;
            bufferStart = buffered.start(0);
            bufferEnd = buffered.end(buffered.length - 1);
        } catch {
            // SourceBuffer detached mid-flight; nothing to trim.
            return;
        }

        const { currentTime } = this.videoElement;
        const removeEnd = Math.min(bufferEnd - windowSeconds, currentTime - 1);
        if (removeEnd <= bufferStart) return;

        this.sourceBuffer.remove(bufferStart, removeEnd);

        if (this.mediaSource && 'setLiveSeekableRange' in this.mediaSource) {
            try {
                (this.mediaSource as any).setLiveSeekableRange(removeEnd, bufferEnd);
            } catch {
                // Ignore if not supported
            }
        }
    }

    updateSourceBuffer(): void {
        if (this.sourceBuffer === null || this.updateend !== 1 || this.sourceBuffer.updating) {
            return;
        }
        if (this.initFlag === MsMediaSource.statusDestroy) {
            // Pipeline is down pending a full stream restart; stop feeding it.
            return;
        }

        if (this.frameBuffer.length === 0) {
            return;
        }

        // Take at least one fragment, and only up to MAX_APPEND_BYTES worth, so a
        // single appendBuffer never grows huge enough to stall iOS WebKit and back
        // frames up into an even larger next append. Splitting only happens at
        // fragment boundaries (each entry is one complete fMP4 fragment).
        let totalSize = 0;
        let count = 0;
        for (let i = 0; i < this.frameBuffer.length; i += 1) {
            const size = this.frameBuffer[i].data.byteLength;
            if (count > 0 && totalSize + size > this.MAX_APPEND_BYTES) break;
            totalSize += size;
            count += 1;
        }
        const batch = this.frameBuffer.splice(0, count);

        const segmentBuffer = new Uint8Array(totalSize);
        let offset = 0;

        for (let i = 0; i < batch.length; i += 1) {
            const frameData = new Uint8Array(batch[i].data);
            segmentBuffer.set(frameData, offset);
            offset += frameData.byteLength;
        }

        try {
            this.sourceBuffer.appendBuffer(segmentBuffer);
            this.updateend = 0;
            if (this.isPlayback && this.videoElement?.paused) {
                this.videoElement.style.display = "";
                this.videoElement.play();
                this.cb({
                    t: 'startPlay',
                });
            }
        } catch (e) {
            const errName = (e as Error)?.name || '';
            if (errName === 'QuotaExceededError') {
                // Reclaim space by trimming the buffered window and retry the
                // SAME batch on the next updateend. The batch may hold the
                // init segment or a keyframe - dropping it would leave every
                // following frame undecodable until the next keyframe (a
                // visible stream gap), so push it back to the head of the
                // queue for the post-trim retry.
                console.warn('MSE quota exceeded; trimming buffered window');
                logStreamError('VIDEO', 'quota', errName, 'buffer full; trimmed');
                this.frameBuffer = batch.concat(this.frameBuffer);
                this.trimBuffered(this.QUOTA_RECOVERY_WINDOW);
                return;
            }
            // Any other append failure: signal fatal pipeline loss (the player
            // restarts the whole stream; in-place rebuilds re-error on WebKit).
            // Do not touch sourceBuffer here.
            console.error(`appending error: [updateend=${this.updateend}, length=${batch.length}]==>${e}`);
            logStreamError('VIDEO', 'append-error', errName, String(e));
            this.initFlag = MsMediaSource.statusDestroy;
            this.cb({ t: 'mseError' });
        }
    }

    processMp4VideoData(event: { data: Mp4EventData }, snapshotFlag: number): void {
        const objData = event.data;

        if (this.initFlag === MsMediaSource.statusIdel) {
            this.frameBuffer = [];
            this.initFlag = MsMediaSource.statusWait;
            if (this.initMse(objData.codec)) {
                this.initFlag = MsMediaSource.statusNormal;
            } else {
                this.initFlag = MsMediaSource.statusError;
            }
        }

        if (this.frameBuffer.length >= this.MAX_FRAME_BUFFER_SIZE) {
            console.warn(`Frame buffer full (${this.frameBuffer.length}), dropping oldest frames`);
            this.frameBuffer.splice(0, Math.floor(this.MAX_FRAME_BUFFER_SIZE * 0.3));
        }
        this.frameBuffer.push(objData);

        if (snapshotFlag === 0) {
            this.updateSourceBuffer();
        }
    }

    processMp4AudioData(event: { data: Mp4EventData }): void {
        const objData = event.data;

        if (this.initFlag === MsMediaSource.statusIdel) {
            this.frameBuffer = [];
            this.initFlag = MsMediaSource.statusWait;
            if (this.initMse(objData.codec)) {
                this.initFlag = MsMediaSource.statusNormal;
            } else {
                this.initFlag = MsMediaSource.statusError;
            }
        }

        // Buffer size limit for audio as well
        if (this.frameBuffer.length >= this.MAX_FRAME_BUFFER_SIZE) {
            console.warn(`Audio frame buffer full (${this.frameBuffer.length}), dropping oldest frames`);
            this.frameBuffer.splice(0, Math.floor(this.MAX_FRAME_BUFFER_SIZE * 0.3));
        }

        this.frameBuffer.push(objData);
        this.updateSourceBuffer();
    }

    setVideoElement(video: HTMLVideoElement): void {
        if (this.videoElement && this.boundOnVideoStall) {
            this.videoElement.removeEventListener('waiting', this.boundOnVideoStall);
            this.videoElement.removeEventListener('stalled', this.boundOnVideoStall);
        }
        this.videoElement = video;
        this.boundOnVideoStall = () => {
            if (this.isIOSWebKit) {
                // Debounce short iOS rebuffer events; syncIOSLivePreview promotes a
                // persistent stall to a full rebuffer after the tolerance window.
                if (this.stallStartedMs === null) this.stallStartedMs = Date.now();
                return;
            }
            this.waitingForBuffer = true;
            this.recoverIfNeeded();
        };
        video.addEventListener('waiting', this.boundOnVideoStall);
        video.addEventListener('stalled', this.boundOnVideoStall);
    }

    /**
     * Switch between live preview (false) and recorded playback (true).
     * Drives live-edge seeking, buffer-window size and the playback-specific
     * startup path in updateSourceBuffer()/handleSourceOpen().
     *
     * NOT WIRED UP YET: recorded playback is not implemented - H264Player's
     * setPlayMode (the only intended caller) is itself dead code today, so
     * this stays false and every stream is treated as a live preview. When
     * playback lands, H264Player must forward its mode here (see the
     * setPlayMode note in h264Player.ts).
     */
    setPlayMode(playback: boolean): void {
        this.isPlayback = playback;
    }

    clearBuffer(): void {
        this.frameBuffer = [];
        this.lastLiveSyncMs = 0;
        this.lastPlaybackTime = 0;
        this.lastPlaybackCheckMs = 0;
        this.waitingForBuffer = true;
        this.hasStartedPlayback = false;
        this.stallStartedMs = null;
        if (this.sourceBuffer && !this.sourceBuffer.updating && this.mediaSource && this.mediaSource.readyState === 'open') {
            try {
                const { buffered } = this.sourceBuffer;
                if (buffered.length > 0) {
                    const end = buffered.end(buffered.length - 1);
                    this.sourceBuffer.remove(0, end);
                }
            } catch {
                // Ignore errors during buffer clearing
            }
        }
        this.currentSegmentIndex = 0;
    }

    resetLivePreview(video: HTMLVideoElement): void {
        this.clearBuffer();
        if (this.mediaSource || this.initFlag !== MsMediaSource.statusIdel) {
            this.uninitMse();
        }
        this.setVideoElement(video);
        this.initFlag = MsMediaSource.statusIdel;
    }

    uninitMse(): void {
        this.mseGeneration++;
        this.mediaSourceCleanup?.();
        this.mediaSourceCleanup = null;

        if (this.videoElement !== null) {
            if (this.boundOnVideoStall) {
                this.videoElement.removeEventListener('waiting', this.boundOnVideoStall);
                this.videoElement.removeEventListener('stalled', this.boundOnVideoStall);
                this.boundOnVideoStall = null;
            }
            this.videoElement.removeEventListener("error", this.boundVideoErrorCallback);
            window.URL.revokeObjectURL(this.videoElement.src);
            this.videoElement.src = "";
        }

        this.uninitSourceBuffer(this.mediaSource);
        this.mediaSource = null;
        this.videoElement = null;
        this.sourceBuffer = null;
        this.frameBuffer = [];
        this.updateend = 1;
        this.mimeCodec = "";
        this.initFlag = MsMediaSource.statusIdel;
    }
}

export default MsMediaSource;
