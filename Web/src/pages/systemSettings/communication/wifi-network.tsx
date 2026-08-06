import { useState, useEffect } from 'react';
import { useLingui } from '@lingui/react';
import SvgIcon from '@/components/svg-icon';
import { Label } from '@/components/ui/label';
import { Separator } from '@/components/ui/separator'
import { Button } from '@/components/ui/button';
import CommunicationSkeleton from './skeleton';
import { Dialog, DialogContent, DialogHeader, DialogTitle, DialogClose, DialogFooter } from '@/components/dialog';
import {
    Popover,
    PopoverContent,
    PopoverTrigger,
} from "@/components/ui/popover"
import systemSettings from '@/services/api/systemSettings';
import { useIsMobile } from '@/hooks/use-mobile';
import { Input } from '@/components/ui/input';
import WifiReloadMask from '@/components/wifi-reload-mask';
import { sleep } from '@/utils';
import { useCommunicationData } from '@/store/communicationData';

type WifiData = {
    ssid: string;
    bssid: string;
    rssi: number;
    channel: number;
    security: string;
    connected: boolean;
    is_known: boolean;
    last_connected_time: number;
}

// When refreshing/reconnecting, poll the STA interface instead of failing fast on a
// transient 500. Only surface "network disconnected" after this timeout elapses.
const POLL_TIMEOUT = 20000;
const POLL_INTERVAL = 1000;
const POLL_REQUEST_TIMEOUT = 2000;

