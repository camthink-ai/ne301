import { useEffect, useState } from 'react';
import { useLingui } from '@lingui/react';
import { Label } from '@/components/ui/label';
import { Separator } from '@/components/ui/separator';
import { Switch } from '@/components/ui/switch';
import { Button } from '@/components/ui/button';
import { Input } from '@/components/ui/input';
import { Tooltip, TooltipContent, TooltipTrigger } from '@/components/tooltip';
import {
  Select, SelectContent, SelectItem, SelectTrigger, SelectValue,
} from '@/components/ui/select';
import SvgIcon from '@/components/svg-icon';
import { toast } from 'sonner';
import captureSettings, {
  type CaptureUploadConfig, type CaptureMode, type CaptureStorage,
  type StoragePolicy, type UploadProto,
} from '@/services/api/captureSettings';
import hardwareManagement, { type SetHardwareInfoReq } from '@/services/api/hardware-management';
import storageManagement from '@/services/api/storageManagement';
import systemSettings from '@/services/api/systemSettings';

const MINUTE_OPTIONS: { label: string; value: number }[] = [];
for (let h = 0; h < 24; h++) {
  for (let m = 0; m < 60; m += 30) {
    const v = h * 60 + m;
    const label = `${String(h).padStart(2, '0')}:${String(m).padStart(2, '0')}`;
    MINUTE_OPTIONS.push({ label, value: v });
  }
}

