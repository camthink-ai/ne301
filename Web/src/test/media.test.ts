import { afterEach, beforeEach, describe, expect, it, vi } from 'vitest';
import MsMediaSource from '@/lib/MSE/media';

interface MediaInternals {
  sourceBuffer: { buffered: TimeRanges };
  isIOSWebKit: boolean;
  isPlayback: boolean;
  waitingForBuffer: boolean;
  hasStartedPlayback: boolean;
  lastLiveSyncMs: number;
  stallStartedMs: number | null;
  syncLivePreview(liveEdge: number, bufferTime: number): void;
  recoverIfNeeded(): void;
}

function makeRanges(start: number, end: number): TimeRanges {
  return {
    length: 1,
    start: () => start,
    end: () => end,
  };
}

function setupMedia(isIOS: boolean, currentTime: number, liveEdge: number) {
  const media = new MsMediaSource(() => {});
  const video = document.createElement('video');
  video.currentTime = currentTime;
  const play = vi.spyOn(video, 'play').mockResolvedValue();
  media.setVideoElement(video);

  const internals = media as unknown as MediaInternals;
  internals.isIOSWebKit = isIOS;
  internals.isPlayback = false;
  internals.waitingForBuffer = false;
  internals.hasStartedPlayback = true;
  internals.lastLiveSyncMs = 0;
  internals.sourceBuffer = { buffered: makeRanges(0, liveEdge) };

  return { internals, media, play, video };
}

describe('MsMediaSource live catch-up', () => {
  it('uses a low-frequency seek instead of playbackRate on iOS', () => {
    const { internals, play, video } = setupMedia(true, 8.8, 10);

    internals.syncLivePreview(10, 1.2);

    expect(video.currentTime).toBeCloseTo(9.6);
    expect(video.playbackRate).toBe(1);
    expect(play).toHaveBeenCalledOnce();
  });

  it('does not repeatedly seek on iOS during the cooldown', () => {
    const { internals, video } = setupMedia(true, 8.8, 10);
    internals.lastLiveSyncMs = Date.now();

    internals.syncLivePreview(10, 1.2);

    expect(video.currentTime).toBe(8.8);
  });

  it('keeps smooth playbackRate catch-up on desktop browsers', () => {
    const { internals, video } = setupMedia(false, 9.3, 10);

    internals.syncLivePreview(10, 0.7);

    expect(video.currentTime).toBe(9.3);
    expect(video.playbackRate).toBeGreaterThan(1);
  });

  it('does not enter rebuffering for a short iOS waiting event', () => {
    const { internals, video } = setupMedia(true, 9.4, 10);

    video.dispatchEvent(new Event('waiting'));

    expect(internals.stallStartedMs).not.toBeNull();
    expect(internals.waitingForBuffer).toBe(false);
  });
});

class FakeMediaSource extends EventTarget {
  static instances: FakeMediaSource[] = [];

  readyState: ReadyState = 'closed';

  sourceBuffers = [] as unknown as SourceBufferList;

  addSourceBuffer = vi.fn(() => {
    const sourceBuffer = {
      mode: 'segments',
      addEventListener: vi.fn(),
    } as unknown as SourceBuffer;
    (this.sourceBuffers as unknown as SourceBuffer[]).push(sourceBuffer);
    return sourceBuffer;
  });

  removeSourceBuffer = vi.fn();

  constructor() {
    super();
    FakeMediaSource.instances.push(this);
  }
}

describe('MsMediaSource lifecycle', () => {
  const OriginalMediaSource = window.MediaSource;
  const originalCreateObjectURL = window.URL.createObjectURL;
  const originalRevokeObjectURL = window.URL.revokeObjectURL;

  beforeEach(() => {
    FakeMediaSource.instances = [];
    Object.defineProperty(window, 'ManagedMediaSource', {
      configurable: true,
      value: undefined,
    });
    Object.defineProperty(window, 'MediaSource', {
      configurable: true,
      value: FakeMediaSource,
    });
    window.URL.createObjectURL = vi.fn(() => 'blob:test');
    window.URL.revokeObjectURL = vi.fn();
  });

  afterEach(() => {
    Object.defineProperty(window, 'MediaSource', {
      configurable: true,
      value: OriginalMediaSource,
    });
    window.URL.createObjectURL = originalCreateObjectURL;
    window.URL.revokeObjectURL = originalRevokeObjectURL;
  });

  it('ignores a stale sourceopen after the MediaSource is replaced', () => {
    const media = new MsMediaSource(() => {});
    const video = document.createElement('video');
    vi.spyOn(video, 'pause').mockImplementation(() => {});
    media.setVideoElement(video);

    expect(media.initMse('video/mp4; codecs="avc1.42E01E"')).toBe(true);
    const staleMediaSource = FakeMediaSource.instances[0];

    media.uninitMse();
    media.setVideoElement(video);
    expect(media.initMse('video/mp4; codecs="avc1.42E01E"')).toBe(true);
    const currentMediaSource = FakeMediaSource.instances[1];

    staleMediaSource.readyState = 'open';
    staleMediaSource.dispatchEvent(new Event('sourceopen'));
    expect(staleMediaSource.addSourceBuffer).not.toHaveBeenCalled();
    expect(currentMediaSource.addSourceBuffer).not.toHaveBeenCalled();

    currentMediaSource.dispatchEvent(new Event('sourceopen'));
    expect(currentMediaSource.addSourceBuffer).not.toHaveBeenCalled();

    currentMediaSource.readyState = 'open';
    currentMediaSource.dispatchEvent(new Event('sourceopen'));
    expect(currentMediaSource.addSourceBuffer).toHaveBeenCalledOnce();
  });
});
