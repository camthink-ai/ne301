import { useState, useEffect } from 'preact/hooks';
import { useLingui } from '@lingui/react';
import { Label } from '@/components/ui/label';
import { Input } from '@/components/ui/input';
import {
  Select,
  SelectContent,
  SelectItem,
  SelectTrigger,
  SelectValue,
} from '@/components/ui/select';
import { Switch } from '@/components/ui/switch';
import { Button } from '@/components/ui/button';
import { Separator } from '@/components/ui/separator';
import { Skeleton } from '@/components/ui/skeleton';
import SvgIcon from '@/components/svg-icon';
import deviceTool, {
  type RtspAuthMode,
  type RtspStatus,
} from '@/services/api/deviceTool';
import { toast } from 'sonner';

type RtspConfigState = {
  enabled: boolean;
  port: number;
  auth_mode: RtspAuthMode;
  username: string;
  password: string;
};

type Errors = {
  port: { error: boolean; message: string };
  username: { error: boolean; message: string };
  password: { error: boolean; message: string };
};

export default function RtspConfig() {
  const { i18n } = useLingui();
  const { getRtspConfigReq, setRtspConfigReq, getRtspStatusReq } = deviceTool;
  const [loading, setLoading] = useState(false);
  const [saving, setSaving] = useState(false);
  const [isPasswordVisible, setIsPasswordVisible] = useState(false);
  const [copied, setCopied] = useState(false);

  const [config, setConfig] = useState<RtspConfigState>({
    enabled: false,
    port: 554,
    auth_mode: 'none',
    username: '',
    password: '',
  });

  const [status, setStatus] = useState<RtspStatus>({
    running: false,
    stream_url: '',
  });

  const [errors, setErrors] = useState<Errors>({
    port: { error: false, message: '' },
    username: { error: false, message: '' },
    password: { error: false, message: '' },
  });

  const deviceIp = window.location.hostname;

  const streamUrl = config.enabled
    ? `rtsp://${config.auth_mode === 'digest' && config.username ? `${config.username}:${config.password}@` : ''}${deviceIp}:${config.port}/live/stream`
    : '';

  const fetchConfig = async () => {
    try {
      setLoading(true);
      const res = await getRtspConfigReq();
      setConfig(res.data);
    } catch (error) {
      console.error('fetchRtspConfig', error);
    } finally {
      setLoading(false);
    }
  };

  const fetchStatus = async () => {
    try {
      const res = await getRtspStatusReq();
      setStatus(res.data);
    } catch (error) {
      console.error('fetchRtspStatus', error);
    }
  };

  useEffect(() => {
    fetchConfig();
    fetchStatus();
  }, []);

  const validate = (): boolean => {
    let valid = true;
    const { port } = config;

    if (!port || port < 1 || port > 65535) {
      setErrors(prev => ({
        ...prev,
        port: { error: true, message: 'sys.device_tool.rtsp.port_error' },
      }));
      valid = false;
    } else {
      setErrors(prev => ({ ...prev, port: { error: false, message: '' } }));
    }

    if (config.auth_mode === 'digest') {
      if (!config.username) {
        setErrors(prev => ({
          ...prev,
          username: {
            error: true,
            message: 'sys.device_tool.rtsp.username_required',
          },
        }));
        valid = false;
      } else {
        setErrors(prev => ({
          ...prev,
          username: { error: false, message: '' },
        }));
      }
      if (!config.password) {
        setErrors(prev => ({
          ...prev,
          password: {
            error: true,
            message: 'sys.device_tool.rtsp.password_required',
          },
        }));
        valid = false;
      } else {
        setErrors(prev => ({
          ...prev,
          password: { error: false, message: '' },
        }));
      }
    }

    return valid;
  };

  const handleSave = async () => {
    if (!validate()) return;
    try {
      setSaving(true);
      await setRtspConfigReq(config);
      await Promise.all([fetchConfig(), fetchStatus()]);
      toast.success(i18n._('sys.device_tool.rtsp.config_saved'));
    } catch (error) {
      console.error('saveRtspConfig', error);
    } finally {
      setSaving(false);
    }
  };

  const handleCopy = async () => {
    if (!streamUrl) return;
    const copyText = `rtsp://${config.auth_mode === 'digest' && config.username ? `${config.username}:${config.password}@` : ''}${deviceIp}:${config.port}/live/stream`;
    try {
      await navigator.clipboard.writeText(copyText);
    } catch {
      const ta = document.createElement('textarea');
      ta.value = copyText;
      ta.style.position = 'fixed';
      ta.style.opacity = '0';
      document.body.appendChild(ta);
      ta.select();
      document.execCommand('copy');
      document.body.removeChild(ta);
    }
    setCopied(true);
    setTimeout(() => setCopied(false), 2000);
  };

  const skeleton = () => (
    <div className="flex flex-col gap-2">
      <Skeleton className="h-10 w-full mb-2" />
      <Skeleton className="h-10 w-full mb-2" />
    </div>
  );

  return (
    <div>
      {loading ? (
        skeleton()
      ) : (
        <>
          {/* Status */}
          <div className="flex justify-between gap-2 mb-2 flex-wrap">
            <div className="flex items-center gap-2">
              <Label className="text-sm text-text-primary">
                {i18n._('common.status')}:
              </Label>
              <div className="flex items-center gap-2">
                <div
                  className={`w-2 h-2 rounded-full ${status.running ? 'bg-green-500' : 'bg-gray-500'}`}
                />
                <p
                  className={`text-sm ${status.running ? 'text-green-500' : 'text-gray-500'}`}
                >
                  {status.running
                    ? i18n._('common.connected')
                    : i18n._('common.disconnected')}
                </p>
              </div>
            </div>
          </div>
          <Separator />

          {/* Enable switch */}
          <div className="flex flex-col gap-1 py-2">
            <div className="flex justify-between items-center">
              <Label className="text-sm text-text-primary">
                {i18n._('sys.device_tool.rtsp.enable')}
              </Label>
              <Switch
                checked={config.enabled}
                onCheckedChange={async v => {
                  const newConfig = { ...config, enabled: v };
                  setConfig(newConfig);
                  try {
                    await setRtspConfigReq(newConfig);
                    await Promise.all([fetchConfig(), fetchStatus()]);
                    toast.success(i18n._('sys.device_tool.rtsp.config_saved'));
                  } catch (error) {
                    setConfig({ ...config, enabled: !v });
                    console.error('toggleRtspEnable', error);
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
            className={config.enabled ? '' : 'opacity-50 pointer-events-none'}
          >
            {/* Port */}
            <div className="flex flex-col gap-1 py-2">
              <div className="flex justify-between gap-2">
                <Label className="text-sm text-text-primary shrink-0">
                  {i18n._('sys.device_tool.rtsp.port')}
                </Label>
                <Input
                  variant="ghost"
                  type="number"
                  min={1}
                  max={65535}
                  placeholder="554"
                  value={config.port || ''}
                  onChange={e => {
                    const v = parseInt(
                      (e.target as HTMLInputElement).value,
                      10
                    );
                    setConfig({ ...config, port: Number.isNaN(v) ? 0 : v });
                  }}
                />
              </div>
              {errors.port.error && (
                <p className="text-sm text-red-500 self-end">
                  {i18n._(errors.port.message)}
                </p>
              )}
            </div>
            <Separator />

            {/* Auth mode */}
            <div className="flex justify-between items-center gap-2 py-2">
              <Label className="text-sm text-text-primary shrink-0">
                {i18n._('sys.device_tool.rtsp.auth_mode')}
              </Label>
              <Select
                value={config.auth_mode}
                onValueChange={v => setConfig({ ...config, auth_mode: v as RtspAuthMode })}
              >
                <SelectTrigger className="bg-transparent border-0 !shadow-none !outline-none text-right min-w-0">
                  <SelectValue />
                </SelectTrigger>
                <SelectContent>
                  <SelectItem value="none">
                    {i18n._('sys.device_tool.rtsp.auth_none')}
                  </SelectItem>
                  <SelectItem value="digest">
                    {i18n._('sys.device_tool.rtsp.auth_digest')}
                  </SelectItem>
                </SelectContent>
              </Select>
            </div>

            {/* Username / Password (digest only) */}
            {config.auth_mode === 'digest' && (
              <>
                <Separator />
                <div className="flex flex-col gap-1 py-2">
                  <div className="flex justify-between gap-2">
                    <Label className="text-sm text-text-primary shrink-0">
                      {i18n._('sys.device_tool.rtsp.username')}
                    </Label>
                    <Input
                      variant="ghost"
                      placeholder={i18n._('common.please_enter')}
                      value={config.username}
                      onChange={e => setConfig({
                          ...config,
                          username: (e.target as HTMLInputElement).value,
                        })}
                    />
                  </div>
                  {errors.username.error && (
                    <p className="text-sm text-red-500 self-end">
                      {i18n._(errors.username.message)}
                    </p>
                  )}
                </div>
                <Separator />
                <div className="flex flex-col gap-1 py-2">
                  <div className="flex justify-between gap-2">
                    <Label className="text-sm text-text-primary shrink-0">
                      {i18n._('sys.device_tool.rtsp.password')}
                    </Label>
                    <div className="flex flex-1 gap-2 items-center">
                      <Input
                        variant="ghost"
                        type={isPasswordVisible ? 'text' : 'password'}
                        placeholder={i18n._('common.please_enter')}
                        value={config.password}
                        onChange={e => setConfig({
                            ...config,
                            password: (e.target as HTMLInputElement).value,
                          })}
                      />
                      <button
                        type="button"
                        onClick={() => setIsPasswordVisible(!isPasswordVisible)}
                        className="flex items-center bg-transparent pr-4 border-none cursor-pointer"
                      >
                        <SvgIcon
                          className="w-4 h-4"
                          icon={
                            isPasswordVisible ? 'visibility' : 'visibility_off'
                          }
                        />
                      </button>
                    </div>
                  </div>
                  {errors.password.error && (
                    <p className="text-sm text-red-500 self-end">
                      {i18n._(errors.password.message)}
                    </p>
                  )}
                </div>
              </>
            )}

            <Separator />

            {/* Stream URL */}
            <div className="py-2">
              <Label className="text-sm text-text-primary mb-2 block">
                {i18n._('sys.device_tool.rtsp.stream_url')}
              </Label>
              <div className="flex items-center gap-2 min-w-0 overflow-hidden bg-gray-100 border border-dashed border-gray-300 rounded-md px-3 py-2">
                <span className="text-sm text-orange-500 font-medium shrink-0">
                  {i18n._('sys.device_tool.rtsp.video_stream')}
                </span>
                <span className="flex-1 min-w-0 font-mono text-sm text-text-primary truncate select-all">
                  {streamUrl}
                </span>
                <button
                  type="button"
                  onClick={handleCopy}
                  className="shrink-0 p-1 rounded hover:bg-gray-200 transition-colors"
                  title={i18n._('sys.device_tool.rtsp.copy')}
                >
                  <SvgIcon
                    className="w-4 h-4 text-text-secondary"
                    icon="content_copy"
                  />
                </button>
                {copied && (
                  <span className="text-xs text-green-500 shrink-0">
                    {i18n._('sys.device_tool.rtsp.copied')}
                  </span>
                )}
              </div>
            </div>
          </div>

          {/* Save button */}
          <div className="flex justify-end gap-2 mt-2">
            <Button variant="primary" disabled={saving} onClick={handleSave}>
              {saving ? (
                <div className="w-full h-full flex items-center justify-center">
                  <div
                    className="w-4 h-4 rounded-full border-2 border-[#f24a00] border-t-transparent animate-spin"
                    aria-label="loading"
                  />
                </div>
              ) : (
                i18n._('common.save')
              )}
            </Button>
          </div>
        </>
      )}
    </div>
  );
}
