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

const jmuxer: JMuxer = new JMuxer();

function receiveMessage(event: MessageEvent<VideoWorkerMessage>): void {
    const msg = event.data;
    switch (msg.cmd) {
        case 'stop':
            jmuxer.destroy();
            // eslint-disable-next-line no-restricted-globals
            self.close();
            break;
        case 'video':
            if (msg.data) {
                const videoBytes = msg.data instanceof Uint8Array ? msg.data : new Uint8Array(msg.data);
                if (videoBytes.byteLength > 0) {
                    jmuxer.feed({
                        video: videoBytes,
                        time: msg.videoTime,
                        iChannelId: msg.iChannelId,
                        userData: msg.userData,
                    });
                }
            }
            break;
        default:
            break;
    }
}

onmessage = receiveMessage;
