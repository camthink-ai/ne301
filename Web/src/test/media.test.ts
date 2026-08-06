import { describe, expect, it, vi } from 'vitest';
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