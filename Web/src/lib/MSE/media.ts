import browser from './utils/myBrowser.js';

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

    private videoElement: HTMLVideoElement | null = null;

    private sourceBuffer: SourceBuffer | null = null;

    private frameBuffer: FrameData[] = [];

    private updateend: number = 1;

    private mimeCodec: string = "";

    private initFlag: number = 0;

    private cb: CallbackFunction;

    private currentSegmentIndex: number = 0;

    private isPlayback: boolean = false; // false: preview, true: playback

    // Buffer management optimization
    private readonly MAX_FRAME_BUFFER_SIZE: number = 300; // Limit frame buffer size

    private readonly BUFFER_WINDOW_SIZE: number = 15; // Keep 15 seconds of buffer (Frigate strategy)

    private readonly isSafari: boolean = browser.isBrowserSafari()
        || /iPad|iPhone|iPod/.test(navigator.userAgent);

    // Removing buffered ranges too frequently can make WebKit re-anchor the
    // playhead to an older keyframe. Keep a wider window on Safari.
    private readonly safariBufferWindowSize: number = 45;

    private readonly liveTargetLatency: number = this.isSafari ? 0.6 : 0.35;

    private readonly liveSoftCatchUpLatency: number = this.isSafari ? 0.9 : 0.65;

    private readonly liveHardCatchUpLatency: number = this.isSafari ? 1.5 : 2;

    private readonly liveSyncCooldownMs: number = 2000;

    private readonly stallRecoveryDelayMs: number = this.isSafari ? 3000 : 1200;

    private readonly startupMinBufferedSeconds: number = this.isSafari ? 0.45 : 0.15;

    private readonly startupMinFragments: number = this.isSafari ? 14 : 3;

    private lastLiveSyncMs: number = 0;

    private lastPlaybackTime: number = 0;

    private lastPlaybackAdvanceMs: number = Date.now();

    private lastPlayheadCorrectionMs: number = 0;

    private hasStartedPlayback: boolean = false;

    private playRequestPending: boolean = false;

    private rebuildTimerId: number | null = null;

    private readonly boundVideoErrorCallback = (event: Event) => this.videoErrorCallback(event);

    private readonly boundVideoStallCallback = () => this.recoverLivePreview();

    private readonly boundTimeUpdateCallback = () => this.handlePlaybackTimeUpdate();

    constructor(cb: CallbackFunction) {
        this.cb = cb;
    }

    static get statusIdel(): number { return 0; }

    static get statusWait(): number { return 1; }

    static get statusNormal(): number { return 2; }

    static get statusError(): number { return 3; }

    static get statusDestroy(): number { return 4; }

    initMse(codec: string): boolean {
        // Unified selection of available MediaSource constructor (prefer ManagedMediaSource)
        const MediaSourceCtor = (window.ManagedMediaSource ?? window.MediaSource) as MediaSourceConstructor | undefined;
        if (!MediaSourceCtor) {
            console.error("MediaSource API is not supported!");
            return false;
        }

        if (MediaSourceCtor.isTypeSupported && !MediaSourceCtor.isTypeSupported(codec)) {
            console.error("Unsupported MIME type or codec: ", codec);
            return false;
        }
        this.mimeCodec = codec;

        try {
            this.videoElement?.removeEventListener("error", this.boundVideoErrorCallback);
            this.videoElement?.addEventListener("error", this.boundVideoErrorCallback);

            // create mse
            this.mediaSource = new MediaSourceCtor();

            // video url
            if (this.videoElement) {
                this.videoElement.src = window.URL.createObjectURL(this.mediaSource);
            }

            // mse event
            this.mediaSource.addEventListener("sourceopen", () => {
                console.log("ms mse open.");
                this.uninitSourceBuffer();
                this.initSourceBuffer();
            });

            this.mediaSource.addEventListener("sourceclose", () => {
                console.log("ms mse close.");
            });

            this.mediaSource.addEventListener("sourceended", () => {
                console.log("ms mse ended.");
            });

            this.mediaSource.addEventListener("error", () => {
                console.log("ms mse error.");
            });

            this.mediaSource.addEventListener("abort", () => {
                console.log("ms mse abort.");
            });
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

            // Preserve the <video> node before uninitMse() nulls this.videoElement
            const video = this.videoElement;
            const codec = this.mimeCodec;

            this.initFlag = MsMediaSource.statusDestroy;
            this.cb({ t: 'mseError' });

            this.uninitMse();
            this.initFlag = MsMediaSource.statusIdel;

            if (codec && video) {
                this.rebuildTimerId = window.setTimeout(() => {
                    this.rebuildTimerId = null;
                    // Re-bind the same video element; without this, MSE rebuilds orphaned
                    // and the UI stays black while WS/FPS keep updating.
                    this.setVideoElement(video);
                    if (this.initMse(codec)) {
                        this.initFlag = MsMediaSource.statusNormal;
                        this.updateSourceBuffer();
                    } else {
                        this.initFlag = MsMediaSource.statusError;
                    }
                }, 300);
            }
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

    initSourceBuffer(): number {
        if (this.sourceBuffer !== null) {
            return -1;
        }

        if (!this.mediaSource) {
            return -1;
        }

        this.sourceBuffer = this.mediaSource.addSourceBuffer(this.mimeCodec);
        this.currentSegmentIndex = 0;
        
        this.sourceBuffer.addEventListener("updateend", () => {
            try {
                if (this.sourceBuffer !== null && this.mediaSource?.readyState === 'open' && this.videoElement) {
                    const { buffered } = this.sourceBuffer;
                    // Guard: no ranges available
                    if (buffered.length === 0) {
                        this.updateend = 1;
                        this.updateSourceBuffer();
                        return;
                    }
                    // Clamp currentSegmentIndex
                    if (this.currentSegmentIndex >= buffered.length) {
                        this.currentSegmentIndex = buffered.length - 1;
                    }
                    this.handleTimeUpdate();
                    this.guardMonotonicPlayhead();
                    this.trackPlaybackAdvance();
                    this.ensurePlayheadInBufferedRange();
                    this.syncLivePreview();
                    if (this.isPlayback) {
                        this.startPlaybackIfReady();
                    }

                    // Keep more history on Safari because WebKit may visibly
                    // re-anchor playback when old ranges are removed.
                    if (!this.sourceBuffer.updating && buffered.length > 0) {
                        const bufferEnd = buffered.end(buffered.length - 1);
                        const bufferStart = buffered.start(0);
                        const bufferWindowSize = this.isSafari
                            ? this.safariBufferWindowSize
                            : this.BUFFER_WINDOW_SIZE;
                        const removeEnd = bufferEnd - bufferWindowSize;
                        const keepBehind = this.isSafari ? 10 : 2;
                        const { currentTime } = this.videoElement;

                        // Remove only data safely behind the current playhead.
                        if (removeEnd > bufferStart && currentTime > bufferStart + keepBehind) {
                            const safeRemoveEnd = Math.min(removeEnd, currentTime - keepBehind);
                            if (safeRemoveEnd > bufferStart) {
                                this.sourceBuffer.remove(bufferStart, safeRemoveEnd);

                                // WebKit can move currentTime when the explicit
                                // seekable range start changes, so let Safari
                                // derive it from SourceBuffer.buffered.
                                if (!this.isSafari
                                    && this.mediaSource
                                    && 'setLiveSeekableRange' in this.mediaSource) {
                                    try {
                                        (this.mediaSource as any).setLiveSeekableRange(safeRemoveEnd, bufferEnd);
                                    } catch {
                                        // Ignore if not supported
                                    }
                                }
                            }
                        }
                    }
                }
            } catch (error) {
                console.log(error);
            }
            this.updateend = 1;
            if (!this.sourceBuffer?.updating) {
                this.updateSourceBuffer();
            }
        });

        return 0;
    }

    private getLiveEdge(): number {
        if (!this.sourceBuffer || this.sourceBuffer.buffered.length === 0) return 0;
        return this.sourceBuffer.buffered.end(this.sourceBuffer.buffered.length - 1);
    }

    private trackPlaybackAdvance(): void {
        const currentTime = this.videoElement?.currentTime ?? 0;
        if (currentTime > this.lastPlaybackTime + 0.01) {
            this.lastPlaybackTime = currentTime;
            this.lastPlaybackAdvanceMs = Date.now();
        }
    }

    private isTimeBuffered(time: number, tolerance = 0.05): boolean {
        if (!this.sourceBuffer) return false;
        const { buffered } = this.sourceBuffer;
        for (let i = 0; i < buffered.length; i += 1) {
            if (time >= buffered.start(i) - tolerance && time <= buffered.end(i) + tolerance) {
                return true;
            }
        }
        return false;
    }

    /**
     * Safari may move currentTime backwards after SourceBuffer eviction or a
     * keyframe re-anchor. Live preview is monotonic, so restore the previous
     * high-water mark when it is still buffered.
     */
    private guardMonotonicPlayhead(): void {
        if (!this.isSafari || this.isPlayback || !this.videoElement) return;

        const now = Date.now();
        const { currentTime } = this.videoElement;
        const movedBackwards = this.lastPlaybackTime > 0
            && currentTime < this.lastPlaybackTime - 0.12;

        if (!movedBackwards
            || !this.isTimeBuffered(this.lastPlaybackTime)
            || now - this.lastPlayheadCorrectionMs < 250) {
            return;
        }

        this.videoElement.currentTime = this.lastPlaybackTime;
        this.lastPlayheadCorrectionMs = now;
        this.lastPlaybackAdvanceMs = now;
    }

    private handlePlaybackTimeUpdate(): void {
        this.guardMonotonicPlayhead();
        this.trackPlaybackAdvance();
    }

    private getBufferedDuration(): number {
        if (!this.sourceBuffer) return 0;
        const { buffered } = this.sourceBuffer;
        let duration = 0;
        for (let i = 0; i < buffered.length; i += 1) {
            duration += buffered.end(i) - buffered.start(i);
        }
        return duration;
    }

    private startPlaybackIfReady(): void {
        if (!this.videoElement || !this.sourceBuffer || this.playRequestPending) return;

        const { buffered } = this.sourceBuffer;
        if (buffered.length === 0) return;

        if (!this.isPlayback
            && !this.hasStartedPlayback
            && this.getBufferedDuration() < this.startupMinBufferedSeconds) {
            return;
        }

        if (!this.videoElement.paused) {
            this.hasStartedPlayback = true;
            return;
        }

        this.videoElement.style.display = "";
        this.playRequestPending = true;
        this.videoElement.play()
            .then(() => {
                if (!this.hasStartedPlayback) {
                    this.hasStartedPlayback = true;
                    this.cb({ t: 'startPlay' });
                }
            })
            .catch(() => {
                // Autoplay can be rejected until the page receives user interaction.
            })
            .finally(() => {
                this.playRequestPending = false;
            });
    }

    private syncLivePreview(forceRecovery = false): void {
        if (!this.videoElement || !this.sourceBuffer || this.isPlayback) return;

        const liveEdge = this.getLiveEdge();
        if (liveEdge <= 0) return;

        const lag = liveEdge - this.videoElement.currentTime;
        const now = Date.now();
        const stalled = now - this.lastPlaybackAdvanceMs > this.stallRecoveryDelayMs;
        // A regular Safari seek can decode from the preceding keyframe and look
        // like a rewind. Seek on Safari only as an actual stall recovery.
        const shouldHardSync = this.isSafari
            ? forceRecovery && stalled && lag > this.liveHardCatchUpLatency
            : lag > this.liveHardCatchUpLatency
                || (forceRecovery && stalled && lag > this.liveTargetLatency);

        if (shouldHardSync && now - this.lastLiveSyncMs >= this.liveSyncCooldownMs) {
            const target = Math.max(
                this.videoElement.currentTime,
                liveEdge - this.liveTargetLatency,
            );
            if (target > this.videoElement.currentTime + 0.05) {
                this.videoElement.currentTime = target;
                this.lastLiveSyncMs = now;
            }
        }

        if (this.isSafari) {
            if (this.videoElement.playbackRate !== 1) {
                this.videoElement.playbackRate = 1;
            }
        } else if (lag > this.liveSoftCatchUpLatency && lag <= this.liveHardCatchUpLatency) {
            this.videoElement.playbackRate = 1.05;
        } else if (lag <= this.liveTargetLatency && this.videoElement.playbackRate !== 1) {
            this.videoElement.playbackRate = 1;
        }

        this.startPlaybackIfReady();
    }

    private recoverLivePreview(): void {
        this.syncLivePreview(true);
    }

    /**
     * Move forward into the next buffered range when the playhead is in a real
     * gap. Being close to the live edge is valid and must never trigger a rewind.
     */
    private ensurePlayheadInBufferedRange(): void {
        if (!this.videoElement || !this.sourceBuffer) return;
        const { buffered } = this.sourceBuffer;
        if (buffered.length === 0) return;

        const t = this.videoElement.currentTime;
        const epsilon = 0.05;
        for (let i = 0; i < buffered.length; i += 1) {
            const start = buffered.start(i);
            const end = buffered.end(i);
            if (t >= start - epsilon && t <= end + epsilon) {
                return;
            }
            if (t < start - epsilon) {
                this.videoElement.currentTime = start + 0.01;
                return;
            }
        }
    }

    handleTimeUpdate(): void {
        // Live preview uses a continuous MSE timeline — segment jumping here
        // creates gaps and black frames on Safari. Only relevant for VOD-style ranges.
        if (!this.isPlayback) return;
        if (!this.sourceBuffer || !this.videoElement) return;
        
        const { buffered } = this.sourceBuffer;
        if (buffered.length === 0 || this.currentSegmentIndex === buffered.length - 1) {
            return;
        }
        if (buffered.length && this.currentSegmentIndex >= buffered.length) {
            this.currentSegmentIndex = buffered.length - 1;
            return;
        }
        const nextSegmentIndex = this.currentSegmentIndex + 1;
        const currentEnd = buffered.end(this.currentSegmentIndex);
        const nextStart = buffered.start(nextSegmentIndex);

        this.currentSegmentIndex += 1;
        this.videoElement.currentTime = nextStart;
        this.sourceBuffer.remove(0, currentEnd);
        this.videoElement.play();
    }

    uninitSourceBuffer(): void {
        if (this.sourceBuffer === null || !this.mediaSource) {
            return;
        }
        // this.sourceBuffer.removeEventListener("updateend", this.removeUpdateCallback);
        for (let i = 0; i < this.mediaSource.sourceBuffers.length; i++) {
            this.mediaSource.removeSourceBuffer(this.mediaSource.sourceBuffers[i]);
        }
        this.sourceBuffer = null;
    }

    updateSourceBuffer(): void {
        if (this.sourceBuffer === null || this.updateend !== 1 || this.sourceBuffer.updating) {
            return;
        }

        const queuedLength = this.frameBuffer.length;
        if (queuedLength === 0) {
            return;
        }

        // WebKit is prone to entering `waiting` when playback starts after only
        // one or two fMP4 fragments. Aggregate a short first batch so the first
        // play() call already has a decodable safety margin.
        if (!this.isPlayback
            && !this.hasStartedPlayback
            && this.sourceBuffer.buffered.length === 0
            && queuedLength < this.startupMinFragments) {
            return;
        }

        // Keep appends small and predictable. Large burst appends are expensive
        // on Safari and make latency corrections much more visible.
        let batchSize: number;
        if (this.isPlayback) {
            batchSize = queuedLength;
        } else if (!this.hasStartedPlayback) {
            batchSize = Math.min(queuedLength, this.startupMinFragments + 4);
        } else {
            batchSize = Math.min(queuedLength, queuedLength > 12 ? 6 : 3);
        }
        const batch = this.frameBuffer.splice(0, batchSize);

        let totalSize = 0;
        for (let i = 0; i < batch.length; i++) {
            totalSize += batch[i].data.byteLength;
        }

        const segmentBuffer = new Uint8Array(totalSize);
        let offset = 0;

        for (let i = 0; i < batch.length; i++) {
            const frameData = new Uint8Array(batch[i].data);
            segmentBuffer.set(frameData, offset);
            offset += frameData.byteLength;
        }

        try {
            this.sourceBuffer.appendBuffer(segmentBuffer);
            this.updateend = 0;
        } catch (e) {
            console.error(`appending error: [update=${this.sourceBuffer.updating}, updateend=${this.updateend}, length=${batch.length}, buffered.length=${this.sourceBuffer.buffered.length}]==>${e}`);
            const video = this.videoElement;
            const savedCodec = this.mimeCodec;
            this.cb({
                t: 'mseError',
            });
            // Rebuild MSE attached to the same <video>; otherwise append failures leave a black screen
            if (video && savedCodec) {
                this.uninitMse();
                this.initFlag = MsMediaSource.statusIdel;
                this.setVideoElement(video);
                this.rebuildTimerId = window.setTimeout(() => {
                    this.rebuildTimerId = null;
                    if (this.videoElement && this.initMse(savedCodec)) {
                        this.initFlag = MsMediaSource.statusNormal;
                        this.updateSourceBuffer();
                    } else {
                        this.initFlag = MsMediaSource.statusError;
                    }
                }, 300);
            } else {
                this.initFlag = MsMediaSource.statusDestroy;
            }
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

        // Buffer size limit: prevent memory overflow
        if (this.frameBuffer.length >= this.MAX_FRAME_BUFFER_SIZE) {
            // Drop oldest frames if buffer is full (backpressure)
            console.warn(`Frame buffer full (${this.frameBuffer.length}), dropping oldest frames`);
            this.frameBuffer.splice(0, Math.floor(this.MAX_FRAME_BUFFER_SIZE * 0.3)); // Drop 30%
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
        if (this.videoElement && this.videoElement !== video) {
            this.videoElement.removeEventListener('waiting', this.boundVideoStallCallback);
            this.videoElement.removeEventListener('stalled', this.boundVideoStallCallback);
            this.videoElement.removeEventListener('timeupdate', this.boundTimeUpdateCallback);
            this.videoElement.removeEventListener('error', this.boundVideoErrorCallback);
        }
        this.videoElement = video;
        video.removeEventListener('waiting', this.boundVideoStallCallback);
        video.removeEventListener('stalled', this.boundVideoStallCallback);
        video.removeEventListener('timeupdate', this.boundTimeUpdateCallback);
        video.addEventListener('waiting', this.boundVideoStallCallback);
        video.addEventListener('stalled', this.boundVideoStallCallback);
        video.addEventListener('timeupdate', this.boundTimeUpdateCallback);
    }

    setPlayMode(playback: boolean): void {
        this.isPlayback = playback;
    }

    clearBuffer(): void {
        // Clear frame buffer to stop processing new frames
        this.frameBuffer = [];
        this.lastLiveSyncMs = 0;
        this.lastPlaybackTime = 0;
        this.lastPlaybackAdvanceMs = Date.now();
        this.lastPlayheadCorrectionMs = 0;
        this.hasStartedPlayback = false;
        this.playRequestPending = false;
        // Clear source buffer if it exists and is not updating
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

    uninitMse(): void {
        if (this.rebuildTimerId !== null) {
            window.clearTimeout(this.rebuildTimerId);
            this.rebuildTimerId = null;
        }
        if (this.videoElement !== null) {
            this.videoElement.removeEventListener("error", this.boundVideoErrorCallback);
            this.videoElement.removeEventListener('waiting', this.boundVideoStallCallback);
            this.videoElement.removeEventListener('stalled', this.boundVideoStallCallback);
            this.videoElement.removeEventListener('timeupdate', this.boundTimeUpdateCallback);
            window.URL.revokeObjectURL(this.videoElement.src);
            this.videoElement.src = "";
        }

        this.uninitSourceBuffer();
        this.mediaSource = null;
        this.videoElement = null;
        this.sourceBuffer = null;
        this.frameBuffer = [];
        this.updateend = 1;
        this.mimeCodec = "";
        this.initFlag = MsMediaSource.statusIdel;
        this.lastLiveSyncMs = 0;
        this.lastPlaybackTime = 0;
        this.lastPlaybackAdvanceMs = Date.now();
        this.lastPlayheadCorrectionMs = 0;
        this.hasStartedPlayback = false;
        this.playRequestPending = false;
    }
}

export default MsMediaSource;