export default function WifiNetworkPage() {
    const { i18n } = useLingui();
    const isMobile = useIsMobile();
    const { getNetworkSTAReq, scanWifi, setWifi, disconnectWifi, deleteWifi } = systemSettings;
    const { getCommunicationData } = useCommunicationData();
    // const wifiDataList = wifiData.data.scan_results.known_networks;
    const [isLoading, setIsLoading] = useState(true);
    const [currentWifiData, setCurrentWifiData] = useState<WifiData | null>(null);
    const [knownWifiDataList, setKnownWifiDataList] = useState<WifiData[]>([]);
    const [otherWifiDataList, setOtherWifiDataList] = useState<WifiData[]>([]);
    const [isConnectWifiDialogOpen, setIsConnectWifiDialogOpen] = useState(false);
    const [isPasswordVisible, setIsPasswordVisible] = useState(false);
    const [wifiPassword, setWifiPassword] = useState('');
    const [wifiInfo, setWifiInfo] = useState<WifiData | null>(null);
    const [showWifiReloadMask, setShowWifiReloadMask] = useState(false);
    const [loadingText, setLoadingText] = useState('');
    const [isReloading, setIsReloading] = useState(false);
    const [isErrorWifiPassword, setIsErrorWifiPassword] = useState(false);
    const getNetworkSTA = async () => {
        try {
            setIsLoading(true);
            const res = await getNetworkSTAReq();
            setCurrentWifiData(res.data);
            setKnownWifiDataList(res.data.scan_results.known_networks);
            setOtherWifiDataList(res.data.scan_results.unknown_networks);
        } catch (error) {
            console.error(error);
        } finally {
            setIsLoading(false);
        }
    }
    useEffect(() => {
        getNetworkSTA();
    }, []);
    const isValidateWifiPassword = (password: string, minLength: number, maxLength: number) => {
        const allowedPattern = /^[a-zA-Z0-9!@#$%^&*()_+\-=[\]{}|;':",./<>?`~]+$/;
        if (!allowedPattern.test(password)) {
            return false;
        }
        if (password.length < minLength || password.length > maxLength) {
            return false;
        }
        return true;
    }

    const connectWifiDialog = () => (
        <Dialog
          open={isConnectWifiDialogOpen}
          onOpenChange={(open) => {
                setIsConnectWifiDialogOpen(open);
                if (!open) setWifiInfo(null);
            }}
        >

            <DialogContent className="mx-8" showCloseButton={false}>
                <DialogClose asChild onClick={() => handleCancelConnectWifi()}>
                    <Button
                      variant="ghost"
                      onClick={() => handleCancelConnectWifi()}
                      className="absolute right-4 top-4 h-8 w-8 p-0 rounded-full opacity-70 hover:opacity-100 transition-opacity z-10"
                    >
                        <SvgIcon icon="close" className="w-4 h-4" />
                    </Button>
                </DialogClose>
                <DialogHeader>
                    <DialogTitle>{i18n._('sys.system_management.wifi-dialog-title')}</DialogTitle>
                </DialogHeader>
                <div className="my-4">
                    <div className="mb-2">
                        <p>{i18n._('sys.system_management.join_network')} {wifiInfo?.ssid ?? ''}</p>
                    </div>
                    <div className="relative">
                        <Input
                          placeholder={i18n._('sys.system_management.wifi-dialog-placeholder')}
                          type={isPasswordVisible ? 'text' : 'password'}
                          value={wifiPassword}
                          onChange={(e) => setWifiPassword((e.target as HTMLInputElement).value)}
                        />
                        <button
                          type="button"
                          onClick={handleWifiPasswordVisible}
                          className="absolute top-1/2 -translate-y-1/2 right-0 flex items-center bg-transparent pr-4 border-none cursor-pointer disabled:opacity-50"
                        >
                            {isPasswordVisible ? (
                                <SvgIcon className="w-4 h-4" icon="visibility" />
                            ) : (
                                <SvgIcon className="w-4 h-4" icon="visibility_off" />
                            )}
                        </button>
                    </div>
                    {isErrorWifiPassword && (
                        <p className="text-sm text-red-500 mt-1">{i18n._('sys.system_management.password_error')}</p>
                    )}
                </div>

                <DialogFooter>
                    <Button size="sm" className="w-1/2 md:w-auto" variant="outline" onClick={() => handleCancelConnectWifi()}>{i18n._('common.cancel')}</Button>
                    <Button size="sm" className="w-1/2 md:w-auto" variant="primary" onClick={() => handleConnectWifi(wifiInfo as WifiData, 'unknown')}>{i18n._('common.confirm')}</Button>
                </DialogFooter>
            </DialogContent>

        </Dialog>
    )
    const handleCancelConnectWifi = () => {
        setIsConnectWifiDialogOpen(false);
        setWifiPassword('');
        setIsErrorWifiPassword(false);
    }
    const handleWifiPasswordVisible = (e: MouseEvent) => {
        e.preventDefault();
        setIsPasswordVisible(!isPasswordVisible);
    }

    const openConnectWifiDialog = async (data: WifiData, type: 'known' | 'unknown') => {
        setWifiInfo(data);
        if (data.security !== 'open' && type === 'unknown') {
            setIsConnectWifiDialogOpen(true);
            return;
        }
        // Pass data parameter directly to avoid dependency on async state updates
        handleConnectWifi(data, type)
    }

    const handleConnectWifi = async (wifiData: WifiData, type: 'known' | 'unknown') => {
        if (!wifiData) return;
        if (!isValidateWifiPassword(wifiPassword, 8, 64) && wifiData.security !== 'open' && type === 'unknown') {
            setIsErrorWifiPassword(true);
            return;
        }
        try {
            const setWifiFn = () => new Promise((resolve, reject) => {
                setWifi({
                    ssid: wifiData.ssid || '',
                    bssid: wifiData.bssid,
                    password: wifiPassword,
                    interface: 'wl'
                }).then(resolve).catch(reject);
            })
            await reloadMask(setWifiFn, i18n._('sys.system_management.connecting_network'));
        } catch (error) {
            console.error('handleConnectWifi', error);
        } finally {
            setIsConnectWifiDialogOpen(false);
            setWifiPassword('');
        }
    }
    const handleForgetWifi = async () => {
        try {
            await disconnectWifi({ interface: 'wl' });
            getNetworkSTA();      
            getCommunicationData();
        } catch (error) {
            console.error('handleForgetWifi', error);
        }
    }

    useEffect(() => {
        if (!isValidateWifiPassword(wifiPassword, 8, 64) && isErrorWifiPassword) {
            setIsErrorWifiPassword(true);
        } else {
            setIsErrorWifiPassword(false);
        }
    }, [wifiPassword]);
    const handleScanWifi = async () => {
        const waitScanWifi = async () => {
            await scanWifi();
            await sleep(3000);
        };
        reloadMask(waitScanWifi, i18n._('sys.system_management.scanning_network'));
    }
    const handleDeleteWifi = async (wifiData: WifiData) => {
        try {
            await deleteWifi({ ssid: wifiData.ssid, bssid: wifiData.bssid });
            getNetworkSTA();
        } catch (error) {
            console.error('handleDeleteWifi', error);
        }
    }

    // Raw STA request used while reconnecting. It throws on failure so the poll loop
    // below can retry, and suppresses error toasts so a reconnecting device does not
    // spam "500" notifications on every attempt.
    const pollNetworkSTA = async (timeoutMs: number) => {
        const controller = new AbortController();
        const timeoutId = setTimeout(() => controller.abort(), timeoutMs);
        try {
            const res = await getNetworkSTAReq({ skipErrorToast: true, signal: controller.signal });
            setCurrentWifiData(res.data);
            setKnownWifiDataList(res.data.scan_results.known_networks);
            setOtherWifiDataList(res.data.scan_results.unknown_networks);
            return res;
        } finally {
            clearTimeout(timeoutId);
        }
    }

    const reloadMask = async (fetchFn: () => Promise<any>, _loadingText: string) => {
        setLoadingText(_loadingText);
        setIsReloading(true);
        setShowWifiReloadMask(true);
        try {
            await fetchFn();
        } catch (error) {
            console.error('reloadMask fetchFn', error);
        }
        // Poll the STA interface until it responds again or the timeout elapses.
        // Only keep the "network disconnected" mask visible if polling truly times out.
        let reconnected = false;
        const start = Date.now();
        /* eslint-disable no-await-in-loop -- sequential polling is intentional */
        while (Date.now() - start < POLL_TIMEOUT) {
            try {
                await pollNetworkSTA(POLL_REQUEST_TIMEOUT);
                reconnected = true;
                break;
            } catch {
                await sleep(POLL_INTERVAL);
            }
        }
        /* eslint-enable no-await-in-loop */
        if (reconnected) {
            setShowWifiReloadMask(false);
        }
        setIsReloading(false);
    }
    return (
        <div className="mt-2">
            {isLoading && <CommunicationSkeleton />}
            {!isLoading && (
                <div className="relative">
                    <Button
                      variant="ghost"
                      className="absolute -top-2 right-0"
                      onClick={handleScanWifi}
                      title={i18n._('sys.system_management.refresh')}
                    >
                        <SvgIcon icon="reload2" className="w-6 h-6" />
                    </Button>

                    {!currentWifiData?.connected && knownWifiDataList.length === 0 && otherWifiDataList.length === 0
                        && (
                            <div className="h-[400px] flex flex-col items-center justify-center ">
                                <SvgIcon icon="empty" className="w-40 h-40" />
                                <p className="text-sm text-text-secondary">{i18n._('sys.system_management.no_network')}</p>
                            </div>

                        )}
                    {currentWifiData?.connected && (
                        <>
                            <p className="text-sm font-bold mb-2">{i18n._('sys.system_management.communication_mode')}</p>
                            <div className="flex flex-col gap-2 bg-gray-100 p-4 rounded-lg">
                                <div className="flex justify-between">
                                    <div className="flex items-center gap-2">
                                        <div className="w-6 h-6 bg-primary rounded-md flex items-center justify-center">
                                            <SvgIcon icon="wifi3" className="w-4 h-4" />
                                        </div>
                                        <p>{i18n._('sys.system_management.wifi')}</p>
                                    </div>
                                </div>
                                <Separator />
                                <div className="flex justify-between">
                                    <Label>{currentWifiData?.ssid}</Label>
                                    <div className="flex items-center gap-1">
                                        <div className="flex items-center space-x-1 mr-1">
                                            <SvgIcon icon="check" className="w-4 h-4" />
                                            <p className="text-sm text-green-500">{i18n._('common.connected')}</p>
                                        </div>
                                        <SvgIcon icon={currentWifiData.rssi >= -55 ? 'wifi' : currentWifiData.rssi >= -75 ? 'wifi_middle' : 'wifi_low'} className="w-4 h-4 text-[#272E3B]" />
                                        <Popover>
                                            <PopoverTrigger onClick={(e: any) => e.stopPropagation()}>
                                                <SvgIcon icon="more" className="w-4 h-4 text-white cursor-pointer" />
                                            </PopoverTrigger>
                                            <PopoverContent className="w-auto p-0">
                                                <div className="flex flex-col gap-2 mx-2 py-2">
                                                    <div className="text-sm px-4 py-1 cursor-pointer hover:bg-gray-100 hover:rounded-md" onClick={() => handleForgetWifi()}>{i18n._('sys.system_management.disconnect')}</div>
                                                    <Separator />
                                                    <div className="text-sm px-4 py-1 cursor-pointer hover:bg-gray-100 hover:rounded-md" onClick={() => handleDeleteWifi(currentWifiData)}>{i18n._('sys.system_management.forget')}</div>
                                                </div>
                                            </PopoverContent>
                                        </Popover>
                                    </div>
                                </div>
                            </div>
                        </>
                    )}
                    {knownWifiDataList.length > 0 && (
                        <div className="mt-4">
                            <p className="text-sm font-bold mb-2">{i18n._('sys.system_management.known_network')}</p>
                            <div className="flex flex-col bg-gray-100 px-4 rounded-lg">
                                {knownWifiDataList.map((item, index) => (
                                    <div onClick={() => { if (isMobile) openConnectWifiDialog(item, 'known'); }} key={`${item.ssid}-${item.bssid}-${index}`} className="group">
                                        <div className="flex justify-between py-2">
                                            <Label>{item.ssid}</Label>
                                            <div className="flex items-center gap-1">
                                                <Button size="sm" variant="outline" onClick={() => openConnectWifiDialog(item, 'known')} className="mr-2 opacity-0 group-hover:opacity-100 transition-opacity">{i18n._('common.connect')}</Button>
                                                {/* {item.security !== 'open' && <SvgIcon icon="lock" className="w-4 h-4" />} */}
                                                <SvgIcon icon={item.rssi >= -55 ? 'wifi' : item.rssi >= -75 ? 'wifi_middle' : 'wifi_low'} className="w-4 h-4 text-[#272E3B]" />
                                                <Popover>
                                                    <PopoverTrigger onClick={(e: any) => e.stopPropagation()}>
                                                        <SvgIcon icon="more" className="w-4 h-4 text-white cursor-pointer" />
                                                    </PopoverTrigger>
                                                    <PopoverContent className="w-auto p-0">
                                                        <div className="flex flex-col gap-2 mx-2 py-2">
                                                            <div className="text-sm px-4 py-1 cursor-pointer hover:bg-gray-100 hover:rounded-md" onClick={() => handleDeleteWifi(item)}>{i18n._('sys.system_management.forget')}</div>
                                                        </div>
                                                    </PopoverContent>
                                                </Popover>
                                            </div>
                                        </div>
                                        {index !== knownWifiDataList.length - 1 && <Separator />}
                                    </div>
                                ))}
                            </div>
                        </div>
                    )}
                    {otherWifiDataList.length > 0 && (
                        <div className="mt-4">
                            <p className="text-sm font-bold mb-2">{i18n._('sys.system_management.other_network')}</p>
                            <div className="flex flex-col bg-gray-100 px-4 rounded-lg">
                                {otherWifiDataList.map((item, index) => (
                                    <div onClick={() => { if (isMobile) openConnectWifiDialog(item, 'unknown'); }} key={`${item.ssid}-${item.bssid}-${index}`} className="group">
                                        <div className="flex justify-between py-2">
                                            <Label className="text-sm">{item.ssid}</Label>
                                            <div className="flex items-center gap-1">
                                                <Button size="sm" variant="outline" onClick={() => openConnectWifiDialog(item, 'unknown')} className="mr-2 opacity-0 group-hover:opacity-100 transition-opacity">{i18n._('common.connect')}</Button>
                                                {item.security !== 'open' && <SvgIcon icon="lock" className="w-4 h-4 mr-1" />}
                                                <SvgIcon icon={item.rssi >= -55 ? 'wifi' : item.rssi >= -75 ? 'wifi_middle' : 'wifi_low'} className="w-4 h-4 text-[#272E3B]" />
                                            </div>
                                        </div>
                                        {index !== otherWifiDataList.length - 1 && <Separator />}
                                    </div>
                                ))}
                            </div>
                        </div>
                    )}
                </div>
            )}
            {connectWifiDialog()}
            {/* {refreshWifiDialog()} */}
            {showWifiReloadMask && <WifiReloadMask loadingText={loadingText} isLoading={isReloading} />}
        </div>
    )
}
