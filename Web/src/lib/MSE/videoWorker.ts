// JMuxer type definitions
interface JMuxerConfig {
    node?: string | HTMLVideoElement;
    mode?: 'video' | 'audio' | 'both';
    flushingTime?: number;
    clearBuffer?: boolean;
    fps?: number;
    debug?: boolean;
    onReady?: () => void;
    onError?: (error: any) => void;
}

interface JMuxerFeedData {
    video?: ArrayBuffer | Uint8Array;
    audio?: ArrayBuffer;
    time?: number;
    iChannelId?: number;
    userData?: any;
}

interface JMuxer {
    feed(data: JMuxerFeedData): void;
    destroy(): void;
}

// Message type definitions
interface VideoWorkerMessage {
    cmd: 'stop' | 'video';
    data?: ArrayBuffer | Uint8Array;
    videoTime?: number;
    iChannelId?: number;
    userData?: any;
}

// Global JMuxer declaration
declare const JMuxer: {
    new(config?: JMuxerConfig): JMuxer;
};

// @ts-expect-error: importScripts is only available in worker context and jmuxer is UMD
importScripts('/libs/jmuxer.min.js');

const jmuxer: JMuxer = new JMuxer({ fps: 30 });
let animationFrameId: number | null = null;
let jmuxerCmd: MessageEvent<VideoWorkerMessage>[] = [];

function receiveMessage(event: MessageEvent<VideoWorkerMessage>): void {
    if (animationFrameId === null) {
        animationFrameId = requestAnimationFrame(dealJmuxerCmd);
    }
    jmuxerCmd.push(event);
}

function dealJmuxerCmd(): void {
    while (jmuxerCmd.length > 0) {
        const event = jmuxerCmd.shift();
        if (event === undefined) {
            break;
        }
        const msg = event.data;

        switch (msg.cmd) {
            case 'stop':
                jmuxer.destroy();
                if (animationFrameId !== null) {
                    cancelAnimationFrame(animationFrameId);
                    animationFrameId = null;
                }
                jmuxerCmd = [];
                // eslint-disable-next-line no-restricted-globals
                self.close();
                return;
            case 'video':
                if (msg.data) {
                    const videoBytes: Uint8Array = msg.data instanceof Uint8Array
                        ? msg.data
                        : new Uint8Array(msg.data);
                    if (videoBytes.byteLength === 0) break;

                    jmuxer.feed({
                        video: videoBytes,
                        time: msg.videoTime,
                        iChannelId: msg.iChannelId,
                        userData: msg.userData,
                    });
                }
                break;
            default:
                break;
        }
    }

    animationFrameId = null;
}

onmessage = receiveMessage;
