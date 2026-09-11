import { useState, useEffect, useRef } from 'preact/hooks';
import { useLingui } from '@lingui/react';
import { Label } from '@/components/ui/label';
import {
  Select,
  SelectContent,
  SelectItem,
  SelectTrigger,
  SelectValue,
} from '@/components/ui/select';
import { Input } from '@/components/ui/input';
import { Switch } from '@/components/ui/switch';
import deviceTool from '@/services/api/deviceTool';
import { Button } from '@/components/ui/button';
import { Skeleton } from '@/components/ui/skeleton';
import SvgIcon from '@/components/svg-icon';
import { Separator } from '@/components/ui/separator';
import RtspConfig from './rtsp-config';

type Errors = {
  url: ErrorType;
  stream_key: ErrorType;
};
type ErrorType = {
  error: boolean;
  message: string;
};
type RtmpConfigDetail = {
  enable: boolean;
  url: string;
  stream_key: string;
};
type RtmpConfig = {
  status: RtmpStatusDetail;
  config: RtmpConfigDetail;
};
type RtmpStatisticsDetail = {
  bytes_sent: number;
  dropped_frames: number;
  frames_sent: number;
  keyframes_sent: number;
  reconnect_count: number;
  stream_duration_sec: number;
};
type RtmpStatusDetail = {
  initialized: boolean;
  streaming: boolean;
  state:
    | 'idle'
    | 'connecting'
    | 'streaming'
    | 'reconnecting'
    | 'stopping'
    | 'error';
};
type RtmpStatus = {
  status: RtmpStatusDetail;
  statistics: RtmpStatisticsDetail;
};
export default function RtmpConfig() {
  const { i18n } = useLingui();
  const {
    getRtmpConfigReq,
    startRtmpReq,
    stopRtmpReq,
    setRtmpConfigReq,
    getRtmpStatusReq,
    setRtspConfigReq,
  } = deviceTool;
  const [rtmpLoading, setRtmpLoading] = useState(false);
  const [configLoading, setConfigLoading] = useState(false);
  const [isPasswordVisible, setIsPasswordVisible] = useState(false);
  const [rtmpStatus, setRtmpStatus] = useState<RtmpStatus>({
    status: {
      initialized: true,
      streaming: false,
      state: 'idle',
    },
    statistics: {
      bytes_sent: 0,
      dropped_frames: 0,
      frames_sent: 0,
      keyframes_sent: 0,
      reconnect_count: 0,
      stream_duration_sec: 0,
    },
  });
  const handlePasswordVisible = () => {
    setIsPasswordVisible(!isPasswordVisible);
  };
  const [rtmpConfig, setRtmpConfig] = useState<RtmpConfig>({
    config: {
      enable: false,
      url: '',
      stream_key: '',
    },
    status: {
      initialized: true,
      streaming: false,
      state: 'idle',
    },
  });
  const [currentMode, setCurrentModeRaw] = useState<string>('rtmp');
  const setCurrentMode = (mode: string) => {
    setCurrentModeRaw(mode);
    deviceTool.setStreamTabPrefReq({ stream_tab: mode }).catch(() => {});
  };
  const autoCheck = useRef(false);
  const [errors, setErrors] = useState<Errors>({
    url: {
      error: false,
      message: '',
    },
    stream_key: {
      error: false,
      message: '',
    },
  });
  const getRtmpConfig = async () => {
    try {
      setRtmpLoading(true);
      const res = await getRtmpConfigReq();
      setRtmpConfig(res.data);
      return res.data;
    } catch (error) {
      console.error('initRtmpConfig', error);
      throw error;
    } finally {
      setRtmpLoading(false);
    }
  };

  const validateRtmpConfig = () => {
    let isValid = true;
    if (!rtmpConfig.config.url) {
      setErrors(prev => ({
        ...prev,
        url: { error: true, message: 'sys.device_tool.url_illegal' },
      }));
      isValid = false;
    } else if (rtmpConfig.config.url.length > 256) {
      setErrors(prev => ({
        ...prev,
        url: { error: true, message: 'sys.device_tool.url_length_error' },
      }));
      isValid = false;
    } else {
      setErrors(prev => ({ ...prev, url: { error: false, message: '' } }));
    }
    if (!rtmpConfig.config.stream_key) {
      setErrors(prev => ({
        ...prev,
        stream_key: {
          error: true,
          message: 'sys.device_tool.stream_key_illegal',
        },
      }));
      isValid = false;
    } else if (rtmpConfig.config.stream_key.length > 128) {
      setErrors(prev => ({
        ...prev,
        stream_key: {
          error: true,
          message: 'sys.device_tool.stream_key_length_error',
        },
      }));
      isValid = false;
    } else {
      setErrors(prev => ({
        ...prev,
        stream_key: { error: false, message: '' },
      }));
    }
    return isValid;
  };
  useEffect(() => {
    if (autoCheck.current) {
      validateRtmpConfig();
    }
  }, [rtmpConfig]);

  const initRtmpData = async () => {
    try {
      setRtmpLoading(true);
      await Promise.all([
        getRtmpConfig(),
        getRtmpStatus(),
        deviceTool
          .getStreamTabPrefReq()
          .then(res => {
            if (res?.data?.stream_tab) setCurrentModeRaw(res.data.stream_tab);
          })
          .catch(() => {}),
      ]);
    } catch (error) {
      console.error('initRtmpData', error);
      throw error;
    } finally {
      setRtmpLoading(false);
    }
  };
  const getRtmpStatus = async () => {
    try {
      const res = await getRtmpStatusReq();
      setRtmpStatus(res.data);
      return res.data;
    } catch (error) {
      console.error('getRtmpStatus', error);
      throw error;
    }
  };
  useEffect(() => {
    initRtmpData();
  }, []);
  const handleStopRtmp = async () => {
    try {
      await stopRtmpReq();
      getRtmpConfig();
      getRtmpStatus();
    } catch (error) {
      console.error('handleStopRtmp', error);
      throw error;
    }
  };

  const handleStartRtmp = async () => {
    try {
      setConfigLoading(true);
      if (!validateRtmpConfig()) return;
      /* Stop RTSP first (mutual exclusion) */
      await setRtspConfigReq({ enabled: false } as any);
      await setRtmpConfigReq({
        url: rtmpConfig.config.url,
        stream_key: rtmpConfig.config.stream_key,
      });
      await startRtmpReq({});
      getRtmpConfig();
      getRtmpStatus();
    } catch (error) {
      console.error('handleStartRtmp', error);
      throw error;
    } finally {
      setConfigLoading(false);
    }
  };

  const skeleton = () => (
    <div className="flex flex-col gap-2">
      <Skeleton className="h-10 w-full mb-2" />
      <Skeleton className="h-10 w-full mb-2" />
    </div>
  );
  return (
    <div>
      <div className="flex items-center  justify-between">
        <div className="flex items-center gap-2">
          <Label className="text-sm text-text-primary">
            {' '}
            {i18n._('sys.device_tool.media_stream_settings')}
          </Label>
        </div>
        <Select
          value={currentMode}
          onValueChange={async value => {
            if (value === currentMode) return;
            try {
              if (value === 'rtsp') {
                /* Switching to RTSP: stop RTMP if active */
                if (rtmpConfig.status.streaming) {
                  await handleStopRtmp();
                }
              } else if (value === 'rtmp') {
                /* Switching to RTMP: disable RTSP */
                await setRtspConfigReq({ enabled: false } as any);
                /* Refresh: RTSP side may have flipped the enable flags */
                getRtmpConfig();
                getRtmpStatus();
              }
            } catch (e) {
              console.error('mode switch error', e);
            }
            setCurrentMode(value);
          }}
        >
          <SelectTrigger>
            <SelectValue placeholder={i18n._('common.status')} />
          </SelectTrigger>
          <SelectContent>
            <SelectItem value="rtmp">RTMP</SelectItem>
            <SelectItem value="rtsp">RTSP</SelectItem>
          </SelectContent>
        </Select>
      </div>
      {/* {rtmpEnabled && ( */}
      <div className="border border-gray-200 border-solid p-4 rounded-md mt-2">
        {currentMode === 'rtmp' ? (
          <>
            {rtmpLoading ? (
              skeleton()
            ) : (
              <>
                <div className="flex justify-between gap-2 mb-2 flex-wrap">
                  <div className="flex items-center gap-2">
                    <Label className="text-sm text-text-primary">
                      {' '}
                      {i18n._('common.status')}:
                    </Label>
                    {rtmpStatus.status.state === 'reconnecting' && (
                      <div className="flex items-center gap-2">
                        <div className="w-2 h-2 rounded-full bg-orange-500"></div>
                        <p className="text-sm text-orange-500">
                          {i18n._('sys.device_tool.reconnecting')}
                        </p>
                      </div>
                    )}
                    {rtmpStatus.status.state === 'connecting' && (
                      <div className="flex items-center gap-2">
                        <div className="w-2 h-2 rounded-full bg-yellow-500"></div>
                        <p className="text-sm text-yellow-500">
                          {i18n._('sys.device_tool.connecting')}
                        </p>
                      </div>
                    )}
                    {rtmpStatus.status.state === 'streaming' && (
                      <div className="flex items-center gap-2">
                        <div className="w-2 h-2 rounded-full bg-green-500"></div>
                        <p className="text-sm text-green-500">
                          {i18n._('common.connected')}
                        </p>
                      </div>
                    )}
                    {(rtmpStatus.status.state === 'idle'
                      || rtmpStatus.status.state === 'error') && (
                      <div className="flex items-center gap-2">
                        <div className="w-2 h-2 rounded-full bg-gray-500"></div>
                        <p className="text-sm text-gray-500">
                          {i18n._('common.disconnected')}
                        </p>
                      </div>
                    )}
                  </div>
                  <div className="flex items-center gap-2">
                    <Label className="text-sm text-text-primary">
                      {' '}
                      {i18n._('sys.device_tool.stream_duration_sec')}:
                    </Label>
                    <p className="text-sm text-text-primary">
                      {rtmpStatus.statistics.stream_duration_sec}s
                    </p>
                  </div>
                </div>
                <Separator />
                {/* Enable switch: gates the fields and the connect button
                    below (same pattern as the RTSP page). Toggling off also
                    stops the live push; toggling on only unlocks editing. */}
                <div className="flex flex-col gap-1 py-2">
                  <div className="flex justify-between items-center">
                    <Label className="text-sm text-text-primary">
                      {i18n._('sys.device_tool.rtmp_enable')}
                    </Label>
                    <Switch
                      checked={rtmpConfig.config.enable}
                      onCheckedChange={async v => {
                        setRtmpConfig({
                          ...rtmpConfig,
                          config: { ...rtmpConfig.config, enable: v },
                        });
                        try {
                          if (!v && rtmpStatus.status.streaming) {
                            /* Disabling the service stops the live push too */
                            await stopRtmpReq();
                          }
                          await setRtmpConfigReq({ enable: v });
                          await Promise.all([getRtmpConfig(), getRtmpStatus()]);
                        } catch (error) {
                          setRtmpConfig({
                            ...rtmpConfig,
                            config: { ...rtmpConfig.config, enable: !v },
                          });
                          console.error('toggleRtmpEnable', error);
                        }
                      }}
                    />
                  </div>
                  <p className="text-xs text-text-secondary">
                    {i18n._('sys.device_tool.stream_autostart_note')}
                  </p>
                </div>
                <Separator />
                <div
                  className={
                    rtmpConfig.config.enable ? '' : 'opacity-50 pointer-events-none'
                  }
                >
                <div className="flex flex-col gap-2">
                  <div className="flex justify-between gap-2 flex-1 pr-0">
                    <Label className="text-sm text-text-primary shrink-0">
                      {' '}
                      {i18n._('sys.device_tool.url')}
                    </Label>
                    <Input
                      variant="ghost"
                      placeholder={i18n._('common.please_enter')}
                      type="text"
                      value={rtmpConfig.config.url}
                      onChange={e => setRtmpConfig({
                          ...rtmpConfig,
                          config: {
                            ...rtmpConfig.config,
                            url: (e.target as HTMLInputElement).value,
                          },
                        })}
                    />
                  </div>
                  {errors.url.error && (
                    <p className="text-sm text-red-500 self-end pr-2">
                      {i18n._(errors.url.message)}
                    </p>
                  )}
                </div>
                <div className="flex flex-col gap-2">
                  <div className="flex justify-between gap-2 flex-1 pr-0">
                    <Label className="text-sm text-text-primary shrink-0">
                      {' '}
                      {i18n._('common.secret_key')}
                    </Label>
                    <div className="flex items-center justify-center relative">
                      <Input
                        className="pr-11"
                        variant="ghost"
                        placeholder={i18n._('common.please_enter')}
                        type={isPasswordVisible ? 'text' : 'password'}
                        value={rtmpConfig.config.stream_key}
                        onChange={e => setRtmpConfig({
                            ...rtmpConfig,
                            config: {
                              ...rtmpConfig.config,
                              stream_key: (e.target as HTMLInputElement).value,
                            },
                          })}
                      />

                      <button
                        type="button"
                        onClick={handlePasswordVisible}
                        className="absolute top-1/2 -translate-y-1/2 right-0 flex items-center bg-transparent pr-4 border-none cursor-pointer disabled:opacity-50"
                        disabled={configLoading}
                      >
                        {isPasswordVisible ? (
                          <SvgIcon className="w-5 h-5" icon="visibility" />
                        ) : (
                          <SvgIcon className="w-5 h-5" icon="visibility_off" />
                        )}
                      </button>
                    </div>
                  </div>
                  {errors.stream_key.error && (
                    <p className="text-sm text-red-500 self-end pr-2">
                      {i18n._(errors.stream_key.message)}
                    </p>
                  )}
                </div>
                </div>
              </>
            )}

            <div className="flex justify-end gap-2 mt-2">
              <Button
                variant="primary"
                disabled={configLoading || !rtmpConfig.config.enable}
                onClick={() => (rtmpConfig.status.streaming
                    ? handleStopRtmp()
                    : handleStartRtmp())}
              >
                {configLoading ? (
                  <div className="w-full h-full flex items-center justify-center">
                    <div
                      className="w-4 h-4 rounded-full border-2 border-[#f24a00] border-t-transparent animate-spin"
                      aria-label="loading"
                    />
                  </div>
                ) : rtmpConfig.status.streaming ? (
                  i18n._('common.disconnect')
                ) : (
                  i18n._('common.connect')
                )}
              </Button>
            </div>
          </>
        ) : (
          <RtspConfig />
        )}
      </div>
      {/* )} */}
    </div>
  );
}
