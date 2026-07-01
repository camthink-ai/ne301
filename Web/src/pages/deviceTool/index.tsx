import { useState, useRef, useEffect } from 'preact/hooks';
import { useLingui } from '@lingui/react';
import { Separator } from '@/components/ui/separator';
import { Card, CardContent } from '@/components/ui/card';
import { Tooltip, TooltipContent, TooltipTrigger } from '@/components/tooltip';
import { Button } from '@/components/ui/button';
import Upload from '@/components/upload';
import {
  Select,
  SelectContent,
  SelectItem,
  SelectTrigger,
  SelectValue,
} from '@/components/ui/select';

import SvgIcon from '@/components/svg-icon';
import { Input } from '@/components/ui/input';
import Player from './player';
import ToolGuide from './tool-guide';
import TriggerConfig from './trigger-config';
import { getItem } from '@/utils/storage';

import deviceTool, { type AiParams, type VideoStreamPushReq } from '@/services/api/deviceTool';
import hardwareManagement from '@/services/api/hardware-management';
import systemApis from '@/services/api/system';

import DeviceToolSkeleton from './skeleton';
import { useAiStatusStore } from '@/store/aiStatus';
import Slider from '@/components/slider';
import { Label } from '@/components/ui/label';
import { getWebSocketUrl, sliceFile } from '@/utils';
import WifiReloadMask from '@/components/wifi-reload-mask';
import H264Player from '@/lib/MSE/h264Player';
import RtmpConfig from './rtmp-config';
import { toast } from 'sonner';

export interface ToolGuideContent {
  title: string;
  description: string;
  footer: string;
}
export interface ToolGuideProps {
  refList: React.RefObject<HTMLDivElement>[];
  scollRef: React.RefObject<HTMLDivElement>;
  isLoading: boolean;
  onClose?: () => void;
}

