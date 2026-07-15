import { useEffect, useState, useRef } from 'react';
import { useLingui } from '@lingui/react';
import { Label } from '@/components/ui/label';
import { Separator } from '@/components/ui/separator';
import { Skeleton } from '@/components/ui/skeleton';
import { Switch } from '@/components/ui/switch';
import { Button } from '@/components/ui/button';
import { Tooltip, TooltipContent, TooltipTrigger } from '@/components/tooltip';
// import Slider from '@/components/slider'; // ISP group (temporarily disabled)
import { useSystemInfo } from '@/store/systemInfo';
import deviceTool from '@/services/api/deviceTool';
import H264Player from '@/lib/MSE/h264Player';
import Loading from '@/components/loading';
import hardwareManagement, {
  ISP_MODE_DAY,
  ISP_MODE_NIGHT,
  ISP_MODE_AUTO,
  type IspMode,
} from '@/services/api/hardware-management';
import DayNightAutoConfig from './day-night-auto';
import SvgIcon from '@/components/svg-icon';
import { Input } from '@/components/ui/input';
import {
  Dialog,
  DialogContent,
  DialogFooter,
  DialogHeader,
  DialogTitle,
} from '@/components/dialog';
import {
  Select,
  SelectContent,
  SelectItem,
  SelectTrigger,
  SelectValue,
} from '@/components/ui/select';
import { getWebSocketUrl } from '@/utils';

