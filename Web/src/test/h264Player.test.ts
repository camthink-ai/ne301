import { beforeAll, describe, expect, it, vi } from 'vitest';
import { previewFrameMs } from '@/lib/MSE/constant';

type PlayerConstructor = typeof import('@/lib/MSE/h264Player').default;
let H264Player: PlayerConstructor;

beforeAll(async () => {
  const appendChild = vi.spyOn(document.head, 'appendChild').mockImplementation(node => node);
  H264Player = (await import('@/lib/MSE/h264Player')).default;
  appendChild.mockRestore();
});

function makeFrame(timestampSeconds = 1_700_000_000): ArrayBuffer {
  const headerSize = 64;
  const payload = new Uint8Array([0, 0, 0, 1, 0x65, 0x88, 0x84]);
  const frame = new ArrayBuffer(headerSize + payload.byteLength);
  const view = new DataView(frame);
  view.setUint32(8, 0, false);
  view.setUint32(12, timestampSeconds, false);
  new Uint8Array(frame, headerSize).set(payload);
  return frame;
}

describe('H264Player preview framing', () => {
  it('uses a stable millisecond frame duration for JMuxer', () => {
    const player = new H264Player(() => {});

    const first = player.dealVideoData(makeFrame());
    const second = player.dealVideoData(makeFrame());

    expect(first).not.toBe(false);
    expect(second).not.toBe(false);
    if (first && second) {
      expect(first.videoTime).toBe(previewFrameMs);
      expect(second.videoTime).toBe(previewFrameMs);
    }
  });

  it('reads the server timestamp and strips the WebSocket header', () => {
    const player = new H264Player(() => {});
    const timestamp = 1_700_000_123;

    const result = player.dealVideoData(makeFrame(timestamp));

    expect(result).not.toBe(false);
    expect(player.currentTime).toBe(timestamp);
    if (result) {
      expect(Array.from(result.data)).toEqual([0, 0, 0, 1, 0x65, 0x88, 0x84]);
    }
  });

  it('rejects payloads without an Annex-B start code', () => {
    expect(H264Player.checkFrameData(new Uint8Array([1, 2, 3, 4]))).toBe(false);
  });
});
