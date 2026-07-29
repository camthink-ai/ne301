import { describe, expect, it, vi } from 'vitest';
import MsMediaSource from '@/lib/MSE/media';

interface MediaInternals {
  sourceBuffer: {
    buffered: TimeRanges;
  };
  isSafari: boolean;
  isPlayback: boolean;
  liveTargetLatency: number;
  liveHardCatchUpLatency: number;
  stallRecoveryDelayMs: number;
  startupMinBufferedSeconds: number;
  lastPlaybackTime: number;
  lastPlaybackAdvanceMs: number;
  lastPlayheadCorrectionMs: number;
  handlePlaybackTimeUpdate(): void;
  startPlaybackIfReady(): void;
  syncLivePreview(forceRecovery?: boolean): void;
}

function makeRanges(start: number, end: number): TimeRanges {
  return {
    length: 1,
    start: () => start,
    end: () => end,
  };
}

function setupSafariMedia(currentTime: number, rangeEnd: number) {
  const media = new MsMediaSource(() => {});
  const video = document.createElement('video');
  video.currentTime = currentTime;
  media.setVideoElement(video);

  const internals = media as unknown as MediaInternals;
  internals.isSafari = true;
  internals.isPlayback = false;
  internals.liveTargetLatency = 0.6;
  internals.liveHardCatchUpLatency = 1.5;
  internals.stallRecoveryDelayMs = 3000;
  internals.startupMinBufferedSeconds = 0.45;
  internals.sourceBuffer = {
    buffered: makeRanges(0, rangeEnd),
  };

  return { internals, video };
}

describe('MsMediaSource Safari live playhead', () => {
  it('waits for a safe Safari startup buffer before playing', async () => {
    const { internals, video } = setupSafariMedia(0, 0.2);
    const play = vi.spyOn(video, 'play').mockResolvedValue();

    internals.startPlaybackIfReady();
    expect(play).not.toHaveBeenCalled();

    internals.sourceBuffer.buffered = makeRanges(0, 0.5);
    internals.startPlaybackIfReady();
    await Promise.resolve();

    expect(play).toHaveBeenCalledOnce();
    play.mockRestore();
  });

  it('restores a buffered high-water mark after WebKit moves backwards', () => {
    const { internals, video } = setupSafariMedia(8.6, 10);
    internals.lastPlaybackTime = 9;
    internals.lastPlayheadCorrectionMs = 0;

    internals.handlePlaybackTimeUpdate();

    expect(video.currentTime).toBe(9);
  });

  it('does not seek merely because normal Safari playback is behind live', () => {
    const { internals, video } = setupSafariMedia(6, 10);
    internals.lastPlaybackTime = 6;
    internals.lastPlaybackAdvanceMs = Date.now();
    const play = vi.spyOn(video, 'play').mockResolvedValue();

    internals.syncLivePreview(false);

    expect(video.currentTime).toBe(6);
    play.mockRestore();
  });

  it('does not seek for a short Safari waiting event', () => {
    const { internals, video } = setupSafariMedia(6, 10);
    internals.lastPlaybackTime = 6;
    internals.lastPlaybackAdvanceMs = Date.now() - 1500;
    const play = vi.spyOn(video, 'play').mockResolvedValue();

    internals.syncLivePreview(true);

    expect(video.currentTime).toBe(6);
    play.mockRestore();
  });
});
