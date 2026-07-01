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

    // Live preview latency tuning
    private readonly LIVE_TARGET_LATENCY = 0.2;

    private readonly LIVE_SYNC_COOLDOWN_MS = 150;

    private lastLiveSyncMs = 0;

    private lastPlaybackTime = 0;

    private lastPlaybackCheckMs = 0;

    private initSegmentAppended: boolean = false;

    private boundOnVideoStall: (() => void) | null = null;

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
        if (!this.sourceBuffer || this.sourceBuffer.buffered.length === 0) return 0;
        const { buffered } = this.sourceBuffer;
        return buffered.end(buffered.length - 1);
    }

    private syncLivePreview(liveEdge: number, bufferTime: number): void {
        if (!this.videoElement || this.isPlayback) return;
        if (!Number.isFinite(bufferTime) || bufferTime < 0 || liveEdge <= 0) return;

        const now = Date.now();

        // Hard catch-up when lag exceeds 1s (prevents multi-minute drift)
        if (bufferTime > 1) {
            if (now - this.lastLiveSyncMs >= this.LIVE_SYNC_COOLDOWN_MS) {
                this.videoElement.currentTime = Math.max(0, liveEdge - this.LIVE_TARGET_LATENCY);
                this.lastLiveSyncMs = now;
                if (this.videoElement.paused) {
                    void this.videoElement.play();
                }
            }
            if (this.videoElement.playbackRate !== 1) {
                this.videoElement.playbackRate = 1;
            }
            return;
        }

        if (bufferTime > 0.45) {
            const rate = Math.min(1.15, 1 + (bufferTime - 0.45) * 0.15);
            if (Math.abs(this.videoElement.playbackRate - rate) > 0.01) {
                this.videoElement.playbackRate = rate;
            }
        } else if (this.videoElement.playbackRate !== 1) {
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

        if (bufferTime > 1 || playbackStuck || this.videoElement.paused) {
            this.videoElement.currentTime = Math.max(0, liveEdge - this.LIVE_TARGET_LATENCY);
            this.lastLiveSyncMs = now;
            void this.videoElement.play();
        }
    }

    private trackPlaybackAdvance(): void {
        const t = this.videoElement?.currentTime ?? 0;
        if (t !== this.lastPlaybackTime) {
            this.lastPlaybackTime = t;
            this.lastPlaybackCheckMs = Date.now();
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
            this.videoElement?.addEventListener("error", this.videoErrorCallback.bind(this));

            // create mse
            this.mediaSource = new MediaSourceCtor();

            // video url
            if (this.videoElement) {
                this.videoElement.src = window.URL.createObjectURL(this.mediaSource);
            }

            // mse event
            this.mediaSource.addEventListener("sourceopen", () => {
                this.uninitSourceBuffer();
                this.initSourceBuffer();
                this.updateSourceBuffer();
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

            // Mark as destroyed and notify external
            this.initFlag = MsMediaSource.statusDestroy;
            this.cb({ t: 'mseError' });

            // Try to reinitialize MSE (preserve existing mimeCodec and videoElement)
            const codec = this.mimeCodec;
            // First completely clean up to avoid residual state
            this.uninitMse();
            this.initFlag = MsMediaSource.statusIdel;
            if (codec && this.videoElement) {
                // Slight delay to avoid immediate rebuild in the same event loop as error trigger
                setTimeout(() => {
                    // Double check videoElement still exists before reinitializing
                    if (this.videoElement && this.initMse(codec)) {
                        this.initFlag = MsMediaSource.statusNormal;
                        // If there are buffered frames, continue driving playback
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
        const curMode = this.sourceBuffer.mode;
        if (curMode === 'segments') {
            this.sourceBuffer.mode = 'sequence';
        }
        
        this.sourceBuffer.addEventListener("updateend", () => {
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

                    // Preview: do not remove buffered ranges (removal can create gaps and freeze playback)
                    if (this.isPlayback && !this.sourceBuffer.updating) {
                        const bufferEnd = buffered.end(buffered.length - 1);
                        const bufferStart = buffered.start(0);
                        const removeEnd = bufferEnd - this.BUFFER_WINDOW_SIZE;

                        if (removeEnd > bufferStart && currentTime > bufferStart) {
                            const safeRemoveEnd = Math.min(removeEnd, currentTime - 1);
                            if (safeRemoveEnd > bufferStart) {
                                this.sourceBuffer.remove(bufferStart, safeRemoveEnd);

                                if (this.mediaSource && 'setLiveSeekableRange' in this.mediaSource) {
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

        const len = this.frameBuffer.length;
        if (len === 0) {
            return;
        }

        // Live preview: one fMP4 fragment per append to keep MSE timeline in sync
        const batchSize = this.isPlayback ? len : 1;
        const batch = this.frameBuffer.splice(0, batchSize);

        let totalSize = 0;
        for (let i = 0; i < batch.length; i += 1) {
            totalSize += batch[i].data.byteLength;
        }

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
            if (!this.isPlayback) {
                this.initSegmentAppended = true;
            }
            if (this.videoElement?.paused) {
                this.videoElement.style.display = "";
                this.videoElement.play();
                this.cb({
                    t: 'startPlay',
                });
            }
        } catch (e) {
            console.error(`appending error: [update=${this.sourceBuffer.updating}, updateend=${this.updateend}, length=${batch.length}, buffered.length=${this.sourceBuffer.buffered.length}]==>${e}`);
            this.initFlag = MsMediaSource.statusDestroy;
            this.cb({
                t: 'mseError',
            });
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

        if (!this.isPlayback) {
            this.frameBuffer.push(objData);
            // Only drop stale moofs after init segment is in MSE
            if (
                this.initSegmentAppended
                && this.sourceBuffer !== null
                && this.updateend === 1
                && this.frameBuffer.length > 12
            ) {
                this.frameBuffer.splice(0, this.frameBuffer.length - 6);
            }
        } else if (this.frameBuffer.length >= this.MAX_FRAME_BUFFER_SIZE) {
            console.warn(`Frame buffer full (${this.frameBuffer.length}), dropping oldest frames`);
            this.frameBuffer.splice(0, Math.floor(this.MAX_FRAME_BUFFER_SIZE * 0.3));
            this.frameBuffer.push(objData);
        } else {
            this.frameBuffer.push(objData);
        }

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
        this.boundOnVideoStall = () => this.recoverIfNeeded();
        video.addEventListener('waiting', this.boundOnVideoStall);
        video.addEventListener('stalled', this.boundOnVideoStall);
    }

    setPlayMode(playback: boolean): void {
        this.isPlayback = playback;
    }

    clearBuffer(): void {
        this.frameBuffer = [];
        this.lastLiveSyncMs = 0;
        this.lastPlaybackTime = 0;
        this.lastPlaybackCheckMs = 0;
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
        this.initSegmentAppended = false;
        this.clearBuffer();
        if (this.mediaSource || this.initFlag !== MsMediaSource.statusIdel) {
            this.uninitMse();
        }
        this.setVideoElement(video);
        this.initFlag = MsMediaSource.statusIdel;
    }

    uninitMse(): void {
        if (this.videoElement !== null) {
            if (this.boundOnVideoStall) {
                this.videoElement.removeEventListener('waiting', this.boundOnVideoStall);
                this.videoElement.removeEventListener('stalled', this.boundOnVideoStall);
                this.boundOnVideoStall = null;
            }
            this.videoElement.removeEventListener("error", this.videoErrorCallback);
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
    }
}

export default MsMediaSource;