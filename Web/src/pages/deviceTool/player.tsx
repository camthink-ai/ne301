import { useState, useEffect, useRef } from 'preact/hooks';
import H264Player from '@/lib/MSE/h264Player';
import Loading from '@/components/loading';
import PlayerPanel from './player-panel';
import deviceTool from '@/services/api/deviceTool';
import { toast } from 'sonner';
import { useLingui } from '@lingui/react';
import { setItem } from '@/utils/storage';

type PlayerProps = {
  videoUrl: string;
  videoRendererInstance: React.RefObject<H264Player | null>;
  // setVideoRendererInstance: (inst: H264Player | null) => void;
}

const STREAM_STATS_KEY = 'deviceToolStreamStatsVisible';

export default function Player({ videoUrl, videoRendererInstance }: PlayerProps) {
  const [loading, setLoading] = useState(false);
  const { i18n } = useLingui();
  // isloading and isReload are mutually exclusive
  // const [isReload, setIsReload] = useState(false);
  const [isShowPanel, setIsShowPanel] = useState(false);
  const [isFullscreen, setIsFullscreen] = useState(false);
  const videoRef = useRef<HTMLVideoElement>(null);
  const containerRef = useRef<HTMLDivElement>(null);
  const idleTimerRef = useRef<number | null>(null);
  const [isShowStreamStats, setIsShowStreamStats] = useState<boolean>(() => {
    if (typeof window === 'undefined') return false;
    try {
      const stored = window.localStorage.getItem(STREAM_STATS_KEY);
      return stored === 'true';
    } catch {
      return false;
    }
  });
  const [fps, setFps] = useState(0);
  const [isControlPanel, setIsControlPanel] = useState(false);
  const [latency, setLatency] = useState(0);
  const [bandwidth, setBandwidth] = useState(0);
  const { startVideoStreamReq, stopVideoStreamReq } = deviceTool;
  const [sysTime, setSysTime] = useState('');

  useEffect(() => {
    if (typeof window === 'undefined') return;
    try {
      setItem(STREAM_STATS_KEY, isShowStreamStats);
    } catch {
      // ignore storage errors
    }
  }, [isShowStreamStats]);
  useEffect(() => {
    // Re-check after every await: if the effect is cleaned up (navigate
    // away / videoUrl change) while `startVideoStreamReq` is in flight, the
    // continuation must not create a player nobody will destroy - its
    // workers and auto-reconnect would outlive the page until tab close.
    let cancelled = false;
    const initializePlayer = async () => {
      // Show loading during the initial connection phase
      setLoading(true);
      await startVideoStreamReq();
      if (cancelled || !videoUrl) return;
      const video = videoRef.current; // HTMLVideoElement | null
      if (!video) return;
      const player = new H264Player(() => { });
      videoRendererInstance.current = player;
      player.initPlayer(video);
      player.start(videoUrl);
    };
    initializePlayer();
    return () => {
      cancelled = true;
      videoRendererInstance.current?.destroy();
      videoRendererInstance.current = null;
      stopVideoStreamReq();
    };
  }, [videoUrl]);
  // Add real-time time updates
  useEffect(() => {
    if (!videoRendererInstance.current) return;
    const interval = setInterval(() => {
      const rawTs = videoRendererInstance.current?.currentTime;
      if (rawTs == null || rawTs === 0) {
        setSysTime('');
        const packetsPerSecond = videoRendererInstance.current?.packetsPerSecond ?? 0;
        const currentPackets = videoRendererInstance.current?.packetCount ?? 0;
        setFps(packetsPerSecond || currentPackets);
        return;
      }

      // If timestamp is invalid, do not display time
      const timestamp = rawTs;
      if (timestamp < 1000000000 || timestamp > 2000000000) {
        setSysTime('');
        const packetsPerSecond = videoRendererInstance.current?.packetsPerSecond ?? 0;
        const currentPackets = videoRendererInstance.current?.packetCount ?? 0;
        setFps(packetsPerSecond || currentPackets);
        return;
      }

      // Convert timestamp to year-month-day hour:minute:second (UTC time)
      const date = new Date(timestamp * 1000);
      const year = date.getUTCFullYear();
      const month = String(date.getUTCMonth() + 1).padStart(2, '0');
      const day = String(date.getUTCDate()).padStart(2, '0');
      const hour = String(date.getUTCHours()).padStart(2, '0');
      const minute = String(date.getUTCMinutes()).padStart(2, '0');
      const second = String(date.getUTCSeconds()).padStart(2, '0');
      const newSysTime = `${year}-${month}-${day} ${hour}:${minute}:${second}`;

      setSysTime(newSysTime);
      const packetsPerSecond = videoRendererInstance.current?.packetsPerSecond ?? 0;
      const currentPackets = videoRendererInstance.current?.packetCount ?? 0;
      setFps(packetsPerSecond || currentPackets);

      // Update latency and bandwidth
      const stats = videoRendererInstance.current?.getStats?.();
      if (stats) {
        setLatency(stats.latency);
        setBandwidth(stats.bandwidth);
      }
    }, 1000); // Update every second
    return () => {
      clearInterval(interval);
    };
  }, [videoRendererInstance.current]);

  const handleReload = () => {
    videoRendererInstance.current?.resetStartState().start(videoUrl);
    // setIsReload(false);
    setLoading(false);
  };
  useEffect(() => {
    const handlWvClose = (e: Event) => {
      const event = e as CustomEvent<{ code?: number; reason?: string }>;
      if (event.detail.reason === 'Connection replaced') {
        toast.error(i18n._('sys.device_tool.preview_disconnected'));
      }
      setIsControlPanel(false);
      setLoading(false);
    };

    const handleWvWork = (e: Event) => {
      setLoading(!(e as CustomEvent<boolean>).detail);
      if ((e as CustomEvent<boolean>).detail) {
        setIsControlPanel(true);
      } else {
        setIsControlPanel(false);
      }
    };
    const handleWvError = (e: Event) => {
      // eslint-disable-next-line no-console
      console.error('handleWvError', e);
    };

    window.addEventListener('wv_work', handleWvWork);
    window.addEventListener('wv_close', handlWvClose);
    window.addEventListener('wv_error', handleWvError);
    return () => {
      window.removeEventListener('wv_work', handleWvWork);
      window.removeEventListener('wv_close', handlWvClose);
      window.removeEventListener('wv_error', handleWvError);
    };
  }, [i18n]);
  const handleSnapshot = () => {
    videoRendererInstance.current?.doSnapshot();
  };
  const handleFullscreen = async () => {
    try {
      if (!document.fullscreenElement) {
        await containerRef.current?.requestFullscreen?.();
      } else {
        await document.exitFullscreen();
      }
    } catch {
      setIsFullscreen(false);
    }
  };
  useEffect(() => {
    const onFsChange = () => {
      const isFs = Boolean(document.fullscreenElement);
      setIsFullscreen(isFs);
    };
    document.addEventListener('fullscreenchange', onFsChange);
    return () => document.removeEventListener('fullscreenchange', onFsChange);
  }, []);

  // Double-click to fullscreen
  useEffect(() => {
    const el = videoRef.current;
    if (!el) return;
    const onDbl = () => {
      handleFullscreen().catch(e => {
        // eslint-disable-next-line no-console
        console.error(e);
      });
    };
    el.addEventListener('dblclick', onDbl);
    return () => {
      el.removeEventListener('dblclick', onDbl);
    };
  }, [videoRef, handleFullscreen]);

  // Hide panel on mouse enter
  useEffect(() => {
    const el = containerRef.current;
    if (!el) return;
    const clearIdle = () => {
      if (idleTimerRef.current !== null) {
        clearTimeout(idleTimerRef.current);
        idleTimerRef.current = null;
      }
    };
    const startIdle = () => {
      clearIdle();
      idleTimerRef.current = window.setTimeout(() => {
        setIsShowPanel(false);
      }, 3000);
    };
    const onEnter = () => {
      setIsShowPanel(true);
      startIdle();
    };
    const onLeave = () => {
      setIsShowPanel(false);
      clearIdle();
    };
    const onMove = () => {
      setIsShowPanel(true);
      startIdle();
    };
    el.addEventListener('mouseenter', onEnter);
    el.addEventListener('mouseleave', onLeave);
    el.addEventListener('mousemove', onMove);
    return () => {
      el.removeEventListener('mouseenter', onEnter);
      el.removeEventListener('mouseleave', onLeave);
      el.removeEventListener('mousemove', onMove);
      clearIdle();
    };
  }, []);

  return (
    <div className="w-full h-full">
      <div
        ref={containerRef}
        className="relative flex w-full h-full m-auto justify-center items-center overflow-hidden"
      >
        <div className="w-full h-full flex items-center justify-center bg-black">
          <div className="relative w-full">
            <video
              ref={videoRef}
              className="block w-full aspect-video "
              id="videoPlayer"
              muted
              playsInline
              autoPlay
              disableRemotePlayback
            />
            {/* Time display in the top-left corner of video */}
            {sysTime && (
              <div className="absolute md:top-4 top-2 md:left-4 left-2 bg-gray-800/50 text-white px-3 py-1 rounded text-sm font-mono">
                {sysTime}
              </div>
            )}
            {isShowStreamStats && (
              <div className="absolute md:top-4 top-2 md:right-4 right-2 bg-gray-800/50 px-2 py-1 rounded text-xs font-mono space-y-0.5">
                <div><span className="text-gray-400 font-bold">{i18n._('sys.device_tool.fps')}:</span> <span className="text-white">{fps}</span></div>
                <div><span className="text-gray-400 font-bold">{i18n._('sys.device_tool.latency')}:</span> <span className="text-white">{latency.toFixed(2)}s</span></div>
                <div><span className="text-gray-400 font-bold">{i18n._('sys.device_tool.bandwidth')}:</span> <span className="text-white">{bandwidth.toFixed(1)} kB/s</span></div>
              </div>
            )}
          </div>
        </div>
        <PlayerPanel
          handleReload={handleReload}
          className={`absolute bottom-0 left-0 transition-all duration-300 ease-in-out ${isShowPanel
            ? 'opacity-100 translate-y-0'
            : 'opacity-0 translate-y-full'
            }`}
          isFullscreen={isFullscreen}
          snapshot={handleSnapshot}
          fullscreen={handleFullscreen}
          isControlPanel={isControlPanel}
          isShowStreamStats={isShowStreamStats}
          toggleStreamStats={() => setIsShowStreamStats((prev) => !prev)}
        />
        {loading && (
          <div className="absolute left-1/2 top-1/2 -translate-x-1/2 -translate-y-1/2">
            <Loading placeholder="Loading..." />
          </div>
        )}
      </div>
    </div>
  );
}
