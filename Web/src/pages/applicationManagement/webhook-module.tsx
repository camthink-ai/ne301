import { useState, useEffect } from 'preact/hooks';
import { Label } from '@/components/ui/label';
import { Input } from '@/components/ui/input';
import {
  Select,
  SelectContent,
  SelectItem,
  SelectTrigger,
  SelectValue,
} from '@/components/ui/select';
import { Button } from '@/components/ui/button';
import { Switch } from '@/components/ui/switch';
import { Separator } from '@/components/ui/separator';
import { useLingui } from '@lingui/react';
import SvgIcon from '@/components/svg-icon';
import Upload from '@/components/upload';
import { Tooltip, TooltipContent, TooltipTrigger } from '@/components/tooltip';
import { readCertificateFile } from '@/utils/readFile';
import applicationManagement, {
  type WebhookConfig,
} from '@/services/api/applicationManagement';
import ApplicationManagementSkeleton from './skeleton';
import { toast } from 'sonner';

type AuthType = 'none' | 'bearer' | 'basic' | 'custom';

type ErrorType = { error: boolean; message: string };
type Errors = {
  url: ErrorType;
  secret: ErrorType;
};

const certAccept = {
  'application/x-x509-ca-cert': ['.crt'],
  'application/x-pem-file': ['.pem'],
  'application/x-x509-user-cert': ['.der', '.cer'],
};

