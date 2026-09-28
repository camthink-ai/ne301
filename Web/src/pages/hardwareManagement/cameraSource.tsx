import { useEffect, useState } from 'preact/hooks';
import { useLingui } from '@lingui/react';
import { Label } from '@/components/ui/label';
import { Separator } from '@/components/ui/separator';
import { Button } from '@/components/ui/button';
import {
  Select, SelectContent, SelectItem, SelectTrigger, SelectValue,
} from '@/components/ui/select';
import { toast } from 'sonner';
import cameraApi, { type CameraConfigData, type CameraListData } from '@/services/api/camera';

/**
 * Image source selection (native DCMIPP camera / USB UVC camera, MJPEG).
 * Reboot-apply: saving only persists the config; the active source changes
 * on the next boot. The Detect button cross-probes the non-active camera
 * (native <-> uvc) and lists its stream configurations.
 */
export default function CameraSource() {
  const { i18n } = useLingui();
  const [camCfg, setCamCfg] = useState<CameraConfigData | null>(null);
  const [initCamCfg, setInitCamCfg] = useState<CameraConfigData | null>(null);
  const [detected, setDetected] = useState<CameraListData | null>(null);
  const [runtimeActive, setRuntimeActive] = useState<string | null>(null);
  const [detecting, setDetecting] = useState(false);
  const [saving, setSaving] = useState(false);
  const [loading, setLoading] = useState(true);

  const load = async () => {
    setLoading(true);
    try {
      const res = await cameraApi.getCameraConfig();
      setCamCfg(res.data);
      setInitCamCfg(res.data);
      /* Seed the camera list so a SAVED resolution resolves its option
       * immediately, and capture the RUNNING source for the pending-
       * restart display (the endpoint also reports saved_source). */
      try {
        const lst = await cameraApi.getCameraList();
        setDetected(lst.data);
        setRuntimeActive((lst.data as { active?: string }).active ?? null);
      } catch (e) {
        console.error('initCameraList', e);
      }
    } catch (e) {
      console.error('initCameraSource', e);
    } finally {
      setLoading(false);
    }
  };

  useEffect(() => { load(); }, []);

  const runDetect = async () => {
    setDetecting(true);
    try {
      const res = await cameraApi.detectCameras();
      setDetected(res.data);
    } catch (e) {
      console.error(e);
      toast.error(i18n._('sys.hardware_management.camera_not_detected'));
    } finally {
      setDetecting(false);
    }
  };

  const save = async () => {
    if (!camCfg) return;
    setSaving(true);
    try {
      await cameraApi.setCameraConfig({
        source: camCfg.source,
        native_stream_res: camCfg.native_stream_res,
        uvc_stream: camCfg.uvc_stream,
      });
      setInitCamCfg({ ...camCfg });
      toast.warning(i18n._('sys.hardware_management.camera_source_reboot_hint'));
    } catch (e) {
      console.error(e);
      toast.error(i18n._('sys.hardware_management.camera_source_save_failed'));
    } finally {
      setSaving(false);
    }
  };

  if (loading || !camCfg) {
    return (
      <div className="flex flex-col gap-2 mt-4">
        <div className="h-8 bg-gray-200 rounded animate-pulse" />
        <div className="h-8 bg-gray-200 rounded animate-pulse" />
      </div>
    );
  }

  const cameraSource = camCfg.source;
  const uvcCam = (detected?.cameras ?? []).find((c) => c.type === 'uvc');
  const nativeCam = (detected?.cameras ?? []).find((c) => c.type === 'native');
  const changed = initCamCfg != null
    && (camCfg.source !== initCamCfg.source
      || camCfg.native_stream_res !== initCamCfg.native_stream_res
      || camCfg.uvc_stream.width !== initCamCfg.uvc_stream.width
      || camCfg.uvc_stream.height !== initCamCfg.uvc_stream.height);

  /* AI decode budget: the device skips inference above 1080P. Explicit
   * selection compares directly; "auto" resolves to the camera's highest
   * offered configuration once detected. */
  const maxUvcCfg = (uvcCam?.configs ?? []).reduce(
    (m, c) => (c.width * c.height > m.width * m.height ? c : m),
    { width: 0, height: 0, fps: 0 },
  );
  const effW = camCfg.uvc_stream.width || maxUvcCfg.width;
  const effH = camCfg.uvc_stream.height || maxUvcCfg.height;
  const oversize = cameraSource === 'uvc'
    && effW * effH > 1920 * 1080;

  return (
    <div className="flex flex-col gap-2 mt-4">
      <div className="flex w-full items-center justify-between text-left">
        <Label>{i18n._('sys.hardware_management.camera_source')}</Label>
        <Button
          variant="outline"
          size="sm"
          className="w-fit"
          disabled={detecting}
          onClick={runDetect}
        >
          {detecting
            ? i18n._('sys.hardware_management.camera_detecting')
            : i18n._('sys.hardware_management.camera_detect')}
        </Button>
      </div>

      {/* source selector */}
      <div className="flex justify-between gap-4 items-center">
        <Label>{i18n._('sys.hardware_management.camera_source')}</Label>
        <Select
          value={cameraSource}
          onValueChange={(v) => {
            setCamCfg((prev) => (prev ? { ...prev, source: v as 'native' | 'uvc' } : prev));
          }}
        >
          <SelectTrigger className="border-0 shadow-none focus-visible:ring-0 w-fit">
            <SelectValue />
          </SelectTrigger>
          <SelectContent>
            <SelectItem value="native">
              {i18n._('sys.hardware_management.camera_source_native')}
            </SelectItem>
            <SelectItem value="uvc">
              {i18n._('sys.hardware_management.camera_source_uvc')}
            </SelectItem>
          </SelectContent>
        </Select>
      </div>
      <Separator />

      {cameraSource === 'native' ? (
        <>
          {/* detected model */}
          {nativeCam && (
            <p className="text-xs text-gray-500">
              {nativeCam.present
                ? nativeCam.model
                : i18n._('sys.hardware_management.camera_not_detected')}
            </p>
          )}
          {/* stream resolution (preview/encode only; AI pipe follows the model) */}
          <div className="flex justify-between gap-4 items-center">
            <Label>{i18n._('sys.hardware_management.native_stream_res')}</Label>
            <Select
              value={String(camCfg.native_stream_res)}
              onValueChange={(v) => {
                const res = Number(v ?? '0');
                setCamCfg((prev) => (prev ? { ...prev, native_stream_res: res } : prev));
              }}
            >
              <SelectTrigger className="border-0 shadow-none focus-visible:ring-0 w-fit">
                <SelectValue />
              </SelectTrigger>
              <SelectContent>
                <SelectItem value="0">720P</SelectItem>
                <SelectItem value="1">1080P</SelectItem>
              </SelectContent>
            </Select>
          </div>
        </>
      ) : (
        <>
          {/* detected uvc camera + config list */}
          {uvcCam && (
            <p className="text-xs text-gray-500">
              {uvcCam.present
                ? `${uvcCam.product} (${uvcCam.vid}/${uvcCam.pid}${uvcCam.bulk ? ' bulk' : ' iso'})`
                : i18n._('sys.hardware_management.camera_not_detected')}
            </p>
          )}
          <div className="flex justify-between gap-4 items-center">
            <Label>{i18n._('sys.hardware_management.uvc_stream_cfg')}</Label>
            <Select
              value={
                camCfg.uvc_stream.width === 0
                  ? 'auto'
                  : `${camCfg.uvc_stream.width}x${camCfg.uvc_stream.height}`
              }
              onValueChange={(v) => {
                if (v === 'auto') {
                  setCamCfg((prev) => (prev
                    ? { ...prev, uvc_stream: { width: 0, height: 0, fps: 0 } }
                    : prev));
                } else {
                  const sel = (uvcCam?.configs ?? []).find(
                    (c) => `${c.width}x${c.height}` === v,
                  );
                  setCamCfg((prev) => (prev && sel
                    ? { ...prev, uvc_stream: { width: sel.width, height: sel.height, fps: sel.fps } }
                    : prev));
                }
              }}
            >
              <SelectTrigger className="border-0 shadow-none focus-visible:ring-0 w-fit">
                <SelectValue />
              </SelectTrigger>
              <SelectContent>
                <SelectItem value="auto">
                  {i18n._('sys.hardware_management.camera_auto')}
                </SelectItem>
                {(uvcCam?.configs ?? []).map((c) => (
                  <SelectItem key={`${c.width}x${c.height}`} value={`${c.width}x${c.height}`}>
                    {c.width}x{c.height}
                  </SelectItem>
                ))}
              </SelectContent>
            </Select>
          </div>
        </>
      )}
          {oversize && (
            <p className="text-xs text-red-600 font-medium">
              {i18n._('sys.hardware_management.camera_oversize_warn')}
            </p>
          )}
      {(() => {
        if (runtimeActive == null) return null;
        /* pending state = source differs, or a saved UVC resolution that
         * is not what is actually streaming right now */
        const resPending = camCfg.source === 'uvc'
          && camCfg.uvc_stream.width !== 0
          && detected?.uvc_status?.streaming === true
          && (detected.uvc_status.width !== camCfg.uvc_stream.width
            || detected.uvc_status.height !== camCfg.uvc_stream.height);
        if (runtimeActive !== camCfg.source || resPending) {
          const runSrc = runtimeActive === 'uvc' ? 'UVC' : 'Native';
          const savedSrc = camCfg.source === 'uvc' ? 'UVC' : 'Native';
          const runRes = detected?.uvc_status?.streaming
            ? `${detected.uvc_status.width}x${detected.uvc_status.height}`
            : undefined;
          const savedRes = camCfg.uvc_stream.width !== 0
            ? `${camCfg.uvc_stream.width}x${camCfg.uvc_stream.height}`
            : undefined;
          return (
            <p className="text-xs text-red-600 font-medium">
              {i18n._('sys.hardware_management.camera_pending_restart')
                .replace('{active}', resPending && runRes ? `${runSrc} ${runRes}` : runSrc)
                .replace('{saved}', resPending && savedRes ? `${savedSrc} ${savedRes}` : savedSrc)}
            </p>
          );
        }
        return null;
      })()}
      <p className="text-xs text-gray-500">
        {i18n._('sys.hardware_management.camera_active_now')}
        {runtimeActive != null ? (runtimeActive === 'uvc' ? ' UVC (MJPEG)' : ' Native (OS04C10)') : ' -'}
      </p>
      <p className="text-xs text-amber-600">
        {i18n._('sys.hardware_management.camera_source_reboot_hint')}
      </p>

      <Button variant="primary" disabled={saving || !changed} onClick={save} className="mt-2 self-end">
        {saving ? '...' : i18n._('common.save')}
      </Button>
    </div>
  );
}