export default function CaptureConfig() {
  const { i18n } = useLingui();
  const [sdReady, setSdReady] = useState(false);

  const [cfg, setCfg] = useState<CaptureUploadConfig | null>(null);
  const [loading, setLoading] = useState(false);
  const [saving, setSaving] = useState(false);
  const [uploadSectionOpen, setUploadSectionOpen] = useState(true);
  const [cameraSectionOpen, setCameraSectionOpen] = useState(true);

  /* ---- hardware capture params (local state, committed on global save) ---- */
  const [fastSkipFrames, setFastSkipFrames] = useState(0);
  const [fastResolution, setFastResolution] = useState(0);
  const [fastJpegQuality, setFastJpegQuality] = useState(85);
  const [captureStorageAi, setCaptureStorageAi] = useState(false);
  /* Snapshot of the camera params as loaded from the device, so saveAll can
   * tell whether the wake-time params actually changed. Only those need the
   * "reboot/next wake" warning; the upload config is hot-reloaded live. */
  const [initCam, setInitCam] = useState({ fastSkipFrames: 0, fastResolution: 0, fastJpegQuality: 85, captureStorageAi: false });

  /* Available netifs (mirrors system settings "通讯方式"), fetched once for the
   * "上传网络" dropdown. Each item: { type: 'wifi'|'halow'|'cellular'|'poe', display_name } */
  const [commTypes, setCommTypes] = useState<{ type: string; display_name: string }[]>([]);

  const mode: CaptureMode = cfg?.mode ?? 'instant';
  const storage: CaptureStorage = cfg?.storage ?? 'auto';
  const policy: StoragePolicy = cfg?.policy ?? 'wrap';
  const proto: UploadProto = cfg?.upload_protocol ?? 'mqtt';
  const uploadNet: string = cfg?.upload_network ?? 'default';

  const load = async () => {
    setLoading(true);
    try {
      const [res, hwRes, stRes, netRes] = await Promise.all([
        captureSettings.getUploadConfig(),
        hardwareManagement.getHardwareInfoReq(),
        storageManagement.getStorage(),
        systemSettings.getNetworkStatusReq(),
      ]);
      setCfg(res.data);
      const arr = netRes?.data?.available_comm_types;
      setCommTypes(Array.isArray(arr) ? arr.map((t: any) => ({ type: t.type, display_name: t.display_name })) : []);
      const hw = hwRes.data;
      const camInit = {
        fastSkipFrames: typeof hw.fast_capture_skip_frames === 'number' ? hw.fast_capture_skip_frames : 0,
        fastResolution: typeof hw.fast_capture_resolution === 'number' ? hw.fast_capture_resolution : 0,
        fastJpegQuality: typeof hw.fast_capture_jpeg_quality === 'number' ? hw.fast_capture_jpeg_quality : 85,
        captureStorageAi: typeof hw.capture_storage_ai === 'boolean' ? hw.capture_storage_ai : false,
      };
      setFastSkipFrames(camInit.fastSkipFrames);
      setFastResolution(camInit.fastResolution);
      setFastJpegQuality(camInit.fastJpegQuality);
      setCaptureStorageAi(camInit.captureStorageAi);
      setInitCam(camInit);
      const st: any = stRes.data;
      setSdReady(st?.sd_card_connected === true);
    } catch (e) {
      console.error(e);
    } finally {
      setLoading(false);
    }
  };

  useEffect(() => { load(); }, []);

  const patch = (k: keyof CaptureUploadConfig, v: unknown) => {
    setCfg((prev) => (prev ? { ...prev, [k]: v } : prev));
  };

  /* Global save: commits BOTH upload config and camera params. The upload
   * config is hot-reloaded (immediate); the wake-time camera params
   * (fast_capture_* / capture_storage_ai) only take effect on next wake/reboot,
   * so we warn only when those actually changed. */
  const saveAll = async () => {
    if (!cfg) return;
    const camChanged = fastSkipFrames !== initCam.fastSkipFrames
      || fastResolution !== initCam.fastResolution
      || fastJpegQuality !== initCam.fastJpegQuality
      || captureStorageAi !== initCam.captureStorageAi;
    setSaving(true);
    try {
      await captureSettings.setUploadConfig(cfg);
      await hardwareManagement.setHardwareInfoReq({
        fast_capture_skip_frames: fastSkipFrames,
        fast_capture_resolution: fastResolution,
        fast_capture_jpeg_quality: fastJpegQuality,
        capture_storage_ai: captureStorageAi,
      } as SetHardwareInfoReq);
      if (camChanged) {
        toast.warning(i18n._('sys.capture_settings.saved_reboot_hint'));
      } else {
        toast.success(i18n._('sys.capture_settings.saved_immediate_hint'));
      }
      setInitCam({ fastSkipFrames, fastResolution, fastJpegQuality, captureStorageAi });
    } catch (e) {
      console.error(e);
      toast.error(i18n._('sys.capture_settings.save_failed') ?? 'Save failed');
    } finally {
      setSaving(false);
    }
  };

  if (loading || !cfg) {
    return (
<div className="flex flex-col gap-2 mt-4 bg-gray-100 p-4 rounded-lg">
      <div className="h-8 bg-gray-200 rounded animate-pulse" />
      <div className="h-8 bg-gray-200 rounded animate-pulse" />
</div>
);
  }

  const isLocalOnly = mode === 'local_only';
  const isScheduled = mode === 'scheduled';
  const isBatch = mode === 'batch';
  const isInstant = mode === 'instant';
  const showUploadSection = !isLocalOnly;
  const showRetry = showUploadSection && storage !== 'none';
  const showStorageSection = storage !== 'none';
  const showScheduleNodes = isScheduled;

  return (
    <div className="flex flex-col gap-2 mt-4 bg-gray-100 p-4 rounded-lg">
      {/* ============ Upload params (collapsible) ============ */}
      <div className="flex flex-col gap-2">
        <button
          type="button"
          className="flex w-full items-center justify-between text-left"
          onClick={() => setUploadSectionOpen(o => !o)}
        >
          <Label>{i18n._('sys.capture_settings.section_upload_mode')}</Label>
          <span className="inline-flex h-4 w-4 shrink-0 items-center justify-center text-gray-500">
            <SvgIcon
              icon="right"
              className={`h-4 w-4 transition-transform duration-200 ${
                uploadSectionOpen ? 'rotate-90' : 'rotate-0'
              }`}
            />
          </span>
        </button>
        {uploadSectionOpen && (
          <div className="border border-gray-200 border-solid p-4 rounded-md mt-2 flex flex-col gap-2">
            {/* mode */}
            <div className="flex justify-between gap-4 items-center">
              <Label>{i18n._('sys.capture_settings.mode')}</Label>
              <Select
                value={mode}
                onValueChange={(v) => {
                patch('mode', v as CaptureMode);
                if (v === 'local_only') { patch('retry_enable', false); }
              }}
              >
                <SelectTrigger className="border-0 shadow-none focus-visible:ring-0 w-fit">
                  <SelectValue />
                </SelectTrigger>
                <SelectContent>
                  <SelectItem value="instant">{i18n._('sys.capture_settings.mode_instant')}</SelectItem>
                  <SelectItem value="batch">{i18n._('sys.capture_settings.mode_batch')}</SelectItem>
                  <SelectItem value="scheduled">{i18n._('sys.capture_settings.mode_scheduled')}</SelectItem>
                  <SelectItem value="local_only">{i18n._('sys.capture_settings.mode_local_only')}</SelectItem>
                </SelectContent>
              </Select>
            </div>
            <Separator />

            {/* storage */}
            <div className="flex justify-between gap-4 items-center">
              <Label>{i18n._('sys.capture_settings.storage')}</Label>
              <Select
                value={storage}
                onValueChange={(v) => patch('storage', v as CaptureStorage)}
              >
                <SelectTrigger className="border-0 shadow-none focus-visible:ring-0 w-fit">
                  <SelectValue />
                </SelectTrigger>
                <SelectContent>
                  <SelectItem value="auto">{i18n._('sys.capture_settings.storage_auto')}</SelectItem>
                  <SelectItem value="flash">{i18n._('sys.capture_settings.storage_flash')}</SelectItem>
                  <SelectItem value="sd" disabled={!sdReady}>
                    {i18n._('sys.capture_settings.storage_sd')}
                  </SelectItem>
                  <SelectItem value="none" disabled={!isInstant}>
                    {i18n._('sys.capture_settings.storage_none')}
                  </SelectItem>
                </SelectContent>
              </Select>
            </div>

            {/* flash record-cap warning — amber alert box below storage selector */}
            {(storage === 'flash' || storage === 'auto') && cfg?.flash_max_records > 0 && (
              <div className="rounded bg-amber-50 dark:bg-amber-950 border border-amber-200 dark:border-amber-800 px-3 py-2 text-xs text-amber-800 dark:text-amber-200">
                {storage === 'auto'
                  ? (cfg.policy === 'wrap'
                    ? i18n._('sys.capture_settings.flash_limit_auto_wrap').replace('{max}', String(cfg.flash_max_records))
                    : i18n._('sys.capture_settings.flash_limit_auto_stop').replace('{max}', String(cfg.flash_max_records)))
                  : (cfg.policy === 'wrap'
                    ? i18n._('sys.capture_settings.flash_limit_flash_wrap').replace('{max}', String(cfg.flash_max_records))
                    : i18n._('sys.capture_settings.flash_limit_flash_stop').replace('{max}', String(cfg.flash_max_records)))}
              </div>
            )}
            <Separator />

            {/* policy + capture_storage_ai (only when storage != none) */}
            {showStorageSection && (
              <>
                <div className="flex justify-between gap-4 items-center">
                  <Label>{i18n._('sys.capture_settings.policy')}</Label>
                  <Select
                    value={policy}
                    onValueChange={(v) => patch('policy', v as StoragePolicy)}
                  >
                    <SelectTrigger className="border-0 shadow-none focus-visible:ring-0 w-fit">
                      <SelectValue />
                    </SelectTrigger>
                    <SelectContent>
                      <SelectItem value="wrap">{i18n._('sys.capture_settings.policy_wrap')}</SelectItem>
                      <SelectItem value="stop">{i18n._('sys.capture_settings.policy_stop')}</SelectItem>
                    </SelectContent>
                  </Select>
                </div>
                <Separator />

                {/* storage AI — moved here from camera params; hidden when storage=none */}
                <div className="flex justify-between gap-4 items-center">
                  <Label>{i18n._('sys.hardware_management.capture_storage_ai')}</Label>
                  <Switch
                    checked={captureStorageAi}
                    onCheckedChange={setCaptureStorageAi}
                  />
                </div>
                <Separator />
              </>
            )}

            {/* upload protocol + batch + schedule */}
            {showUploadSection && (
              <>
                <div className="flex justify-between gap-4 items-center">
                  <Label>{i18n._('sys.capture_settings.upload_protocol')}</Label>
                  <Select
                    value={proto}
                    onValueChange={(v) => patch('upload_protocol', v as UploadProto)}
                  >
                    <SelectTrigger className="border-0 shadow-none focus-visible:ring-0 w-fit">
                      <SelectValue />
                    </SelectTrigger>
                    <SelectContent>
                      <SelectItem value="mqtt">{i18n._('sys.capture_settings.proto_mqtt')}</SelectItem>
                      <SelectItem value="webhook">{i18n._('sys.capture_settings.proto_webhook')}</SelectItem>
                    </SelectContent>
                  </Select>
                </div>
                <Separator />

                <div className="flex justify-between gap-4 items-center">
                  <Label>{i18n._('sys.capture_settings.upload_network')}</Label>
                  <Select
                    value={uploadNet}
                    onValueChange={(v) => patch('upload_network', v)}
                  >
                    <SelectTrigger className="border-0 shadow-none focus-visible:ring-0 w-fit">
                      <SelectValue />
                    </SelectTrigger>
                    <SelectContent>
                      <SelectItem value="default">{i18n._('sys.capture_settings.upload_net_default')}</SelectItem>
                      {commTypes.map((t) => (
                        <SelectItem key={t.type} value={t.type}>{t.display_name}</SelectItem>
                      ))}
                    </SelectContent>
                  </Select>
                </div>
                <Separator />

                {isBatch && (
                  <>
                    <div className="flex justify-between gap-4 items-center">
                      <Label>{i18n._('sys.capture_settings.batch_count')}</Label>
                      <Input
                        className="w-24 text-right"
                        type="number"
                        min={2}
                        max={20}
                        value={cfg.batch_count}
                        onChange={(e) => {
                          const v = parseInt((e.target as HTMLInputElement).value || '2', 10);
                          patch('batch_count', Math.max(2, Math.min(20, v)));
                        }}
                      />
                    </div>
                    <Separator />
                  </>
                )}

                {showScheduleNodes && (
                  <>
                    <div className="flex flex-col gap-2">
                      <Label>{i18n._('sys.capture_settings.schedule_minutes')}</Label>
                      <div className="flex flex-wrap gap-2">
                        {(cfg.schedule_minutes ?? []).slice(0, 8).map((min, idx) => (
                          <div key={idx} className="flex items-center gap-1">
                            <Select
                              value={String(min)}
                              onValueChange={(v) => {
                              const arr = [...(cfg.schedule_minutes ?? [])];
                              arr[idx] = Number(v);
                              patch('schedule_minutes', arr);
                            }}
                            >
                              <SelectTrigger className="border px-2 py-1 text-sm w-24">
                                <SelectValue />
                              </SelectTrigger>
                              <SelectContent>
                                {MINUTE_OPTIONS.map((o) => (
                                  <SelectItem key={o.value} value={String(o.value)}>{o.label}</SelectItem>
                                ))}
                              </SelectContent>
                            </Select>
                            <button
                              type="button"
                              className="text-red-500 text-xs"
                              onClick={() => {
                                const arr = (cfg.schedule_minutes ?? []).filter((_, i) => i !== idx);
                                patch('schedule_minutes', arr);
                              }}
                            >✕
                            </button>
                          </div>
                        ))}
                      </div>
                      {(cfg.schedule_minutes?.length ?? 0) < 8 && (
                        <Button
                          variant="outline"
                          size="sm"
                          className="w-fit"
                          onClick={() => {
                            const arr = [...(cfg.schedule_minutes ?? []), 480];
                            patch('schedule_minutes', arr);
                          }}
                        >
                          + {i18n._('sys.capture_settings.add_node')}
                        </Button>
                      )}
                      <p className="text-xs text-gray-500 mt-1">
                        {i18n._('sys.capture_settings.schedule_hint')}
                      </p>
                    </div>
                    <Separator />
                  </>
                )}
              </>
            )}

            {/* retry */}
            {showRetry && (
              <>
                <div className="flex justify-between gap-4 items-center">
                  <div className="flex items-center gap-2">
                    <Label>{i18n._('sys.capture_settings.retry_enable')}</Label>
                  </div>
                  <Switch
                    checked={cfg.retry_enable}
                    onCheckedChange={(v) => patch('retry_enable', v)}
                  />
                </div>
                <Separator />

                {cfg.retry_enable && (
                  <>
                    <div className="flex justify-between gap-4 items-center">
                      <Label>{i18n._('sys.capture_settings.retry_max_attempts')}</Label>
                      <Input
                        className="w-24 text-right"
                        type="number"
                        min={0}
                        max={20}
                        value={cfg.retry_max_attempts}
                        onChange={(e) => {
                          const v = parseInt((e.target as HTMLInputElement).value || '0', 10);
                          patch('retry_max_attempts', Math.max(0, Math.min(20, v)));
                        }}
                      />
                    </div>
                    <Separator />
                  </>
                )}
              </>
            )}

            {/* keep_sent_hours */}
            {showStorageSection && (
              <div className="flex justify-between gap-4 items-center">
                  <Label>{i18n._('sys.capture_settings.keep_sent_hours')}</Label>
                  <Input
                    className="w-24 text-right"
                    type="number"
                    min={0}
                    max={720}
                    value={cfg.keep_sent_hours}
                    onChange={(e) => {
                      const v = parseInt((e.target as HTMLInputElement).value || '168', 10);
                      patch('keep_sent_hours', Math.max(0, Math.min(720, v)));
                    }}
                  />
              </div>
            )}
          </div>
        )}
      </div>

      {/* ============ Camera params (collapsible) ============ */}
      <Separator />
      <div className="flex flex-col gap-2">
        <button
          type="button"
          className="flex w-full items-center justify-between text-left"
          onClick={() => setCameraSectionOpen(o => !o)}
        >
          <div className="flex items-center gap-2">
            <Label>{i18n._('sys.capture_settings.section_capture_params')}</Label>
            <Tooltip mbEnhance>
              <TooltipTrigger>
                <div className="inline-flex h-4 w-4 shrink-0 items-center justify-center text-gray-500">
                  <SvgIcon className="h-4 w-4 text-gray-500" icon="info" />
                </div>
              </TooltipTrigger>
              <TooltipContent className="max-w-80 text-pretty">
                <p>{i18n._('sys.hardware_management.capture_config_tip')}</p>
              </TooltipContent>
            </Tooltip>
          </div>
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
            {/* skip frames — local state, committed on global save */}
            <div className="flex justify-between gap-4 items-center">
              <Label>{i18n._('sys.hardware_management.fast_capture_skip_frames')}</Label>
              <Input
                className="w-24 text-right"
                type="number"
                min={0}
                max={300}
                value={fastSkipFrames}
                onChange={(e) => {
                  const v = parseInt((e.target as HTMLInputElement).value || '0', 10);
                  setFastSkipFrames(Math.max(0, Math.min(300, v)));
                }}
              />
            </div>
            <Separator />

            {/* resolution */}
            <div className="flex justify-between gap-4 items-center">
              <Label>{i18n._('sys.hardware_management.fast_capture_resolution')}</Label>
              <Select
                value={String(fastResolution)}
                onValueChange={(v) => setFastResolution(Number(v ?? '0'))}
              >
                <SelectTrigger className="border-0 shadow-none focus-visible:ring-0 w-32">
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

            {/* jpeg quality (with tooltip) */}
            <div className="flex justify-between gap-4 items-center">
              <div className="flex min-w-0 flex-1 items-center gap-2">
                <Label className="shrink-0">
                  {i18n._('sys.hardware_management.fast_capture_jpeg_quality')}
                </Label>
                <Tooltip mbEnhance>
                  <TooltipTrigger>
                    <div className="inline-flex h-4 w-4 shrink-0 items-center justify-center text-gray-500">
                      <SvgIcon className="h-4 w-4 text-gray-500" icon="info" />
                    </div>
                  </TooltipTrigger>
                  <TooltipContent className="max-w-80 text-pretty">
                    <p>{i18n._('sys.hardware_management.capture_jpeg_quality_tip')}</p>
                  </TooltipContent>
                </Tooltip>
              </div>
              <Input
                className="w-24 text-right"
                type="number"
                min={1}
                max={100}
                value={fastJpegQuality}
                onChange={(e) => {
                  const v = parseInt((e.target as HTMLInputElement).value || '85', 10);
                  setFastJpegQuality(Math.max(1, Math.min(100, v)));
                }}
              />
            </div>
          </div>
        )}
      </div>

      {/* ============ Global save (commits both upload config + camera params) ============ */}
      <Button variant="primary" disabled={saving} onClick={saveAll} className="mt-2">
        {saving ? '...' : i18n._('common.save')}
      </Button>
    </div>
  );
}
