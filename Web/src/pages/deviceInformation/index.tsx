import { useEffect, useState, useRef } from 'preact/hooks';
import { useLingui } from '@lingui/react';
import { Card, CardContent } from '@/components/ui/card';
import { Label } from '@/components/ui/label';
import { Separator } from '@/components/ui/separator';
import { Popover, PopoverContent, PopoverTrigger } from '@/components/ui/popover';
import { Input } from '@/components/ui/input';
import { Button } from '@/components/ui/button';
import DeviceInformationSkeleton from './skeleton';
import SvgIcon from '@/components/svg-icon';
import { useSystemInfo } from '@/store/systemInfo'
import systemApis from '@/services/api/system';
import { getNetLog, clearNetLog, NET_DIAG } from '@/services/request';
import { toast } from 'sonner';
import type { DeviceInfo } from '@/services/api/system';

export default function DeviceInformation() {
  const { i18n } = useLingui();
  const { loading, deviceInfo, getDeviceInfo } = useSystemInfo();
  const { setDeviceNameReq } = systemApis;
  const [formatDeviceInfo, setFormatDeviceInfo] = useState<(DeviceInfo & { power?: 'high' | 'middle' | 'low' }) | null>(null)
  const [isEdit, setIsEdit] = useState(false)
  const [deviceName, setDeviceName] = useState(deviceInfo?.device_name ?? '')
  const [showPopover, setShowPopover] = useState(true)
  const [isLoading, setIsLoading] = useState(false)
  const [netLog, setNetLog] = useState<any[]>([])
  // Refresh on mount: testers navigate here after seeing the error toast
  useEffect(() => { setNetLog(getNetLog()) }, [])
  const inputRef = useRef<HTMLInputElement>(null)
  const deviceImg = new URL('@/assets/images/camthink_Vision_AI_camera.webp', import.meta.url).href;
  const errorsRef = useRef<{ isValidate: boolean, message: string }>({ isValidate: true, message: '' })

  useEffect(() => {
    getDeviceInfo();
  }, []);

  useEffect(() => {
    if (!deviceInfo) return;
    const storageCardInfo = (deviceInfo.storage_card_info?.match(/^.*?GB/) || [''])[0];
    setFormatDeviceInfo({
      ...deviceInfo,
      storage_card_info: storageCardInfo,
      power: deviceInfo.battery_percent === 0 ? '' : (deviceInfo.battery_percent ?? 0) >= 70
        ? 'high'
        : (deviceInfo.battery_percent ?? 0) >= 30
          ? 'middle'
          : 'low',
    } as DeviceInfo);
  }, [deviceInfo]);

  const validateDeviceName = () => {
    if (deviceName.length <= 0 || deviceName.length > 32) {
      errorsRef.current = {
        isValidate: false,
        message: 'device_name_length_error',
      }
      return false
    }
    // Support Chinese, English, numbers, underscores, and hyphens
    if (!/^[\u4e00-\u9fa5a-zA-Z0-9_-]+$/.test(deviceName)) {
      errorsRef.current = {
        isValidate: false,
        message: 'device_name_illegal',
      }
      return false
    }
    return true
  }

  useEffect(() => {
    setDeviceName(deviceInfo?.device_name ?? '')
  }, [deviceInfo])

  const handleBlur = () => {
    if (deviceName !== deviceInfo?.device_name) {
      setShowPopover(true)
    } else {
      setIsEdit(false)
    }
  }
  const handleKeyDown = (e: KeyboardEvent) => {
    if (e.key === 'Enter') {
      e.preventDefault()
      setShowPopover(true)
    } else if (e.key === 'Escape') {
      e.preventDefault()
      setIsEdit(false)
      setShowPopover(false)
    }
  }
  const handleCancel = () => {
    setDeviceName(deviceInfo?.device_name ?? '')
    setShowPopover(false)
    setIsEdit(false)
  }

  const handleConfirm = async () => {
    if (deviceName === deviceInfo?.device_name) {
      setShowPopover(false)
      setIsEdit(false)
      return
    }
    try {
      if (!validateDeviceName()) {
        toast.error(i18n._(`sys.header.${errorsRef?.current?.message}`))
        setShowPopover(false)
        setIsEdit(false)
        setDeviceName(deviceInfo?.device_name ?? '')
        return
      }
      setIsLoading(true)
      await setDeviceNameReq({ device_name: deviceName.trim() })
      await getDeviceInfo()
      toast.success(i18n._('sys.header.device_name_updated'))
      setShowPopover(false)
      setIsEdit(false)
    } catch (error) {
      console.error('handleConfirm', error)
      toast.error(i18n._('common.update_failed'))
      // Restore original value
      setDeviceName(deviceInfo?.device_name ?? '')
    } finally {
      setIsLoading(false)
    }
  }
  const handleEdit = () => {
    setIsEdit(true)
    // Delay focus, wait for Input component to finish rendering
    setTimeout(() => {
      inputRef.current?.focus()
    }, 0)
  }
  return (
    <div className="px-4 py-8 flex flex-col gap-4 items-center justify-center">
      {/* <h2 className="text-2xl font-bold text-gray-900">
        {i18n._('sys.menu.device_information')}
      </h2> */}
      <div className="mb-4">
        <img src={deviceImg} alt="camthink_Vision_AI_camera" />
      </div>
      <Card className="w-full sm:w-xl">
        <CardContent className="flex flex-col gap-4">
          {loading ? <DeviceInformationSkeleton /> : (
            <>
              <div className="flex justify-between items-center">
                <Label>{i18n._('sys.device_information.title')}</Label>
                {isEdit ? (
                  <Popover
                    open={showPopover}
                    onOpenChange={(open) => {
                      if (!open) {
                        setShowPopover(false)
                      }
                    }}
                  >
                    <PopoverTrigger asChild>
                      <Input
                        ref={inputRef}
                        variant="ghost"
                        type="text"
                        value={deviceName}
                        onChange={(e) => setDeviceName((e.target as HTMLInputElement)?.value ?? '')}
                        onBlur={handleBlur}
                        onFocus={() => {
                          setShowPopover(false)
                        }}
                        onKeyDown={handleKeyDown}
                        className="w-48"
                      />
                    </PopoverTrigger>
                    <PopoverContent className="w-full p-3">
                      <div className="space-y-2">
                        <p className="text-sm font-medium">{i18n._('sys.device_information.is_edit_device_name')}</p>
                        {/* <p className="text-xs text-gray-500">{i18n._('common.is_edit_device_name_desc')}</p> */}
                        <div className="flex gap-2">
                          <Button
                            size="sm"
                            variant="outline"
                            onClick={handleCancel}
                          >
                            {i18n._('common.cancel')}
                          </Button>
                          <Button
                            size="sm"
                            variant="primary"
                            onClick={handleConfirm}
                            disabled={isLoading}
                          >
                            {isLoading ? i18n._('common.save') : i18n._('common.confirm')}
                          </Button>
                        </div>
                      </div>
                    </PopoverContent>
                  </Popover>
                ) : (
                  <div className="flex items-center gap-2">
                    <p className="text-text-primary text-sm">{formatDeviceInfo?.device_name ?? '--'}</p>
                    <Button
                      variant="ghost"
                      size="sm"
                      onClick={() => handleEdit()}
                      className="w-6 h-6 flex items-center justify-center p-0"
                    >
                      <SvgIcon icon="pen" className="w-3 h-3" />
                    </Button>
                  </div>
                )}
              </div>
              <Separator />
              <div className="flex justify-between">
                <Label>{i18n._('sys.device_information.mac')}</Label>
                <p className="text-text-primary text-sm">{formatDeviceInfo?.mac_address ?? '--'}</p>
              </div>
              <Separator />
              <div className="flex justify-between">
                <Label>{i18n._('sys.device_information.sn')}</Label>
                <p className="text-text-primary text-sm">{formatDeviceInfo?.serial_number ?? '--'}</p>
              </div>
              <Separator />
              <div className="flex justify-between">
                <Label>{i18n._('sys.device_information.hardware_version')}</Label>
                <p className="text-text-primary text-sm">{formatDeviceInfo?.hardware_version ?? '--'}</p>
              </div>
              <Separator />
              <div className="flex justify-between">
                <Label>{i18n._('sys.device_information.software_version')}</Label>
                <p className="text-text-primary text-sm">{formatDeviceInfo?.software_version ?? '--'}</p>
              </div>
              <Separator />
              <div className="flex justify-between">
                <Label>{i18n._('sys.device_information.camera_module')}</Label>
                <p className="text-text-primary text-sm">{formatDeviceInfo?.camera_module ?? '--'}</p>
              </div>
              <Separator />
              <div className="flex justify-between">
                <Label>{i18n._('sys.device_information.expansion_module')}</Label>
                <p className="text-text-primary text-sm">{(formatDeviceInfo?.extension_modules && formatDeviceInfo.extension_modules !== '-') ? formatDeviceInfo?.extension_modules.split(',').join(',') : '--'}</p>
              </div>
              <Separator />
              <div className="flex justify-between">
                <Label>{i18n._('sys.device_information.storage_card')}</Label>
                <p className="text-text-primary text-sm">{formatDeviceInfo?.storage_card_info || '--'}</p>
              </div>
              <Separator />
              <div className="flex justify-between">
                <Label>{i18n._('sys.device_information.power')}</Label>
                {formatDeviceInfo?.power ? (
                  <div className="text-text-primary text-sm flex items-center gap-1">
                    <span>{i18n._('sys.device_information.battery')}</span>
                    -
                    {formatDeviceInfo?.power === 'high' ? <SvgIcon className="w-6 h-6" icon="batter_full" /> : formatDeviceInfo?.power === 'low' ? <SvgIcon className="w-6 h-6 flex items-center justify-center" icon="battery_low" /> : <SvgIcon className="w-6 h-6 flex items-center justify-center" icon="battery_middle" />}
                    <span>{i18n._(`sys.device_information.power_${formatDeviceInfo?.power}`)}</span>
                  </div>
                ) : <p className="text-text-primary text-sm">--</p>}
              </div>
              <Separator />
              <div className="flex justify-between">
                <Label>{i18n._('sys.device_information.communication')}</Label>
                <p className="text-text-primary text-sm">{formatDeviceInfo?.communication_type ?? '--'}</p>
              </div>
            </>
          )}
        </CardContent>
      </Card>
      {NET_DIAG && netLog.length > 0 && (
        <Card className="w-full sm:w-xl">
          <CardContent className="flex flex-col gap-2">
            <div className="flex justify-between items-center">
              <Label>{i18n._('sys.device_information.net_fail_log')}</Label>
              <Button
                variant="outline"
                size="sm"
                onClick={() => { clearNetLog(); setNetLog([]) }}
              >
                {i18n._('common.clear')}
              </Button>
            </div>
            <div className="max-h-64 overflow-y-auto text-xs flex flex-col gap-1.5">
              {netLog.slice().reverse().map((e, i) => (
                <div key={i} className="flex flex-col">
                  <span className="text-text-primary break-all">{e.time} {e.api}</span>
                  <span className="text-gray-500 break-all">
                    {e.phase} | {e.code || '--'}{e.status ? ` | http ${e.status}` : ''}{e.elapsedMs != null ? ` | ${e.elapsedMs}ms` : ''}
                  </span>
                  <span className="text-gray-400 break-all">{e.message}</span>
                </div>
              ))}
            </div>
          </CardContent>
        </Card>
      )}
    </div>
  );
} 