export default function Graphics() {
  const { i18n } = useLingui();
  const { deviceInfo } = useSystemInfo();
  const {
    getHardwareInfoReq,
    setHardwareInfoReq,
  } = hardwareManagement;
  const { toggleAiReq, startVideoStreamReq, stopVideoStreamReq } = deviceTool;
  const [connectionStatus, setConnectionStatus] = useState('');
  const [brightness, setBrightness] = useState(0);
  const [contrast, setContrast] = useState(0);
  const [flipHorizontal, setFlipHorizontal] = useState(false);
  const [flipVertical, setFlipVertical] = useState(false);
  const [aec, setAec] = useState(0);
  const [fastSkipFrames, setFastSkipFrames] = useState(0);
  const [fastResolution, setFastResolution] = useState(0);
  const [fastJpegQuality, setFastJpegQuality] = useState(85);
  const [captureDisableComm, setCaptureDisableComm] = useState(false);
  const [captureStorageAi, setCaptureStorageAi] = useState(false);
  const [cameraSectionOpen, setCameraSectionOpen] = useState(true);
  const [captureSectionOpen, setCaptureSectionOpen] = useState(true);
  const [ispMode, setIspMode] = useState<IspMode>(0);
  const [luxRefDialogOpen, setLuxRefDialogOpen] = useState(false);
  const [luxRefForm, setLuxRefForm] = useState({
    calibFactor: 0,
    hlRef: 0,
    hlExpo1: 0,
    hlLum1: 0,
    hlExpo2: 0,
    hlLum2: 0,
    llRef: 0,
    llExpo1: 0,
    llLum1: 0,
    llExpo2: 0,
    llLum2: 0,
  });
  // ISP state (temporarily disabled with ISP group)
  // const [ispSectionOpen, setIspSectionOpen] = useState(true);
  // const [ispExposureModeAuto, setIspExposureModeAuto] = useState(true);
  // const [ispExposureComp, setIspExposureComp] = useState(0);
  // const [ispAntiFlicker, setIspAntiFlicker] = useState<0 | 50 | 60>(0);
  // const [ispSensorDelay, setIspSensorDelay] = useState(0);
  // const [ispEstimatedLux, setIspEstimatedLux] = useState<number | null>(null);
  // const [ispManualSensorGain, setIspManualSensorGain] = useState(0);
  // const [ispManualExposureTime, setIspManualExposureTime] = useState(0);
  const playerRef = useRef<HTMLVideoElement>(null);
  const [playerLoading, setPlayerLoading] = useState(false);
  const [loading, setLoading] = useState(false);
  let playerRender: H264Player | null = null;
  // ----------Skeleton screen
  const skeletonScreen = () => (
    <div className="flex flex-col gap-2 w-full">
      <div className="flex justify-between gap-4">
        <Skeleton className="w-10 h-8" />
        <Skeleton className="w-auto flex-1 h-8" />
      </div>
      <div className="flex justify-between gap-4">
        <Skeleton className="w-10 h-8" />
        <Skeleton className="w-auto flex-1 h-8" />
      </div>
      <div className="flex justify-between">
        <Skeleton className="w-10 h-8" />
        <Skeleton className="w-10 h-8" />
      </div>
      <div className="flex justify-between gap-2">
        <Skeleton className="w-10 h-8" />
        <Skeleton className="w-10 h-8" />
      </div>
    </div>
  );
  const initData = () => (deviceInfo?.camera_module
      ? setConnectionStatus('connected')
      : setConnectionStatus('disconnected'));
  const initPlayer = async () => {
    try {
      setPlayerLoading(true);
      await startVideoStreamReq();
      if (!playerRef.current) return;
      const video = playerRef.current;
      playerRender = new H264Player(() => {});
      playerRender.initPlayer(video);
      playerRender.start(getWebSocketUrl());
    } catch (error) {
      console.error(error);
    } finally {
      setPlayerLoading(false);
    }
  };
  useEffect(() => {
    (async () => {
      initData();
      await toggleAiReq({ ai_enabled: false });
      initPlayer();
    })();
    return () => {
      playerRender?.destroy();
      playerRender = null;
      stopVideoStreamReq();
    };
  }, [deviceInfo]);

  const initHardwareInfo = async () => {
    try {
      setLoading(true);
      const res = await getHardwareInfoReq();
      setBrightness(res.data.brightness);
      setContrast(res.data.contrast);
      setFlipHorizontal(res.data.horizontal_flip);
      setFlipVertical(res.data.vertical_flip);
      setAec(res.data.aec);
      if (typeof res.data.fast_capture_skip_frames === 'number') {
        setFastSkipFrames(res.data.fast_capture_skip_frames);
      }
      if (typeof res.data.fast_capture_resolution === 'number') {
        setFastResolution(res.data.fast_capture_resolution);
      }
      if (typeof res.data.fast_capture_jpeg_quality === 'number') {
        setFastJpegQuality(res.data.fast_capture_jpeg_quality);
      }
      if (typeof res.data.capture_disable_comm === 'boolean') {
        setCaptureDisableComm(res.data.capture_disable_comm);
      }
      if (typeof res.data.capture_storage_ai === 'boolean') {
        setCaptureStorageAi(res.data.capture_storage_ai);
      }
      if (typeof res.data.isp_mode === 'number') {
        setIspMode(res.data.isp_mode);
      }
      // TODO: when ISP APIs are wired, hydrate ISP exposure state from /api/v1/isp/aec, /aec/manual, /sensor_delay, /statistics, etc.
    } catch (error) {
      console.error(error);
    } finally {
      setLoading(false);
    }
  };
  useEffect(() => {
    initHardwareInfo();
  }, []);

  const buildImageConfigRequest = (
    overrides: Partial<{
      brightness: number;
      contrast: number;
      horizontal_flip: boolean;
      vertical_flip: boolean;
      aec: number;
      isp_mode: IspMode;
      fast_capture_skip_frames: number;
      fast_capture_resolution: number;
      fast_capture_jpeg_quality: number;
      capture_disable_comm: boolean;
      capture_storage_ai: boolean;
    }> = {}
  ) => ({
    brightness: overrides.brightness ?? brightness,
    contrast: overrides.contrast ?? contrast,
    horizontal_flip: overrides.horizontal_flip ?? flipHorizontal,
    vertical_flip: overrides.vertical_flip ?? flipVertical,
    aec: overrides.aec ?? aec,
    isp_mode: overrides.isp_mode ?? ispMode,
    fast_capture_skip_frames:
      overrides.fast_capture_skip_frames ?? fastSkipFrames,
    fast_capture_resolution:
      overrides.fast_capture_resolution ?? fastResolution,
    fast_capture_jpeg_quality:
      overrides.fast_capture_jpeg_quality ?? fastJpegQuality,
    capture_disable_comm: overrides.capture_disable_comm ?? captureDisableComm,
    capture_storage_ai: overrides.capture_storage_ai ?? captureStorageAi,
  });

  const handleFastCaptureChange = (
    type: 'fast_skip_frames' | 'fast_jpeg_quality',
    rawValue: number
  ) => {
    let value = rawValue;
    if (Number.isNaN(value)) {
      value = 0;
    }
    if (type === 'fast_skip_frames') {
      if (value < 0) value = 0;
      else if (value > 300) value = 300;
      setFastSkipFrames(value);
    } else if (type === 'fast_jpeg_quality') {
      if (value < 1) value = 1;
      else if (value > 100) value = 100;
      setFastJpegQuality(value);
    }
  };

  const postFastCapture = async (
    type: 'fast_skip_frames' | 'fast_jpeg_quality',
    rawValue: number
  ) => {
    // Keep exactly aligned with deviceTool: always submit the incoming value for this request
    let value = rawValue;
    if (Number.isNaN(value)) {
      value = 0;
    }
    if (type === 'fast_skip_frames') {
      if (value < 0) value = 0;
      else if (value > 300) value = 300;
    } else if (value < 0) value = 0;
      else if (value > 100) value = 100;

    const nextSkip = type === 'fast_skip_frames' ? value : fastSkipFrames;
    const nextQuality = type === 'fast_jpeg_quality' ? value : fastJpegQuality;

    try {
      await setHardwareInfoReq(
        buildImageConfigRequest({
          fast_capture_skip_frames: nextSkip,
          fast_capture_jpeg_quality: nextQuality,
        })
      );
    } catch (error) {
      console.error(error);
    }
  };

  const handleSetHardwareInfo = async (type: string, value: number) => {
    // compute next state first to avoid sending outdated values
    let nextBrightness = brightness;
    let nextContrast = contrast;
    let nextFlipHorizontal = flipHorizontal;
    let nextFlipVertical = flipVertical;
    let nextAec = aec;
    const nextFastSkipFrames = fastSkipFrames;
    let nextFastResolution = fastResolution;
    const nextFastJpegQuality = fastJpegQuality;

    switch (type) {
      case 'brightness':
        nextBrightness = value > 100 ? 100 : value < 0 ? 0 : value;
        setBrightness(nextBrightness);
        break;
      case 'contrast':
        nextContrast = value > 100 ? 100 : value < 0 ? 0 : value;
        setContrast(nextContrast);
        break;
      case 'flip_horizontal':
        nextFlipHorizontal = !flipHorizontal;
        setFlipHorizontal(nextFlipHorizontal);
        break;
      case 'flip_vertical':
        nextFlipVertical = !flipVertical;
        setFlipVertical(nextFlipVertical);
        break;
      case 'aec':
        nextAec = value;
        setAec(value);
        break;
      case 'fast_resolution':
        nextFastResolution = value;
        setFastResolution(value);
        break;
      default:
        break;
    }

    try {
      await setHardwareInfoReq(
        buildImageConfigRequest({
          brightness: nextBrightness,
          contrast: nextContrast,
          horizontal_flip: nextFlipHorizontal,
          vertical_flip: nextFlipVertical,
          aec: nextAec,
          fast_capture_skip_frames: nextFastSkipFrames,
          fast_capture_resolution: nextFastResolution,
          fast_capture_jpeg_quality: nextFastJpegQuality,
        })
      );
    } catch (error) {
      console.error(error);
    }
  };

  // const handleIspExposureModeChange = (auto: boolean) => {
  //   setIspExposureModeAuto(auto);
  // };

  return (
    <div>
      {loading && skeletonScreen()}
      {!loading && (
        <div
          className={`flex flex-col gap-2 mt-4 ${loading ? '' : 'bg-gray-100'} p-4 rounded-lg`}
        >
          <div className="flex justify-between">
            <Label>{i18n._('sys.hardware_management.connection_status')}</Label>
            <div className="flex items-center gap-2">
              <div
                className={`w-2 h-2 rounded-full ${connectionStatus === 'connected' ? 'bg-green-500' : 'bg-gray-500'}`}
              >
              </div>
              <p>{i18n._(`common.${connectionStatus}`)}</p>
            </div>
          </div>
          {connectionStatus === 'connected' && (
            <>
              <Separator />
              <div className="flex justify-between relative bg-black">
                <video
                  ref={playerRef as React.Ref<HTMLVideoElement>}
                  id="player"
                  className="w-full aspect-video"
                  muted
                  playsInline
                  disableRemotePlayback
                />
                {playerLoading && (
                  <div className="absolute left-1/2 top-1/2 -translate-x-1/2 -translate-y-1/2">
                    <Loading placeholder="Loading..." />
                  </div>
                )}
              </div>
              {loading ? (
                skeletonScreen()
              ) : (
                <>
                  <Separator />
                  {/* Camera configuration (flip etc.) */}
                  <div className="flex flex-col gap-2">
                    <button
                      type="button"
                      className="flex w-full items-center justify-between text-left"
                      onClick={() => setCameraSectionOpen(o => !o)}
                    >
                      <Label>
                        {i18n._('sys.hardware_management.camera_config')}
                      </Label>
                      <span className="inline-flex h-4 w-4 shrink-0 items-center justify-center text-gray-500">
                        <SvgIcon
                          icon="right"
                          className={`h-4 w-4 transition-transform duration-200 ${
                            cameraSectionOpen ? 'rotate-90' : 'rotate-0'
                          }`}
                        />
                      </span>
                    </button>
                    {cameraSectionOpen && (
                      <div className="border border-gray-200 border-solid p-4 rounded-md mt-2 flex flex-col gap-2">
                        <div className="flex justify-between">
                          <Label>
                            {i18n._('sys.hardware_management.flip_horizontal')}
                          </Label>
                          <Switch
                            checked={flipHorizontal}
                            onCheckedChange={() => handleSetHardwareInfo(
                                'flip_horizontal',
                                flipHorizontal ? 1 : 0
                              )}
                          />
                        </div>
                        <Separator />
                        <div className="flex justify-between">
                          <Label>
                            {i18n._('sys.hardware_management.flip_vertical')}
                          </Label>
                          <Switch
                            checked={flipVertical}
                            onCheckedChange={() => handleSetHardwareInfo(
                                'flip_vertical',
                                flipVertical ? 1 : 0
                              )}
                          />
                        </div>
                        <Separator />
                        <div className="flex justify-between gap-4 items-center">
                          <div className="flex min-w-0 flex-1 items-center gap-2">
                            <Label className="shrink-0">
                              {i18n._('sys.hardware_management.isp_mode')}
                            </Label>
                            <Tooltip mbEnhance>
                              <TooltipTrigger>
                                <div className="inline-flex h-4 w-4 shrink-0 items-center justify-center text-gray-500">
                                  <SvgIcon
                                    className="h-4 w-4 text-gray-500"
                                    icon="info"
                                  />
                                </div>
                              </TooltipTrigger>
                              <TooltipContent className="max-w-80 text-pretty">
                                <div>
                                  <p>
                                    {i18n._(
                                      'sys.hardware_management.isp_mode_realtime_hint'
                                    )}
                                  </p>
                                </div>
                              </TooltipContent>
                            </Tooltip>
                          </div>
                          <Select
                            value={String(ispMode)}
                            onValueChange={async v => {
                              const next = Number(v) as IspMode;
                              setIspMode(next);
                              try {
                                await setHardwareInfoReq(
                                  buildImageConfigRequest({ isp_mode: next })
                                );
                              } catch (error) {
                                console.error(error);
                              }
                            }}
                          >
                            <SelectTrigger className="w-28 border-0 shadow-none focus-visible:ring-0">
                              <SelectValue />
                            </SelectTrigger>
                            <SelectContent>
                              <SelectItem value={String(ISP_MODE_DAY)}>
                                {i18n._('sys.hardware_management.isp_mode_day')}
                              </SelectItem>
                              <SelectItem value={String(ISP_MODE_NIGHT)}>
                                {i18n._('sys.hardware_management.isp_mode_night')}
                              </SelectItem>
                              <SelectItem value={String(ISP_MODE_AUTO)}>
                                {i18n._('sys.hardware_management.isp_mode_auto')}
                              </SelectItem>
                            </SelectContent>
                          </Select>
                        </div>
                        {ispMode === ISP_MODE_AUTO && <DayNightAutoConfig />}
                      </div>
                    )}
                  </div>
                  <Separator />
                  {/* Capture configuration (fast capture) */}
                  <div className="flex flex-col gap-2">
                    <button
                      type="button"
                      className="flex w-full items-center justify-between text-left"
                      onClick={() => setCaptureSectionOpen(o => !o)}
                    >
                      <div className="flex items-center gap-2">
                        <Label>
                          {i18n._('sys.hardware_management.capture_config')}
                        </Label>
                        <Tooltip mbEnhance>
                          <TooltipTrigger>
                            <div className="inline-flex h-4 w-4 shrink-0 items-center justify-center text-gray-500">
                              <SvgIcon
                                className="h-4 w-4 text-gray-500"
                                icon="info"
                              />
                            </div>
                          </TooltipTrigger>
                          <TooltipContent className="max-w-80 text-pretty">
                            <div>
                              <p>
                                {i18n._(
                                  'sys.hardware_management.capture_config_tip'
                                )}
                              </p>
                            </div>
                          </TooltipContent>
                        </Tooltip>
                      </div>
                      <span className="inline-flex h-4 w-4 shrink-0 items-center justify-center text-gray-500">
                        <SvgIcon
                          icon="right"
                          className={`h-4 w-4 transition-transform duration-200 ${
                            captureSectionOpen ? 'rotate-90' : 'rotate-0'
                          }`}
                        />
                      </span>
                    </button>
                    {captureSectionOpen && (
                      <div className="border border-gray-200 border-solid p-4 rounded-md mt-2 flex flex-col gap-2">
                        <div className="flex justify-between gap-4 items-center">
                          <Label>
                            {i18n._(
                              'sys.hardware_management.fast_capture_skip_frames'
                            )}
                          </Label>
                          <Input
                            className="w-24 text-right"
                            type="number"
                            min={0}
                            max={300}
                            value={fastSkipFrames}
                            onChange={e => {
                              const raw = parseInt(
                                (e.target as HTMLInputElement).value || '0',
                                10
                              );
                              handleFastCaptureChange('fast_skip_frames', raw);
                              // set input value to input element
                              (e.target as HTMLInputElement).value =                                String(fastSkipFrames);
                            }}
                            onBlur={e => {
                              const raw = parseInt(
                                (e.target as HTMLInputElement).value || '0',
                                10
                              );
                              postFastCapture('fast_skip_frames', raw);
                              // set input value to input element
                              (e.target as HTMLInputElement).value =                                String(fastSkipFrames);
                            }}
                          />
                        </div>
                        <Separator />
                        <div className="flex justify-between gap-4 items-center">
                          <Label>
                            {i18n._(
                              'sys.hardware_management.fast_capture_resolution'
                            )}
                          </Label>
                          <Select
                            value={String(fastResolution)}
                            onValueChange={value => {
                              const v = Number(value || 0);
                              handleSetHardwareInfo('fast_resolution', v);
                            }}
                          >
                            <SelectTrigger className="border-0 shadow-none focus-visible:ring-0 focus-visible:border-transparent w-32">
                              <SelectValue />
                            </SelectTrigger>
                            <SelectContent>
                              <SelectItem value="0">1280x720</SelectItem>
                              <SelectItem value="1">1920x1080</SelectItem>
                              <SelectItem value="2">2688x1520</SelectItem>
                            </SelectContent>
                          </Select>
                        </div>
                        <Separator />
                        <div className="flex justify-between gap-4 items-center">
                          <div className="flex min-w-0 flex-1 items-center gap-2">
                            <Label className="shrink-0">
                              {i18n._(
                                'sys.hardware_management.fast_capture_jpeg_quality'
                              )}
                            </Label>
                            <Tooltip mbEnhance>
                              <TooltipTrigger>
                                <div className="inline-flex h-4 w-4 shrink-0 items-center justify-center text-gray-500">
                                  <SvgIcon
                                    className="h-4 w-4 text-gray-500"
                                    icon="info"
                                  />
                                </div>
                              </TooltipTrigger>
                              <TooltipContent className="max-w-80 text-pretty">
                                <div>
                                  <p>
                                    {i18n._(
                                      'sys.hardware_management.capture_jpeg_quality_tip'
                                    )}
                                  </p>
                                </div>
                              </TooltipContent>
                            </Tooltip>
                          </div>
                          <Input
                            className="w-24 text-right"
                            type="number"
                            min={1}
                            max={100}
                            value={fastJpegQuality}
                            onChange={e => {
                              const raw = parseInt(
                                (e.target as HTMLInputElement).value || '0',
                                10
                              );
                              handleFastCaptureChange('fast_jpeg_quality', raw);
                              // set input value to input element
                              (e.target as HTMLInputElement).value =                                String(fastJpegQuality);
                            }}
                            onBlur={e => {
                              const raw = parseInt(
                                (e.target as HTMLInputElement).value || '0',
                                10
                              );
                              postFastCapture('fast_jpeg_quality', raw);
                              // set input value to input element
                              (e.target as HTMLInputElement).value =                                String(fastJpegQuality);
                            }}
                          />
                        </div>
                        {/*
                        <Separator />
                        <div className="flex justify-between gap-4 items-center">
                          <Label>
                            {i18n._('sys.hardware_management.capture_disable_comm')}
                          </Label>
                          <Switch
                            checked={captureDisableComm}
                            onCheckedChange={async (checked) => {
                              setCaptureDisableComm(checked);
                              try {
                                await setHardwareInfoReq(
                                  buildImageConfigRequest({ capture_disable_comm: checked }),
                                );
                              } catch (error) {
                                console.error(error);
                                setCaptureDisableComm(!checked);
                              }
                            }}
                          />
                        </div>
                        <Separator />
                        <div className="flex justify-between gap-4 items-center">
                          <Label>
                            {i18n._('sys.hardware_management.capture_storage_ai')}
                          </Label>
                          <Switch
                            checked={captureStorageAi}
                            onCheckedChange={async (checked) => {
                              setCaptureStorageAi(checked);
                              try {
                                await setHardwareInfoReq(
                                  buildImageConfigRequest({ capture_storage_ai: checked }),
                                );
                              } catch (error) {
                                console.error(error);
                                setCaptureStorageAi(!checked);
                              }
                            }}
                          />
                        </div>
                        */}
                      </div>
                    )}
                  </div>
                  <Separator />
                  {/* ISP configuration (temporarily disabled)
                  <div className="flex flex-col gap-2">
                    <button
                      type="button"
                      className="flex justify-between items-center w-full text-left"
                      onClick={() => setIspSectionOpen(!ispSectionOpen)}
                    >
                      <Label>{i18n._('sys.hardware_management.isp_config')}</Label>
                      <span className="w-4 h-4 text-gray-500">
                        <SvgIcon
                          icon="right"
                          className={`w-4 h-4 transition-transform duration-200 ${
                            ispSectionOpen ? 'rotate-90' : 'rotate-0'
                          }`}
                        />
                      </span>
                    </button>
                    {ispSectionOpen && (
                      <div className="border border-gray-200 border-solid p-4 rounded-md mt-2 flex flex-col gap-3">
                        // Exposure block 
                        <div className="flex flex-col gap-3">
                          <div className="flex justify-between gap-4 items-center">
                            <Label>{i18n._('sys.hardware_management.isp_exposure_mode')}</Label>
                            <div className="flex gap-2">
                              <button
                                type="button"
                                className={`px-3 py-1 rounded border text-sm ${
                                  ispExposureModeAuto ? 'bg-primary text-white border-primary' : 'bg-white text-text-primary border-gray-200'
                                }`}
                                onClick={() => handleIspExposureModeChange(true)}
                              >
                                {i18n._('sys.hardware_management.isp_exposure_mode_auto')}
                              </button>
                              <button
                                type="button"
                                className={`px-3 py-1 rounded border text-sm ${
                                  !ispExposureModeAuto ? 'bg-primary text-white border-primary' : 'bg-white text-text-primary border-gray-200'
                                }`}
                                onClick={() => handleIspExposureModeChange(false)}
                              >
                                {i18n._('sys.hardware_management.isp_exposure_mode_manual')}
                              </button>
                            </div>
                          </div>
                          <div className="flex justify-between gap-4 items-center">
                            <Label>{i18n._('sys.hardware_management.isp_exposure_comp')}</Label>
                            <Select
                              disabled={!ispExposureModeAuto}
                              value={String(ispExposureComp)}
                              onValueChange={(value) => {
                                const v = Number(value || 0);
                                setIspExposureComp(v);
                              }}
                            >
                              <SelectTrigger className="border-0 shadow-none focus-visible:ring-0 focus-visible:border-transparent w-28">
                                <SelectValue />
                              </SelectTrigger>
                              <SelectContent>
                                <SelectItem value="-2">-2.0 EV</SelectItem>
                                <SelectItem value="-1.5">-1.5 EV</SelectItem>
                                <SelectItem value="-1">-1.0 EV</SelectItem>
                                <SelectItem value="-0.5">-0.5 EV</SelectItem>
                                <SelectItem value="0">0 EV</SelectItem>
                                <SelectItem value="0.5">+0.5 EV</SelectItem>
                                <SelectItem value="1">+1.0 EV</SelectItem>
                                <SelectItem value="1.5">+1.5 EV</SelectItem>
                                <SelectItem value="2">+2.0 EV</SelectItem>
                              </SelectContent>
                            </Select>
                          </div>
                          <div className="flex justify-between gap-4 items-center">
                            <Label>{i18n._('sys.hardware_management.isp_anti_flicker')}</Label>
                            <Select
                              disabled={!ispExposureModeAuto}
                              value={String(ispAntiFlicker)}
                              onValueChange={(value) => {
                                const v = Number(value || 0) as 0 | 50 | 60;
                                setIspAntiFlicker(v);
                              }}
                            >
                              <SelectTrigger className="border-0 shadow-none focus-visible:ring-0 focus-visible:border-transparent w-28">
                                <SelectValue />
                              </SelectTrigger>
                              <SelectContent>
                                <SelectItem value="0">{i18n._('sys.hardware_management.isp_anti_flicker_off')}</SelectItem>
                                <SelectItem value="50">{i18n._('sys.hardware_management.isp_anti_flicker_50')}</SelectItem>
                                <SelectItem value="60">{i18n._('sys.hardware_management.isp_anti_flicker_60')}</SelectItem>
                              </SelectContent>
                            </Select>
                          </div>
                          <div className="flex justify-between gap-4 items-center">
                            <Label>{i18n._('sys.hardware_management.isp_sensor_delay')}</Label>
                            <Input
                              className="w-24 text-right"
                              type="number"
                              min={0}
                              value={ispSensorDelay}
                              onChange={(e) => {
                                const v = Number((e.target as HTMLInputElement).value || 0);
                                const clamped = v < 0 ? 0 : v;
                                setIspSensorDelay(clamped);
                              }}
                            />
                          </div>
                          <div className="flex justify-between gap-4 items-center">
                            <Label>{i18n._('sys.hardware_management.isp_estimated_lux')}</Label>
                            <div className="flex items-center gap-3">
                              <span className="text-sm text-text-primary min-w-[3rem] text-right">
                                {ispEstimatedLux != null ? ispEstimatedLux : '--'}
                              </span>
                              <Button
                                type="button"
                                variant="outline"
                                size="sm"
                                className="inline-flex items-center justify-center gap-2 shrink-0 px-3"
                                onClick={() => setLuxRefDialogOpen(true)}
                              >
                                <span>{i18n._('sys.hardware_management.isp_lux_setting')}</span>
                              </Button>
                            </div>
                          </div>
                          <Separator />
                          <div className="flex flex-col gap-2">
                            <div className="flex justify-between gap-4 items-center">
                              <Label>{i18n._('sys.hardware_management.isp_sensor_gain')} (dB)</Label>
                              <Input
                                className="w-24 text-right"
                                type="number"
                                min={0}
                                disabled={ispExposureModeAuto}
                                value={ispManualSensorGain}
                                onChange={(e) => {
                                  const v = Number((e.target as HTMLInputElement).value || 0);
                                  const clamped = v < 0 ? 0 : v;
                                  setIspManualSensorGain(clamped);
                                }}
                              />
                            </div>
                            <Slider
                              className="mt-2 mb-4"
                              min={0}
                              max={100}
                              step={1}
                              disabled={ispExposureModeAuto}
                              value={ispManualSensorGain}
                              onChange={(v) => {
                                const clamped = v < 0 ? 0 : v;
                                setIspManualSensorGain(clamped);
                              }}
                            />
                          </div>
                          <div className="flex flex-col gap-2">
                            <div className="flex justify-between gap-4 items-center">
                              <Label>{i18n._('sys.hardware_management.isp_exposure_time')} (µs)</Label>
                              <Input
                                className="w-24 text-right"
                                type="number"
                                min={0}
                                disabled={ispExposureModeAuto}
                                value={ispManualExposureTime}
                                onChange={(e) => {
                                  const v = Number((e.target as HTMLInputElement).value || 0);
                                  const clamped = v < 0 ? 0 : v;
                                  setIspManualExposureTime(clamped);
                                }}
                              />
                            </div>
                            <Slider
                              className="mt-2 mb-4"
                              min={0}
                              max={100000}
                              step={1000}
                              disabled={ispExposureModeAuto}
                              value={ispManualExposureTime}
                              onChange={(v) => {
                                const clamped = v < 0 ? 0 : v;
                                setIspManualExposureTime(clamped);
                              }}
                            />
                          </div>
                        </div>
                        <Separator />
                        <div className="flex justify-between gap-4 items-center">
                          <Label>{i18n._('sys.hardware_management.isp_awb')}</Label>
                          <span className="text-sm text-gray-500">
                            /api/v1/isp/awb
                          </span>
                        </div>
                        <Separator />
                        <div className="flex justify-between gap-4 items-center">
                          <Label>{i18n._('sys.hardware_management.isp_demosaicing')}</Label>
                          <span className="text-sm text-gray-500">
                            /api/v1/isp/demosaicing
                          </span>
                        </div>
                        <Separator />
                        <div className="flex justify-between gap-4 items-center">
                          <Label>{i18n._('sys.hardware_management.isp_black_level')}</Label>
                          <span className="text-sm text-gray-500">
                            /api/v1/isp/black_level
                          </span>
                        </div>
                        <Separator />
                        <div className="flex justify-between gap-4 items-center">
                          <Label>{i18n._('sys.hardware_management.isp_bad_pixel')}</Label>
                          <span className="text-sm text-gray-500">
                            /api/v1/isp/bad_pixel
                          </span>
                        </div>
                        <Separator />
                        <div className="flex justify-between gap-4 items-center">
                          <Label>{i18n._('sys.hardware_management.isp_gain')}</Label>
                          <span className="text-sm text-gray-500">
                            /api/v1/isp/gain
                          </span>
                        </div>
                        <Separator />
                        <div className="flex justify-between gap-4 items-center">
                          <Label>{i18n._('sys.hardware_management.isp_color_conv')}</Label>
                          <span className="text-sm text-gray-500">
                            /api/v1/isp/color_conv
                          </span>
                        </div>
                        <Separator />
                        <div className="flex justify-between gap-4 items-center">
                          <Label>{i18n._('sys.hardware_management.isp_contrast_gamma')}</Label>
                          <span className="text-sm text-gray-500">
                            /api/v1/isp/contrast, /api/v1/isp/gamma
                          </span>
                        </div>
                        <Separator />
                        <div className="flex justify-between gap-4 items-center">
                          <Label>{i18n._('sys.hardware_management.isp_stat')}</Label>
                          <span className="text-sm text-gray-500">
                            /api/v1/isp/stat_area, /api/v1/isp/stat_removal
                          </span>
                        </div>
                        <Separator />
                        <div className="flex justify-between gap-4 items-center">
                          <Label>{i18n._('sys.hardware_management.isp_lux_sensor')}</Label>
                          <span className="text-sm text-gray-500">
                            /api/v1/isp/lux_ref, /api/v1/isp/sensor_delay
                          </span>
                        </div>
                      </div>
                    )}
                  </div>
                  {/* ISP configuration (layout-only for now) */}
                </>
              )}
            </>
          )}
        </div>
      )}
      {/* LuxRef configuration dialog */}
      <Dialog open={luxRefDialogOpen} onOpenChange={setLuxRefDialogOpen}>
        <DialogContent className="max-w-3xl">
          <DialogHeader>
            <DialogTitle>
              {i18n._('sys.hardware_management.lux_ref_title')
                ?? 'Lux Calibration Setup'}
            </DialogTitle>
          </DialogHeader>
          <div className="mt-8 space-y-6">
            {/* Sensor calibration factor */}
            <div className="space-y-2">
              <Label>
                {i18n._('sys.hardware_management.lux_ref_calib_factor')
                  ?? 'Sensor calibration factor'}
              </Label>
              <Input
                className="max-w-xs"
                type="number"
                step={0.001}
                value={luxRefForm.calibFactor}
                onChange={e => setLuxRefForm(prev => ({
                    ...prev,
                    calibFactor: Number(
                      (e.target as HTMLInputElement).value || 0
                    ),
                  }))}
              />
            </div>
            {/* High lux conditions */}
            <div className="space-y-3">
              <Label className="font-semibold">
                {i18n._('sys.hardware_management.lux_ref_high_title')
                  ?? 'High lux conditions'}
              </Label>
              <div className="grid gap-4 md:grid-cols-3">
                <div className="space-y-1">
                  <Label>
                    {i18n._('sys.hardware_management.lux_ref_hl_ref')
                      ?? 'High lux reference (lx)'}
                  </Label>
                  <Input
                    type="number"
                    value={luxRefForm.hlRef}
                    onChange={e => setLuxRefForm(prev => ({
                        ...prev,
                        hlRef: Number(
                          (e.target as HTMLInputElement).value || 0
                        ),
                      }))}
                  />
                </div>
                <div className="space-y-1">
                  <Label>
                    {i18n._('sys.hardware_management.lux_ref_hl_expo1')
                      ?? '1st ref exposure time (µs)'}
                  </Label>
                  <Input
                    type="number"
                    value={luxRefForm.hlExpo1}
                    onChange={e => setLuxRefForm(prev => ({
                        ...prev,
                        hlExpo1: Number(
                          (e.target as HTMLInputElement).value || 0
                        ),
                      }))}
                  />
                </div>
                <div className="space-y-1">
                  <Label>
                    {i18n._('sys.hardware_management.lux_ref_hl_lum1')
                      ?? '1st ref luminance'}
                  </Label>
                  <Input
                    type="number"
                    value={luxRefForm.hlLum1}
                    onChange={e => setLuxRefForm(prev => ({
                        ...prev,
                        hlLum1: Number(
                          (e.target as HTMLInputElement).value || 0
                        ),
                      }))}
                  />
                </div>
                <div className="space-y-1">
                  <Label>
                    {i18n._('sys.hardware_management.lux_ref_hl_expo2')
                      ?? '2nd ref exposure time (µs)'}
                  </Label>
                  <Input
                    type="number"
                    value={luxRefForm.hlExpo2}
                    onChange={e => setLuxRefForm(prev => ({
                        ...prev,
                        hlExpo2: Number(
                          (e.target as HTMLInputElement).value || 0
                        ),
                      }))}
                  />
                </div>
                <div className="space-y-1">
                  <Label>
                    {i18n._('sys.hardware_management.lux_ref_hl_lum2')
                      ?? '2nd ref luminance'}
                  </Label>
                  <Input
                    type="number"
                    value={luxRefForm.hlLum2}
                    onChange={e => setLuxRefForm(prev => ({
                        ...prev,
                        hlLum2: Number(
                          (e.target as HTMLInputElement).value || 0
                        ),
                      }))}
                  />
                </div>
              </div>
            </div>
            {/* Low lux conditions */}
            <div className="space-y-3">
              <Label className="font-semibold">
                {i18n._('sys.hardware_management.lux_ref_low_title')
                  ?? 'Low lux conditions'}
              </Label>
              <div className="grid gap-4 md:grid-cols-3">
                <div className="space-y-1">
                  <Label>
                    {i18n._('sys.hardware_management.lux_ref_ll_ref')
                      ?? 'Low lux reference (lx)'}
                  </Label>
                  <Input
                    type="number"
                    value={luxRefForm.llRef}
                    onChange={e => setLuxRefForm(prev => ({
                        ...prev,
                        llRef: Number(
                          (e.target as HTMLInputElement).value || 0
                        ),
                      }))}
                  />
                </div>
                <div className="space-y-1">
                  <Label>
                    {i18n._('sys.hardware_management.lux_ref_ll_expo1')
                      ?? '1st ref exposure time (µs)'}
                  </Label>
                  <Input
                    type="number"
                    value={luxRefForm.llExpo1}
                    onChange={e => setLuxRefForm(prev => ({
                        ...prev,
                        llExpo1: Number(
                          (e.target as HTMLInputElement).value || 0
                        ),
                      }))}
                  />
                </div>
                <div className="space-y-1">
                  <Label>
                    {i18n._('sys.hardware_management.lux_ref_ll_lum1')
                      ?? '1st ref luminance'}
                  </Label>
                  <Input
                    type="number"
                    value={luxRefForm.llLum1}
                    onChange={e => setLuxRefForm(prev => ({
                        ...prev,
                        llLum1: Number(
                          (e.target as HTMLInputElement).value || 0
                        ),
                      }))}
                  />
                </div>
                <div className="space-y-1">
                  <Label>
                    {i18n._('sys.hardware_management.lux_ref_ll_expo2')
                      ?? '2nd ref exposure time (µs)'}
                  </Label>
                  <Input
                    type="number"
                    value={luxRefForm.llExpo2}
                    onChange={e => setLuxRefForm(prev => ({
                        ...prev,
                        llExpo2: Number(
                          (e.target as HTMLInputElement).value || 0
                        ),
                      }))}
                  />
                </div>
                <div className="space-y-1">
                  <Label>
                    {i18n._('sys.hardware_management.lux_ref_ll_lum2')
                      ?? '2nd ref luminance'}
                  </Label>
                  <Input
                    type="number"
                    value={luxRefForm.llLum2}
                    onChange={e => setLuxRefForm(prev => ({
                        ...prev,
                        llLum2: Number(
                          (e.target as HTMLInputElement).value || 0
                        ),
                      }))}
                  />
                </div>
              </div>
            </div>
          </div>
          <DialogFooter className="mt-6">
            <Button
              variant="outline"
              onClick={() => setLuxRefDialogOpen(false)}
            >
              {i18n._('common.cancel') ?? 'Cancel'}
            </Button>
            <Button
              variant="primary"
              onClick={() => {
                // TODO: wire to /api/v1/isp/lux_ref save
                setLuxRefDialogOpen(false);
              }}
            >
              {i18n._('common.save') ?? 'Save'}
            </Button>
          </DialogFooter>
        </DialogContent>
      </Dialog>
    </div>
  );
}