export default function WebhookModule() {
  const { i18n } = useLingui();
  const {
    getWebhookConfigReq,
    setWebhookConfigReq,
    testWebhookReq,
    getWebhookCaCertReq,
    setWebhookCaCertReq,
    deleteWebhookCaCertReq,
  } = applicationManagement;

  const [loading, setLoading] = useState(false);
  const [testLoading, setTestLoading] = useState(false);
  const [isSecretVisible, setIsSecretVisible] = useState(false);
  const [autoCheck, setAutoCheck] = useState(false);
  const [caCertData, setCaCertData] = useState('');
  const [caCertFilename, setCaCertFilename] = useState('');
  const [initialHasCa, setInitialHasCa] = useState(false);

  const uploadSlot = (
    <>
      <SvgIcon icon="upload" />
      {i18n._('common.upload')}
    </>
  );

  const [config, setConfig] = useState<WebhookConfig>({
    enable: false,
    url: '',
    auth_type: 'none',
    secret: '',
  });

  const [errors, setErrors] = useState<Errors>({
    url: { error: false, message: '' },
    secret: { error: false, message: '' },
  });

  const validateConfig = (): boolean => {
    let isValid = true;

    if (config.url && !/^https?:\/\/.+/.test(config.url)) {
      setErrors(prev => ({
        ...prev,
        url: {
          error: true,
          message: 'sys.application_management.webhook_url_error',
        },
      }));
      isValid = false;
    } else if (config.url && config.url.length > 256) {
      setErrors(prev => ({
        ...prev,
        url: {
          error: true,
          message: 'sys.application_management.webhook_url_error',
        },
      }));
      isValid = false;
    } else {
      setErrors(prev => ({ ...prev, url: { error: false, message: '' } }));
    }

    if (config.auth_type !== 'none' && !config.secret) {
      setErrors(prev => ({
        ...prev,
        secret: {
          error: true,
          message: 'sys.application_management.webhook_secret_error',
        },
      }));
      isValid = false;
    } else {
      setErrors(prev => ({ ...prev, secret: { error: false, message: '' } }));
    }

    return isValid;
  };

  useEffect(() => {
    if (autoCheck) validateConfig();
  }, [config]);

  const initConfig = async () => {
    try {
      setLoading(true);
      const res = await getWebhookConfigReq();
      setConfig(res.data);

      if (res.data.has_custom_ca) {
        try {
          const certRes = await getWebhookCaCertReq();
          setCaCertData(certRes.data.ca_cert_data);
          setCaCertFilename('custom-ca-cert.pem');
          setInitialHasCa(true);
        } catch {
          setCaCertData('');
          setCaCertFilename('');
          setInitialHasCa(false);
        }
      } else {
        setCaCertData('');
        setCaCertFilename('');
        setInitialHasCa(false);
      }
    } catch (error) {
      console.error('initWebhookConfig failed', error as Error);
    } finally {
      setLoading(false);
    }
  };

  useEffect(() => {
    initConfig();
  }, []);

  const handleSaveCaCert = async (file: File) => {
    const data = await readCertificateFile(file);
    if (!data) {
      toast.error(i18n._('sys.application_management.webhook_ca_cert_invalid'));
      return;
    }
    setCaCertData(data);
    setCaCertFilename(file.name);
  };

  const handleSave = async () => {
    try {
      setAutoCheck(true);
      if (!validateConfig()) return;
      await setWebhookConfigReq(config);

      if (caCertData) {
        await setWebhookCaCertReq({ ca_cert_data: caCertData });
      } else if (initialHasCa && !caCertData) {
        await deleteWebhookCaCertReq().catch(() => {});
      }

      await initConfig();
      toast.success(i18n._('sys.application_management.configSuccess'));
    } catch (error) {
      console.error('saveWebhookConfig failed', error as Error);
    }
  };

  const handleTest = async () => {
    try {
      setTestLoading(true);
      const res = await testWebhookReq();
      if (res.data?.success) {
        toast.success(
          i18n._('sys.application_management.webhook_test_success')
        );
      } else {
        toast.error(i18n._('sys.application_management.webhook_test_failed'));
      }
    } catch {
      toast.error(i18n._('sys.application_management.webhook_test_failed'));
    } finally {
      setTestLoading(false);
    }
  };

  const handlePasswordVisible = (e: MouseEvent) => {
    e.preventDefault();
    setIsSecretVisible(!isSecretVisible);
  };

  return (
    <div>
      {loading ? (
        <ApplicationManagementSkeleton />
      ) : (
        <>
          <div className="flex flex-col gap-2 mt-4 bg-gray-100 p-4 rounded-lg">
            <div className="flex justify-between">
              <Label>
                {i18n._('sys.application_management.webhook_enable')}
              </Label>
              <Switch
                checked={config.enable}
                onCheckedChange={v => setConfig({ ...config, enable: v })}
              />
            </div>
            <Separator />
            <div className="flex justify-between">
              <Label>
                {i18n._('sys.application_management.webhook_status')}
              </Label>
              <div className="flex items-center gap-2">
                <div
                  className={`w-2 h-2 rounded-full ${config.enable ? 'bg-green-500' : 'bg-gray-500'}`}
                />
                <p
                  className={config.enable ? 'text-green-500' : 'text-gray-500'}
                >
                  {config.enable
                    ? i18n._(
                        'sys.application_management.webhook_status_enabled'
                      )
                    : i18n._(
                        'sys.application_management.webhook_status_disabled'
                      )}
                </p>
              </div>
            </div>
            <Separator />
            <div className="flex flex-col gap-1">
              <div className="flex flex-col sm:flex-row gap-2 sm:justify-between">
                <Label className="shrink-0">
                  {i18n._('sys.application_management.webhook_url')}
                </Label>
                <Input
                  placeholder={i18n._(
                    'sys.application_management.webhook_url_placeholder'
                  )}
                  variant="ghost"
                  value={config.url}
                  onChange={e => setConfig({
                      ...config,
                      url: (e.target as HTMLInputElement).value,
                    })}
                />
              </div>
              {errors.url.error && (
                <p className="text-red-500 text-sm self-end">
                  {i18n._(errors.url.message)}
                </p>
              )}
            </div>
            <Separator />
            <div className="flex gap-2 justify-between">
              <Label>
                {i18n._('sys.application_management.webhook_auth_type')}
              </Label>
              <Select
                value={config.auth_type}
                onValueChange={v => setConfig({ ...config, auth_type: v as AuthType })}
              >
                <SelectTrigger className="bg-transparent border-0 !shadow-none !outline-none focus:!outline-none focus:!ring-0 focus:!ring-offset-0 focus:!shadow-none focus:!border-transparent focus-visible:!outline-none focus-visible:!ring-0 focus-visible:!ring-offset-0 text-right">
                  <SelectValue />
                </SelectTrigger>
                <SelectContent>
                  <SelectItem value="none">
                    {i18n._('sys.application_management.webhook_auth_none')}
                  </SelectItem>
                  <SelectItem value="bearer">Bearer</SelectItem>
                  <SelectItem value="basic">Basic</SelectItem>
                  <SelectItem value="custom">
                    {i18n._('sys.application_management.webhook_auth_custom')}
                  </SelectItem>
                </SelectContent>
              </Select>
            </div>
            {config.auth_type !== 'none' && (
              <>
                <Separator />
                <div className="flex flex-col gap-1">
                  <div className="flex gap-2 justify-between">
                    <Label className="shrink-0">
                      {i18n._('sys.application_management.webhook_secret')}
                    </Label>
                    <div className="flex flex-1 gap-2 justify-between">
                      <Input
                        placeholder={i18n._('common.please_enter')}
                        type={isSecretVisible ? 'text' : 'password'}
                        variant="ghost"
                        value={config.secret}
                        onChange={e => setConfig({
                            ...config,
                            secret: (e.target as HTMLInputElement).value,
                          })}
                      />
                      <button
                        type="button"
                        onClick={handlePasswordVisible}
                        className="flex items-center bg-transparent pr-4 border-none cursor-pointer disabled:opacity-50"
                      >
                        {isSecretVisible ? (
                          <SvgIcon className="w-4 h-4" icon="visibility" />
                        ) : (
                          <SvgIcon className="w-4 h-4" icon="visibility_off" />
                        )}
                      </button>
                    </div>
                  </div>
                  {errors.secret.error && (
                    <p className="text-red-500 text-sm self-end">
                      {i18n._(errors.secret.message)}
                    </p>
                  )}
                </div>
              </>
            )}
            <Separator />
            <div className="flex justify-between">
              <div className="flex items-center gap-2">
                <Label>
                  {i18n._('sys.application_management.webhook_ca_cert')}
                </Label>
                <Tooltip>
                  <TooltipTrigger>
                    <SvgIcon icon="info" className="w-4 h-4" />
                  </TooltipTrigger>
                  <TooltipContent className="max-w-80 text-pretty">
                    {i18n._(
                      'sys.application_management.webhook_ca_cert_tooltip'
                    )}
                  </TooltipContent>
                </Tooltip>
              </div>
              <div className="flex items-center gap-2">
                {caCertFilename ? (
                  <p className="text-sm text-text-primary">{caCertFilename}</p>
                ) : (
                  <p className="text-sm text-gray-400">
                    {i18n._(
                      'sys.application_management.webhook_ca_cert_placeholder'
                    )}
                  </p>
                )}
                <Upload
                  slot={uploadSlot}
                  type="button"
                  onFileChange={handleSaveCaCert}
                  accept={certAccept}
                  fileType={['.crt', '.pem', '.cer', '.der']}
                  maxSize={1024 * 8}
                  multiple={false}
                />
                <Button
                  variant="outline"
                  onClick={() => {
                    setCaCertData('');
                    setCaCertFilename('');
                  }}
                >
                  {i18n._('common.clear')}
                </Button>
              </div>
            </div>
          </div>
          <div className="w-full flex justify-end gap-2">
            <Button
              variant="outline"
              className="w-20 mt-4"
              onClick={handleSave}
            >
              {i18n._('common.save')}
            </Button>
            <Button
              variant="primary"
              disabled={testLoading}
              className="w-28 mt-4"
              onClick={handleTest}
            >
              {testLoading ? (
                <div className="w-full h-full flex items-center justify-center">
                  <div
                    className="w-4 h-4 rounded-full border-2 border-[#f24a00] border-t-transparent animate-spin"
                    aria-label="loading"
                  />
                </div>
              ) : (
                i18n._('sys.application_management.webhook_test')
              )}
            </Button>
          </div>
        </>
      )}
    </div>
  );
}