export default function DeviceTool() {
  const { i18n } = useLingui();

  // eslint-disable-next-line @typescript-eslint/no-unused-vars
  // configImageTrigger
  const {
    getAiStatusReq,
    getWorkModeStatusReq,
    getPowerModeReq,
    switchWorkModeReq,
    switchPowerModeReq,
    configVideoStreamPushReq,
    getAiParamsReq,
    setAiParamsReq,
    stopVideoStreamReq,
    toggleAiReq,
  } = deviceTool;
  const { getSysClkConfigReq, setSysClkConfigReq } = hardwareManagement;
  const videoRendererInstance = useRef<H264Player | null>(null);
  const { uploadOTAFileReq, reloadModelReq, preCheckReq } = systemApis;
  const { setIsAiInference, setAiStatus, aiStatus } = useAiStatusStore();
  const [curPowerModel, setCurPowerModel] = useState<string>('full_speed');
  const handlePowerModelChange = async (value: string) => {
    try {
      await switchPowerModeReq({ mode: value as 'full_speed' | 'low_power' });
      setCurPowerModel(value);
    } catch (error) {
      console.error('handlePowerModelChange', error);
      throw error;
    }
  };
  // const [uploadLoading, setUploadLoading] = useState(false);
  const [modelUploadLoading, setModelUploadLoading] = useState(false);
  const [playerEpoch, setPlayerEpoch] = useState(0);
  // video URL
  const [videoParameters, setVideoParameters] = useState<VideoStreamPushReq>({
    enabled: false,
    server_url: '',
  });
  // const [videoUrl, setVideoUrl] = useState<string>('')
  const [curWorkModel, setCurWorkModel] = useState<string>('image');
  const [sysClkProfile, setSysClkProfile] = useState<1 | 2 | 3 | 4>(4);
  const [sysClkFlashValid, setSysClkFlashValid] = useState(false);
  const handleWorkModelChange = async (value: string) => {
    try {
      const newVideoParameters = {
        ...videoParameters,
        enabled: value === 'video_stream',
      };
      await switchWorkModeReq({ mode: value as 'image' | 'video_stream' });
      await configVideoStreamPushReq(newVideoParameters);
      setCurWorkModel(value);
      setVideoParameters(newVideoParameters);
    } catch (error) {
      console.error('handleWorkModelChange', error);
      throw error;
    }
  };
  const handleVideoParametersChange = (key: string) => (e: Event) => {
    const target = e.target as HTMLInputElement;
    setVideoParameters(p => ({
      ...p,
      [key]: target.value,
    }));
  };

  const [aiModelName, setAiModelName] = useState('');
  const [aiParams, setAiParams] = useState({
    nms_threshold: 0,
    confidence_threshold: 0,
  });
  // Trigger method

  const acceptFileType = {
    // Only accept .bin firmware files
    'application/octet-stream': ['.bin'],
  };
  const [isLoading, setIsLoading] = useState(true);
  const getAiStatus = async () => {
    try {
      const res = await getAiStatusReq();
      setIsAiInference(res.data.ai_enabled);
      setAiModelName(res.data.model.name);
      setAiStatus(res.data.model.status);
    } catch (error) {
      console.error('getAiStatus', error);
      throw error;
    }
  };
  const getAiParams = async () => {
    try {
      const res = await getAiParamsReq();
      setAiParams(res.data);
    } catch (error) {
      console.error('getAiParams', error);
      throw error;
    }
  };
  // const initTriggerConfig = async () => {
  //   try {
  //     const res = await getTriggerConfigReq();
  //     setTriggerConfig(res.data);
  //   } catch (error) {
  //     console.error('initTriggerConfig', error);
  //     throw error;
  //   }
  // };
  const initPowerMode = async () => {
    try {
      const res = await getPowerModeReq();
      setCurPowerModel(res.data.current_mode);
    } catch (error) {
      console.error('initPowerMode', error);
      throw error;
    }
  };
  const getWorkModeStatus = async () => {
    try {
      const res = await getWorkModeStatusReq();
      setCurWorkModel(res.data.current_mode);
    } catch (error) {
      console.error('getWorkModeStatus', error);
      throw error;
    }
  };
  const initSysClk = async () => {
    try {
      const clkRes = await getSysClkConfigReq();
      const d = clkRes.data as { valid?: boolean; sys_clk_profile?: number };
      const valid = Boolean(d?.valid);
      setSysClkFlashValid(valid);
      if (valid && typeof d.sys_clk_profile === 'number') {
        const p = d.sys_clk_profile;
        if (p === 1 || p === 2 || p === 3 || p === 4) {
          setSysClkProfile(p);
        } else {
          setSysClkProfile(4);
        }
      } else {
        setSysClkProfile(4);
      }
    } catch (error) {
      console.error('initSysClk', error);
      setSysClkFlashValid(false);
      setSysClkProfile(4);
    }
  };
  const initQueue = async () => {
    try {
      setIsLoading(true);
      // Initialize AI state to true
      await toggleAiReq({ ai_enabled: true });

      await Promise.allSettled([
        getAiStatus(),
        getWorkModeStatus(),
        // initTriggerConfig(),
        initPowerMode(),
        getAiParams(),
        initSysClk(),
      ])
    } catch (error) {
      console.error('initQueue', error);
      throw error;
    } finally {
      setIsLoading(false);
    }
  }
  useEffect(() => {
    initQueue();
  }, []);

  const handleAiParamsChange = async (key: string, value: number) => {
    if (value < 1) {
      value = 1;
    } else if (value > 100) {
      value = 100;
    }
    if (Number.isNaN(value)) {
      value = 50;
    }
    setAiParams(p => ({ ...p, [key]: value }));
  };
  const postAiParams = async (key: keyof AiParams, value: number) => {
    const params = { ...aiParams, [key]: value };
    try {
      await setAiParamsReq(params);
    } catch (error) {
      console.error('postAiParams', error);
      throw error;
    }
  };
  const handleUploadChange = async (file: File) => {
    try {
      setModelUploadLoading(true);
      await stopVideoStreamReq();
      const contentPreview = await sliceFile(file, 2048);
      if (!contentPreview.size) {
        throw new Error(i18n._('sys.system_management.invalid_firmware_file') || 'Invalid firmware file');
      }
      await preCheckReq(contentPreview, 'ai');
      await uploadOTAFileReq(file, 'ai');
      await reloadModelReq();
    } catch (error) {
      console.error('handleUploadChange', error);
      throw error;
    } finally {
      try {
        await stopVideoStreamReq();
        await new Promise((resolve) => { setTimeout(resolve, 1000); });
        // Remount player for a clean MSE / Worker / WebSocket lifecycle
        setPlayerEpoch((epoch) => epoch + 1);
        await getAiStatus();
      } catch (restartError) {
        console.error('preview restart after model upload', restartError);
      }
      setModelUploadLoading(false);
    }
  };

  // const handleUploadChange = async (file: File) => {
  const uploadSlot = (
    <>
      <SvgIcon icon="upload" />
      {i18n._('common.upload')}
    </>
  );

  // Feature guide
  const [showToolGuide, setShowToolGuide] = useState(
    getItem('guideFlag') !== 'false'
  );

  const handleToolGuideClose = () => {
    setShowToolGuide(false);
  };

  // Feature guide refs
  // const aiInferenceRef = useRef<HTMLDivElement>(null)
  const currentModelRef = useRef<HTMLDivElement>(null);
  const patternRef = useRef<HTMLDivElement>(null);
  const triggerRef = useRef<HTMLDivElement>(null);
  const scollRef = useRef<HTMLDivElement>(null);
  const refList = [currentModelRef, patternRef, triggerRef];
  return (
    <div ref={scollRef} className="h-full w-full overflow-y-auto">
      {modelUploadLoading && <WifiReloadMask isLoading={modelUploadLoading} loadingText={i18n._('sys.device_tool.upload_model_loading')} />}
      <div className="w-full py-4 px-4 md:px-0  flex justify-center">
        {showToolGuide && !isLoading && (
          <ToolGuide
            refList={refList}
            scollRef={scollRef}
            isLoading={isLoading}
            onClose={handleToolGuideClose}
          />
        )}
        <Card className="relative">
          <CardContent className="sm:w-xl flex flex-col">

            <div className=" bg-gray-100 w-full  aspect-video flex justify-center items-center">
              <Player
                key={playerEpoch}
                videoUrl={getWebSocketUrl()}
                videoRendererInstance={videoRendererInstance}
              />
            </div>
            <div
              className=" w-full  bg-white pt-4 px-4"
            >
              {isLoading ? (
                <DeviceToolSkeleton />
              )
                : (
                  <div>
                    {/* Camera settings */}
                    <p className="text-sm text-text-primary font-semibold mb-2 mt-4">
                      {i18n._('sys.device_tool.camera_title')}
                    </p>
                    <Card className="bg-gray-50 mb-4">
                      <CardContent className="">
                        <div className="w-full h-full ">
                          <div
                            ref={currentModelRef}
                            className="flex items-center justify-between flex-wrap gap-2"
                          >
                            <Label className="text-sm">
                              {i18n._('sys.device_tool.model')}
                            </Label>
                            <div className="flex items-center gap-2 flex-wrap">
                              <p className="text-sm text-text-secondary mr-1">
                                {aiStatus === 'loaded' ? aiModelName : i18n._('sys.model_verification.please_upload_model')}
                              </p>
                              <Upload
                                slot={uploadSlot}
                                className="flex flex-1 h-full justify-start"
                                type="button"
                                accept={acceptFileType}
                                maxFiles={1}
                                maxSize={1024 * 1024 * 10}
                                minSize={0}
                                multiple={false}
                                onFileChange={handleUploadChange}
                              />
                            </div>
                          </div>
                          {aiStatus === 'loaded' && (
                            <>
                              <Separator className="my-2" />
                              <div className="flex items-center justify-between">
                                <p className="text-sm font-semibold text-text-primary">
                                  {i18n._('sys.device_tool.params')}
                                </p>
                              </div>
                              <div className="flex flex-col items-center gap-4 p-2 border border-gray-200 border-solid rounded-md mt-2">
                                <div className="w-full flex items-center justify-between gap-4">
                                  <Label>{i18n._('sys.device_tool.nms_threshold')}</Label>
                                  <div className="flex items-center gap-2 flex-1">
                                    <Slider
                                      className="flex-1 min-w-16"
                                      value={aiParams.nms_threshold}
                                      max={100}
                                      step={1}
                                      min={1}
                                      onChange={value => setAiParams(p => ({ ...p, nms_threshold: value }))}
                                      onChangeEnd={(value) => postAiParams('nms_threshold', value)}
                                    />
                                    <Input
                                      className="w-16"
                                      type="number"
                                      min={1}
                                      max={100}
                                      step={1}
                                      value={aiParams.nms_threshold}
                                      onChange={(e) => handleAiParamsChange('nms_threshold', parseInt((e.target as HTMLInputElement).value || '0', 10))}
                                      onBlur={(e) => postAiParams('nms_threshold', parseInt((e.target as HTMLInputElement).value || '0', 10))}
                                    />
                                  </div>
                                </div>
                                <div className="w-full flex items-center justify-between gap-4">
                                  <Label>{i18n._('sys.device_tool.confidence_threshold')}</Label>
                                  <div className="flex items-center gap-2 flex-1">
                                    <Slider
                                      className="flex-1 min-w-16"
                                      value={aiParams.confidence_threshold}
                                      max={100}
                                      step={1}
                                      min={1}
                                      onChange={value => setAiParams(p => ({ ...p, confidence_threshold: value }))}
                                      onChangeEnd={value => postAiParams('confidence_threshold', value)}
                                    />
                                    <Input
                                      className="w-16"
                                      type="number"
                                      min={1}
                                      max={100}
                                      step={1}
                                      value={aiParams.confidence_threshold}
                                      onChange={(e) => handleAiParamsChange('confidence_threshold', parseInt((e.target as HTMLInputElement).value || '1', 10))}
                                      onBlur={(e) => postAiParams('confidence_threshold', parseInt((e.target as HTMLInputElement).value || '1', 10))}
                                    />
                                  </div>
                                </div>
                              </div>
                            </>
                          )}
                          <Separator className="my-2" />
                          <div ref={patternRef}>
                            <div className="flex items-center  justify-between">
                              <Label className="text-sm text-text-primary">
                                {i18n._('sys.device_tool.power')}
                              </Label>
                              <Select
                                value={curPowerModel}
                                onValueChange={handlePowerModelChange}
                              >
                                <SelectTrigger className="border-0 shadow-none focus-visible:ring-0 focus-visible:border-transparent">
                                  <SelectValue
                                    placeholder={i18n._('sys.device_tool.power')}
                                  />
                                </SelectTrigger>
                                <SelectContent>
                                  <SelectItem value="full_speed">
                                    {i18n._('sys.device_tool.full_speed')}
                                  </SelectItem>
                                  <SelectItem value="low_power">
                                    {i18n._('sys.device_tool.low_power')}
                                  </SelectItem>
                                </SelectContent>
                              </Select>
                            </div>
                            <Separator className="my-2" />
                            <div>
                              <div className="flex items-center  justify-between">
                                <Label className="text-sm text-text-primary">
                                  {i18n._('sys.device_tool.work')}
                                </Label>
                                <Select
                                  value={curWorkModel}
                                  onValueChange={handleWorkModelChange}
                                >
                                  <SelectTrigger className="border-0 shadow-none focus-visible:ring-0 focus-visible:border-transparent">
                                    <SelectValue
                                      placeholder={i18n._('sys.device_tool.work')}
                                    />
                                  </SelectTrigger>
                                  <SelectContent>
                                    <SelectItem value="image">
                                      {i18n._('sys.device_tool.image')}
                                    </SelectItem>
                                    {/* <SelectItem value="video_stream">
                                    {i18n._('sys.device_tool.video')}
                                  </SelectItem> */}
                                  </SelectContent>
                                </Select>
                              </div>
                              <Separator className="my-2" />
                              <div className="flex items-center justify-between">
                                <div className="flex min-w-0 items-center gap-1">
                                  <Label className="text-sm text-text-primary shrink-0">
                                    {i18n._('sys.device_tool.sys_clk_title')}
                                  </Label>
                                  <Tooltip mbEnhance>
                                    <TooltipTrigger>
                                      <div className="inline-flex h-4 w-4 shrink-0 items-center justify-center text-gray-500">
                                        <SvgIcon className="h-4 w-4 text-gray-500" icon="info" />
                                      </div>
                                    </TooltipTrigger>
                                    <TooltipContent className="max-w-80 text-pretty">
                                      <div>
                                        <p>{i18n._('sys.device_tool.sys_clk_reboot_hint')}</p>
                                        {!sysClkFlashValid && (
                                          <p className="mt-2">{i18n._('sys.device_tool.sys_clk_no_valid_profile')}</p>
                                        )}
                                      </div>
                                    </TooltipContent>
                                  </Tooltip>
                                </div>
                                <Select
                                  value={String(sysClkProfile)}
                                  onValueChange={async (v) => {
                                    const next = Number(v) as 1 | 2 | 3 | 4;
                                    setSysClkProfile(next);
                                    try {
                                      await setSysClkConfigReq({ sys_clk_profile: next });
                                      setSysClkFlashValid(true);
                                      toast.success(i18n._('sys.device_tool.sys_clk_saved_toast'));
                                    } catch (e) {
                                      console.error(e);
                                      toast.error('Save failed');
                                    }
                                  }}
                                >
                                  <SelectTrigger className="border-0 shadow-none focus-visible:ring-0 focus-visible:border-transparent">
                                    <SelectValue placeholder={i18n._('sys.device_tool.sys_clk_title')} />
                                  </SelectTrigger>
                                  <SelectContent>
                                    <SelectItem value="1">{i18n._('sys.device_tool.sys_clk_hse_200')}</SelectItem>
                                    <SelectItem value="2">{i18n._('sys.device_tool.sys_clk_hse_400')}</SelectItem>
                                    <SelectItem value="3">{i18n._('sys.device_tool.sys_clk_hsi_800')}</SelectItem>
                                    <SelectItem value="4">{i18n._('sys.device_tool.sys_clk_hse_800')}</SelectItem>
                                  </SelectContent>
                                </Select>
                              </div>
                              {curWorkModel === 'video_stream' && (
                                <div className="border border-gray-200 border-solid p-2 rounded-md mt-2">
                                  <div className="flex items-center  justify-between">
                                    <div className="flex items-center">
                                      <p className="text-sm text-text-primary">
                                        {i18n._('sys.device_tool.video_stream_url')}
                                      </p>
                                      <Tooltip mbEnhance>
                                        <TooltipTrigger>
                                          <div className="w-4 mr-2 ml-1 flex justify-center items-center">
                                            <SvgIcon
                                              className="w-4 h-4"
                                              icon="info"
                                            />
                                          </div>
                                        </TooltipTrigger>
                                        <TooltipContent className="max-w-80 text-pretty">
                                          <div>
                                            <p>
                                              {i18n._(
                                                'sys.device_tool.dynamic_adjust_note'
                                              )}
                                            </p>
                                          </div>
                                        </TooltipContent>
                                      </Tooltip>
                                    </div>
                                    <Input
                                      className="flex-1"
                                      variant="ghost"
                                      placeholder={i18n._('common.please_enter')}
                                      type="text"
                                      value={videoParameters.server_url}
                                      onChange={handleVideoParametersChange(
                                        'server_url'
                                      )}
                                    />
                                  </div>
                                  <Separator className="my-2" />
                                  <div className="flex justify-end">
                                    <Button
                                      variant="primary"
                                      onClick={() => handleWorkModelChange('video_stream')}
                                    >
                                      {i18n._('common.confirm')}
                                    </Button>
                                  </div>
                                </div>
                              )}
                            </div>
                            <Separator className="my-2" />
                            <RtmpConfig />
                          </div>
                        </div>
                      </CardContent>
                    </Card>
                    <TriggerConfig
                      childeRef={triggerRef}
                    />
                  </div>
                )}
            </div>
          </CardContent>
        </Card>
      </div>
    </div>
  );
}
