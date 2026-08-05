import { useState, useRef, useEffect } from 'preact/hooks'
import SvgIcon from '@/components/svg-icon';
import { useLingui } from '@lingui/react'
import { useSystemInfo } from '@/store/systemInfo'
import { Input } from '@/components/ui/input'
import { Button } from '@/components/ui/button'
import { Popover, PopoverContent, PopoverTrigger } from '@/components/ui/popover'
import systemApis from '@/services/api/system'
import { toast } from 'sonner'
import { useIsMobile } from '@/hooks/use-mobile'
import { Tooltip, TooltipContent, TooltipTrigger } from '@/components/tooltip'
import { Link } from 'react-router-dom'
import { useCommunicationData } from '@/store/communicationData'

export default function DeviceInfo() {
    const { i18n } = useLingui()
    const isMobile = useIsMobile()
    const { deviceInfo, getDeviceInfo } = useSystemInfo()
    const { setDeviceNameReq } = systemApis
    const { getCommunicationData, communicationData } = useCommunicationData()
    const deviceImage = new URL('@/assets/images/camthink_Vision_AI_camera.webp', import.meta.url).href;
    const [isEdit, setIsEdit] = useState(false)
    const [deviceName, setDeviceName] = useState(deviceInfo?.device_name ?? '')
    const [powerStatus, setPowerStatus] = useState('power')
    const [showPopover, setShowPopover] = useState(false)
    const inputRef = useRef<HTMLInputElement>(null)
    const errorsRef = useRef<{ isValidate: boolean, message: string }>({ isValidate: true, message: '' })
    useEffect(() => {
        setPowerStatus(deviceInfo?.battery_percent === 0 ? 'power' : (deviceInfo?.battery_percent ?? 0) >= 70
            ? 'high'
            : (deviceInfo?.battery_percent ?? 0) >= 30
                ? 'middle'
                : 'low')
        setDeviceName(deviceInfo?.device_name ?? '')
    }, [deviceInfo])
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
        getCommunicationData()
    }, [])

    const communicationTypeSlot = () => (
        <div className="flex items-center space-x-1">
            {communicationData?.active_type === 'cellular' && (
                <Tooltip>
                    <TooltipTrigger>
                        <div className="w-5 h-5">
                            <Link to="/system-settings">
                                <SvgIcon icon="cellular" />
                            </Link>
                        </div>
                    </TooltipTrigger>
                    <TooltipContent className="absolute">
                        <p>{i18n._('sys.system_management.cellular_network')}</p>
                    </TooltipContent>
                </Tooltip>
            )}
            {communicationData?.active_type === 'wifi' && (
                <Tooltip>
                    <TooltipTrigger>
                        <div className="w-5 h-5">
                            <Link to="/system-settings">
                                <SvgIcon icon="wifi" />
                            </Link>
                        </div>
                    </TooltipTrigger>
                    <TooltipContent className="absolute">
                        <p>{i18n._('sys.system_management.wifi')}</p>
                    </TooltipContent>
                </Tooltip>
            )}
            {communicationData?.active_type === 'halow' && (
                <Tooltip>
                    <TooltipTrigger>
                        <div className="w-5 h-5">
                            <Link to="/system-settings">
                                <SvgIcon icon="wifi-halow" />
                            </Link>
                        </div>
                    </TooltipTrigger>
                    <TooltipContent className="absolute">
                        <p>{i18n._('sys.system_management.halow')}</p>
                    </TooltipContent>
                </Tooltip>
            )}
            {communicationData?.active_type === 'poe' && (
                <Tooltip>
                    <TooltipTrigger>
                        <div className="w-4 h-4">
                            <Link to="/system-settings">
                                <SvgIcon icon="ethernet_port" />
                            </Link>
                        </div>
                    </TooltipTrigger>
                    <TooltipContent className="absolute">
                        <p>{i18n._('sys.system_management.poe')}</p>
                    </TooltipContent>
                </Tooltip>
            )}
            {powerStatus === 'high' && (
                <Tooltip>
                    <TooltipTrigger>
                        <div className="w-5 h-5">
                            <Link to="/system-settings">
                                <SvgIcon icon="batter_full" />
                            </Link>
                        </div>
                    </TooltipTrigger>
                    <TooltipContent className="absolute">
                        <p>{i18n._('sys.device_information.power_high')}</p>
                    </TooltipContent>
                </Tooltip>
            )}
            {powerStatus === 'middle' && (
                <Tooltip>
                    <TooltipTrigger>
                        <div className="w-5 h-5">
                            <Link to="/system-settings">
                                <SvgIcon icon="battery_middle" />
                            </Link>
                        </div>
                    </TooltipTrigger>
                    <TooltipContent className="absolute">
                        <p>{i18n._('sys.device_information.power_middle')}</p>
                    </TooltipContent>
                </Tooltip>
            )}
            {powerStatus === 'low' && (
                <Tooltip>
                    <TooltipTrigger>
                        <div className="w-5 h-5">
                            <Link to="/system-settings">
                                <SvgIcon icon="battery_low" />
                            </Link>
                        </div>
                    </TooltipTrigger>
                    <TooltipContent className="absolute max-w-80 text-pretty">
                        <p>{i18n._('sys.device_information.power_low')}</p>
                    </TooltipContent>
                </Tooltip>
            )}
            {powerStatus === 'power' && (
                <Tooltip>
                    <TooltipTrigger>
                        <div className="w-5 h-5">
                            <Link to="/device-information">
                                <SvgIcon icon="power" className="w-5 h-5" />
                            </Link>
                        </div>
                    </TooltipTrigger>
                    <TooltipContent className="absolute max-w-80 text-pretty">
                        <p>{i18n._('sys.header.long_power_supply')}</p>
                    </TooltipContent>
                </Tooltip>
            )}
        </div>
    )
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
    const handleBlur = () => {
        if (deviceName !== deviceInfo?.device_name) {
            setShowPopover(true)
        } else {
            setIsEdit(false)
        }
    }
    const handleFocus = () => {
        setIsEdit(true)
        setTimeout(() => {
            inputRef.current?.focus()
        }, 10)
    }
    const handleCancel = () => {
        setDeviceName(deviceInfo?.device_name ?? '')
        setShowPopover(false)
        setIsEdit(false)
    }
    const handleConfirm = async () => {
        try {
            setShowPopover(false)
            setIsEdit(false)
            if (!validateDeviceName()) {
                setDeviceName(deviceInfo?.device_name ?? '')
                toast.error(i18n._(`sys.header.${errorsRef?.current?.message}`))
                return
            }
            await setDeviceNameReq({ device_name: deviceName })
            await getDeviceInfo()
            toast.success(i18n._('sys.header.device_name_updated'))
        } catch (error) {
            console.error('handleConfirm', error)
        }
    }
    return (
        <div className="items-center space-x-3 md:flex">
            <div className="flex">
                {!isMobile && (
                    <>
                        <div className="w-8 h-6 mr-2">
                            <img src={deviceImage} alt="camthink_Vision_AI_camera" />
                        </div>
                        <div className="flex items-center space-x-2 mr-2">
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
                                          variant="outline"
                                          type="text"
                                          value={deviceName}
                                          onChange={(e) => setDeviceName((e.target as HTMLInputElement)?.value ?? '')}
                                          onBlur={handleBlur}
                                          onKeyDown={handleKeyDown}
                                          onFocus={() => {
                                                setShowPopover(false)
                                            }}
                                        />
                                    </PopoverTrigger>
                                    <PopoverContent className="w-full p-3">
                                        <div className="space-y-2">
                                            <p className="text-sm font-medium">{i18n._('sys.device_information.is_edit_device_name')}</p>
                                            {/* <p className="text-xs text-gray-500">{i18n._('common.confirm_change_desc')}</p> */}
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
                                                >
                                                    {i18n._('common.confirm')}
                                                </Button>
                                            </div>
                                        </div>
                                    </PopoverContent>
                                </Popover>
                            ) : (
                                <>
                                    <span className="text-sm font-medium text-gray-700">
                                        {deviceInfo?.device_name ?? '--'}
                                    </span>
                                    <Button variant="ghost" onClick={handleFocus} className="w-4 h-4 flex items-center justify-center">
                                        <SvgIcon icon="pen" />
                                    </Button>
                                </>
                            )}

                        </div>
                    </>
                )}
                {!isMobile && <div className="w-[1px] h-8 bg-gray-200 mx-3"></div>}
                <div className="flex items-center space-x-1">
                    {communicationTypeSlot()}
                </div>
            </div>
        </div>
    )
}