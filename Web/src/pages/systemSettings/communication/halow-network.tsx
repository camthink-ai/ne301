import { useState, useEffect, useCallback } from 'react';
import { useLingui } from '@lingui/react';
import SvgIcon from '@/components/svg-icon';
import { Label } from '@/components/ui/label';
import { Separator } from '@/components/ui/separator';
import { Button } from '@/components/ui/button';
import CommunicationSkeleton from './skeleton';
import { Dialog, DialogContent, DialogHeader, DialogTitle, DialogClose, DialogFooter } from '@/components/dialog';
import {
    Popover,
    PopoverContent,
    PopoverTrigger,
} from '@/components/ui/popover';
import { Select, SelectTrigger, SelectValue, SelectContent, SelectItem } from '@/components/ui/select';
import { useIsMobile } from '@/hooks/use-mobile';
import { Input } from '@/components/ui/input';
import WifiReloadMask from '@/components/wifi-reload-mask';
import { sleep, retryFetch } from '@/utils';
import { isValidPoeIp } from '@/utils/verify';
import { toast } from 'sonner';
import systemSettings from '@/services/api/systemSettings';
import { useLanguage } from '@/hooks/useLanguageProvider';
import { getHalowRegionLabel, normalizeHalowRegionCode } from './halow-region';

type HalowData = {
    ssid: string;
    bssid: string;
    rssi: number;
    channel: number;
    security: string;
    connected: boolean;
    is_known: boolean;
    quick_connect?: boolean;
    last_connected_time: number;
};

type HalowIpConfig = {
    ip_mode: 'dhcp' | 'static';
    ip_address: string;
    netmask: string;
    gateway: string;
};

type HalowRadioConfig = {
    tx_power_dbm: number;
    scan_dwell_ms: number;
    rate_mcs: number;
    rate_bw_mhz: number;
    rate_gi: number;
};

type HalowRadioLimits = {
    tx_power_dbm: { min: number; max: number };
    scan_dwell_ms: { min: number; max: number; default: number };
};

const RADIO_AUTO = 'auto';

const HALOW_RADIO_LIMITS_DEFAULT: HalowRadioLimits = {
    tx_power_dbm: { min: 0, max: 30 },
    scan_dwell_ms: { min: 15, max: 300, default: 30 },
};

/** HaLow save/connect may down/up the netif; allow long mask polling like comm switch. */
const HALOW_MASK_POLL_MS = 3000;
const HALOW_MASK_RETRIES = 10;

const filterOtherNetworks = (list: HalowData[], connected: HalowData | null) => {
    if (!connected?.connected) return list;
    return list.filter(
        (item) => !(item.ssid === connected.ssid && (!connected.bssid || item.bssid === connected.bssid))
    );
};

export default function HalowNetworkPage() {
    const { i18n } = useLingui();
    const { locale } = useLanguage();
    const isMobile = useIsMobile();
    const {
        getHalowStaReq,
        setHalowRegionReq,
        scanHalow,
        setHalow,
        disconnectHalow,
        deleteHalow,
        getHalowIpReq,
        setHalowIpReq,
        getHalowRadioReq,
        setHalowRadioReq,
    } = systemSettings;

    const [isLoading, setIsLoading] = useState(true);
    const [region, setRegion] = useState('us');
    const [supportedRegions, setSupportedRegions] = useState<string[]>(['us', 'eu', 'cn', 'jp', 'au']);
    const [currentHalowData, setCurrentHalowData] = useState<HalowData | null>(null);
    const [otherHalowDataList, setOtherHalowDataList] = useState<HalowData[]>([]);
    const [isConnectDialogOpen, setIsConnectDialogOpen] = useState(false);
    const [isPasswordVisible, setIsPasswordVisible] = useState(false);
    const [halowPassword, setHalowPassword] = useState('');
    const [halowInfo, setHalowInfo] = useState<HalowData | null>(null);
    const [showReloadMask, setShowReloadMask] = useState(false);
    const [loadingText, setLoadingText] = useState('');
    const [isReloading, setIsReloading] = useState(false);
    const [isErrorPassword, setIsErrorPassword] = useState(false);
    const [isForgetDialogOpen, setIsForgetDialogOpen] = useState(false);
    const [forgetLoading, setForgetLoading] = useState(false);
    const [ipSaving, setIpSaving] = useState(false);
    const [ipRefreshing, setIpRefreshing] = useState(false);
    const [ipConfig, setIpConfig] = useState<HalowIpConfig>({
        ip_mode: 'dhcp',
        ip_address: '',
        netmask: '',
        gateway: '',
    });
    const [liveIp, setLiveIp] = useState<HalowIpConfig>({
        ip_mode: 'dhcp',
        ip_address: '',
        netmask: '',
        gateway: '',
    });
    const [radioConfig, setRadioConfig] = useState<HalowRadioConfig>({
        tx_power_dbm: 0,
        scan_dwell_ms: 30,
        rate_mcs: -1,
        rate_bw_mhz: -1,
        rate_gi: -1,
    });
    const [radioSaving, setRadioSaving] = useState(false);
    const [radioRefreshing, setRadioRefreshing] = useState(false);
    const [radioLimits, setRadioLimits] = useState<HalowRadioLimits>(HALOW_RADIO_LIMITS_DEFAULT);

    const applyStaResponse = useCallback((data: any) => {
        if (data.region) {
            setRegion(normalizeHalowRegionCode(String(data.region)));
        }
        if (Array.isArray(data.supported_regions) && data.supported_regions.length > 0) {
            setSupportedRegions(data.supported_regions.map((r: string) => normalizeHalowRegionCode(String(r))));
        }

        const connected: HalowData | null = data.connected
            ? {
                ssid: data.ssid ?? '',
                bssid: data.bssid ?? '',
                rssi: data.rssi ?? 0,
                channel: data.channel ?? 0,
                security: data.security ?? 'open',
                connected: true,
                is_known: true,
                last_connected_time: data.last_connected_time ?? 0,
            }
            : null;

        setCurrentHalowData(connected);

        if (data.connected) {
            setLiveIp({
                ip_mode: (data.ip_mode === 'static' ? 'static' : 'dhcp'),
                ip_address: data.ip_address ?? '',
                netmask: data.netmask ?? '',
                gateway: data.gateway ?? '',
            });
        }

        const unknown: HalowData[] = data.scan_results?.unknown_networks ?? [];
        setOtherHalowDataList(filterOtherNetworks(unknown, connected));
    }, []);

    const getHalowSta = useCallback(async (showSkeleton = true) => {
        try {
            if (showSkeleton) setIsLoading(true);
            const res = await getHalowStaReq();
            applyStaResponse(res.data);
            return res.data;
        } catch (error) {
            // eslint-disable-next-line no-console
            console.error(error);
            return null;
        } finally {
            if (showSkeleton) setIsLoading(false);
        }
    }, [applyStaResponse, getHalowStaReq]);

    const isHalowScanInProgress = (data: any) => {
        if (!data) return false;
        if (typeof data.scan_in_progress === 'boolean') {
            return data.scan_in_progress;
        }
        return data.scan_results?.scan_in_progress === true;
    };

    const pollHalowScanComplete = useCallback(async (maxWaitMs = 30000, intervalMs = 500) => {
        const deadline = Date.now() + maxWaitMs;
        const pollOnce = async (): Promise<boolean> => {
            if (Date.now() >= deadline) {
                await getHalowSta(false);
                return false;
            }
            const res = await getHalowStaReq();
            applyStaResponse(res.data);
            if (!isHalowScanInProgress(res.data)) {
                return true;
            }
            await sleep(intervalMs);
            return pollOnce();
        };
        return pollOnce();
    }, [applyStaResponse, getHalowStaReq]);

    const loadHalowRadio = useCallback(async (showRefreshing = false) => {
        try {
            if (showRefreshing) setRadioRefreshing(true);
            const res = await getHalowRadioReq();
            const data = res.data ?? {};
            const limits = data.limits ?? {};
            setRadioLimits({
                tx_power_dbm: {
                    min: Number(limits.tx_power_dbm?.min ?? HALOW_RADIO_LIMITS_DEFAULT.tx_power_dbm.min),
                    max: Number(limits.tx_power_dbm?.max ?? HALOW_RADIO_LIMITS_DEFAULT.tx_power_dbm.max),
                },
                scan_dwell_ms: {
                    min: Number(limits.scan_dwell_ms?.min ?? HALOW_RADIO_LIMITS_DEFAULT.scan_dwell_ms.min),
                    max: Number(limits.scan_dwell_ms?.max ?? HALOW_RADIO_LIMITS_DEFAULT.scan_dwell_ms.max),
                    default: Number(limits.scan_dwell_ms?.default ?? HALOW_RADIO_LIMITS_DEFAULT.scan_dwell_ms.default),
                },
            });
            setRadioConfig({
                tx_power_dbm: Number(data.tx_power_dbm ?? 0),
                scan_dwell_ms: Number(data.scan_dwell_ms ?? limits.scan_dwell_ms?.default ?? 30),
                rate_mcs: Number(data.rate_mcs ?? -1),
                rate_bw_mhz: Number(data.rate_bw_mhz ?? -1),
                rate_gi: Number(data.rate_gi ?? -1),
            });
        } catch (error) {
            // eslint-disable-next-line no-console
            console.error(error);
        } finally {
            if (showRefreshing) setRadioRefreshing(false);
        }
    }, [getHalowRadioReq]);

    const handleRefreshRadio = () => loadHalowRadio(true);

    const loadHalowIp = useCallback(async (showRefreshing = false) => {
        try {
            if (showRefreshing) setIpRefreshing(true);
            const res = await getHalowIpReq();
            const mode = res.data?.ip_mode === 'static' ? 'static' : 'dhcp';
            const next: HalowIpConfig = {
                ip_mode: mode,
                ip_address: res.data?.ip_address ?? '',
                netmask: res.data?.netmask ?? '',
                gateway: res.data?.gateway ?? '',
            };
            setIpConfig(next);
            if (mode === 'dhcp') {
                setLiveIp(next);
            }
        } catch (error) {
            // eslint-disable-next-line no-console
            console.error(error);
        } finally {
            if (showRefreshing) setIpRefreshing(false);
        }
    }, [getHalowIpReq]);

    const handleRefreshIpConfig = () => loadHalowIp(true);

    useEffect(() => {
        getHalowSta();
        loadHalowRadio();
        loadHalowIp();
    }, [getHalowSta, loadHalowRadio, loadHalowIp]);

    const reloadMask = async (
        fetchFn: () => Promise<any>,
        loadingTime: number,
        loadCount: number,
        maskText: string
    ) => {
        setLoadingText(maskText);
        setIsReloading(true);
        setShowReloadMask(true);
        try {
            await fetchFn();
            await retryFetch(() => getHalowSta(false), loadingTime, loadCount);
        } finally {
            setShowReloadMask(false);
            setIsReloading(false);
        }
    };

    const isValidatePassword = (password: string, minLength: number, maxLength: number) => {
        const allowedPattern = /^[a-zA-Z0-9!@#$%^&*()_+\-=[\]{}|;':",./<>?`~]+$/;
        if (!allowedPattern.test(password)) return false;
        if (password.length < minLength || password.length > maxLength) return false;
        return true;
    };

    const handleCancelConnect = () => {
        setIsConnectDialogOpen(false);
        setHalowPassword('');
        setIsErrorPassword(false);
        setHalowInfo(null);
    };

    const handlePasswordVisible = (e: MouseEvent) => {
        e.preventDefault();
        setIsPasswordVisible(!isPasswordVisible);
    };

    const openConnectDialog = (data: HalowData) => {
        setHalowInfo(data);
        if (data.security !== 'open') {
            setIsConnectDialogOpen(true);
            return;
        }
        handleConnect(data);
    };

    const handleConnect = async (data: HalowData) => {
        if (!data) return;
        if (!isValidatePassword(halowPassword, 8, 64) && data.security !== 'open') {
            setIsErrorPassword(true);
            return;
        }
        const connectFn = () => setHalow({
            interface: 'halow',
            ssid: data.ssid,
            bssid: data.bssid,
            password: halowPassword,
            region,
        });
        try {
            await reloadMask(connectFn, 3000, 3, i18n._('sys.system_management.connecting_network'));
        } catch (error) {
            // eslint-disable-next-line no-console
            console.error(error);
        } finally {
            setIsConnectDialogOpen(false);
            setHalowPassword('');
            setHalowInfo(null);
        }
    };

    const handleQuickConnect = async (data: HalowData) => {
        const connectFn = () => setHalow({
            interface: 'halow',
            ssid: data.ssid,
            bssid: data.bssid,
            password: '',
            region,
            use_saved_password: true,
        });
        try {
            await reloadMask(connectFn, 3000, 3, i18n._('sys.system_management.connecting_network'));
        } catch (error) {
            // eslint-disable-next-line no-console
            console.error(error);
        }
    };

    const handleDisconnect = async () => {
        try {
            await disconnectHalow({ interface: 'halow' });
            await handleScan();
        } catch (error) {
            // eslint-disable-next-line no-console
            console.error(error);
        }
    };

    const validateIpConfig = () => {
        if (ipConfig.ip_mode !== 'static') {
            return true;
        }
        if (!isValidPoeIp(ipConfig.ip_address) || !isValidPoeIp(ipConfig.netmask) || !isValidPoeIp(ipConfig.gateway)) {
            toast.error(i18n._('errors.poe.parameter_configuration_error'));
            return false;
        }
        return true;
    };

    const handleSaveIpConfig = async () => {
        if (!validateIpConfig()) {
            return;
        }
        const saveFn = () => setHalowIpReq(
            ipConfig.ip_mode === 'dhcp'
                ? { ip_mode: 'dhcp' }
                : {
                    ip_mode: 'static',
                    ip_address: ipConfig.ip_address,
                    netmask: ipConfig.netmask,
                    gateway: ipConfig.gateway,
                }
        );
        try {
            setIpSaving(true);
            await reloadMask(
                saveFn,
                HALOW_MASK_POLL_MS,
                HALOW_MASK_RETRIES,
                i18n._('sys.system_management.saving_network_config')
            );
            toast.success(i18n._('sys.system_management.save_success'));
            await loadHalowIp();
        } catch (error) {
            // eslint-disable-next-line no-console
            console.error(error);
        } finally {
            setIpSaving(false);
        }
    };

    const handleForgetNetwork = async () => {
        try {
            setForgetLoading(true);
            await deleteHalow();
            setIsForgetDialogOpen(false);
            setCurrentHalowData(null);
            await getHalowSta(false);
            await handleScan();
        } catch (error) {
            // eslint-disable-next-line no-console
            console.error(error);
        } finally {
            setForgetLoading(false);
        }
    };

    const handleScan = async () => {
        setLoadingText(i18n._('sys.system_management.scanning_network'));
        setIsReloading(true);
        setShowReloadMask(true);
        try {
            await scanHalow();
            await pollHalowScanComplete();
        } catch (error) {
            // eslint-disable-next-line no-console
            console.error(error);
        } finally {
            setShowReloadMask(false);
            setIsReloading(false);
        }
    };

    const handleRegionChange = async (value: string) => {
        if (currentHalowData?.connected) return;
        try {
            setRegion(value);
            await setHalowRegionReq({ region: value });
            await loadHalowRadio();
            await handleScan();
        } catch (error) {
            // eslint-disable-next-line no-console
            console.error(error);
        }
    };

    const handleSaveRadioConfig = async () => {
        if (radioConfig.scan_dwell_ms < radioLimits.scan_dwell_ms.min
            || radioConfig.scan_dwell_ms > radioLimits.scan_dwell_ms.max) {
            toast.error(i18n._('errors.poe.parameter_configuration_error'));
            return;
        }
        if (radioConfig.tx_power_dbm < radioLimits.tx_power_dbm.min
            || radioConfig.tx_power_dbm > radioLimits.tx_power_dbm.max) {
            toast.error(i18n._('errors.poe.parameter_configuration_error'));
            return;
        }
        try {
            setRadioSaving(true);
            const saveFn = () => setHalowRadioReq({
                tx_power_dbm: radioConfig.tx_power_dbm,
                scan_dwell_ms: radioConfig.scan_dwell_ms,
                rate_mcs: radioConfig.rate_mcs,
                rate_bw_mhz: radioConfig.rate_bw_mhz,
                rate_gi: radioConfig.rate_gi,
            });
            await reloadMask(
                saveFn,
                HALOW_MASK_POLL_MS,
                HALOW_MASK_RETRIES,
                i18n._('sys.system_management.saving_radio_config')
            );
            toast.success(i18n._('sys.system_management.save_success'));
            await loadHalowRadio();
        } catch (error) {
            // eslint-disable-next-line no-console
            console.error(error);
        } finally {
            setRadioSaving(false);
        }
    };

    useEffect(() => {
        if (!isValidatePassword(halowPassword, 8, 64) && isErrorPassword) {
            setIsErrorPassword(true);
        } else {
            setIsErrorPassword(false);
        }
    }, [halowPassword, isErrorPassword]);

    const displayIp = ipConfig.ip_mode === 'dhcp' ? liveIp : ipConfig;

    const networkConfigSection = () => (
        <div className="mt-4">
            <div className="relative mb-2">
                <p className="text-sm font-bold">{i18n._('sys.system_management.halow_network_config')}</p>
                <Button
                  variant="ghost"
                  className="absolute -top-2 right-0"
                  onClick={handleRefreshIpConfig}
                  disabled={ipRefreshing}
                  title={i18n._('sys.system_management.halow_refresh_network_config')}
                >
                    <SvgIcon icon="reload2" className={`w-6 h-6 ${ipRefreshing ? 'animate-spin' : ''}`} />
                </Button>
            </div>
            <div className="flex flex-col bg-gray-100 px-4 py-2 rounded-lg gap-1">
                <div className="flex justify-between items-center gap-2 py-2">
                    <Label className="text-sm shrink-0">{i18n._('sys.system_management.halow_ip_mode')}</Label>
                    <Select
                      value={ipConfig.ip_mode}
                      onValueChange={(value: 'dhcp' | 'static') => {
                            if (value === 'dhcp') {
                                setIpConfig({
                                    ip_mode: 'dhcp',
                                    ip_address: liveIp.ip_address,
                                    netmask: liveIp.netmask,
                                    gateway: liveIp.gateway,
                                });
                            } else {
                                setIpConfig((prev) => ({ ...prev, ip_mode: 'static' }));
                            }
                        }}
                    >
                        <SelectTrigger className="w-[140px] bg-white">
                            <SelectValue />
                        </SelectTrigger>
                        <SelectContent>
                            <SelectItem value="dhcp">{i18n._('sys.system_management.dhcp')}</SelectItem>
                            <SelectItem value="static">{i18n._('sys.system_management.static')}</SelectItem>
                        </SelectContent>
                    </Select>
                </div>
                <Separator />
                <div className="flex justify-between items-center gap-2 py-2">
                    <Label className="text-sm shrink-0">{i18n._('sys.system_management.ip_address')}</Label>
                    <Input
                      type="text"
                      className="w-[160px] text-right bg-white"
                      readOnly={ipConfig.ip_mode === 'dhcp'}
                      value={displayIp.ip_address}
                      onChange={(e) => setIpConfig({ ...ipConfig, ip_address: (e.target as HTMLInputElement).value })}
                      placeholder={i18n._('sys.system_management.ip_address')}
                    />
                </div>
                <Separator />
                <div className="flex justify-between items-center gap-2 py-2">
                    <Label className="text-sm shrink-0">{i18n._('sys.system_management.netmask')}</Label>
                    <Input
                      type="text"
                      className="w-[160px] text-right bg-white"
                      readOnly={ipConfig.ip_mode === 'dhcp'}
                      value={displayIp.netmask}
                      onChange={(e) => setIpConfig({ ...ipConfig, netmask: (e.target as HTMLInputElement).value })}
                      placeholder={i18n._('sys.system_management.netmask')}
                    />
                </div>
                <Separator />
                <div className="flex justify-between items-center gap-2 py-2">
                    <Label className="text-sm shrink-0">{i18n._('sys.system_management.gateway')}</Label>
                    <Input
                      type="text"
                      className="w-[160px] text-right bg-white"
                      readOnly={ipConfig.ip_mode === 'dhcp'}
                      value={displayIp.gateway}
                      onChange={(e) => setIpConfig({ ...ipConfig, gateway: (e.target as HTMLInputElement).value })}
                      placeholder={i18n._('sys.system_management.gateway')}
                    />
                </div>
                <div className="flex justify-end py-2">
                    <Button size="sm" variant="primary" disabled={ipSaving || showReloadMask} onClick={handleSaveIpConfig}>
                        {i18n._('common.save')}
                    </Button>
                </div>
            </div>
        </div>
    );

    const forgetNetworkDialog = () => (
        <Dialog open={isForgetDialogOpen} onOpenChange={setIsForgetDialogOpen}>
            <DialogContent className="mx-8">
                <DialogHeader>
                    <DialogTitle>{i18n._('common.confirm')}</DialogTitle>
                    <div className="text-sm text-text-primary my-4">
                        {i18n._('sys.system_management.halow_forget_network_confirm')}
                    </div>
                </DialogHeader>
                <DialogFooter>
                    <Button className="md:w-auto w-1/2" variant="outline" onClick={() => setIsForgetDialogOpen(false)}>
                        {i18n._('common.cancel')}
                    </Button>
                    <Button className="md:w-auto w-1/2" variant="primary" disabled={forgetLoading} onClick={handleForgetNetwork}>
                        {i18n._('common.confirm')}
                    </Button>
                </DialogFooter>
            </DialogContent>
        </Dialog>
    );

    const radioSettingsSection = () => (
        <div className="mt-4">
            <div className="relative mb-2">
                <p className="text-sm font-bold">{i18n._('sys.system_management.halow_radio_settings')}</p>
                <Button
                  variant="ghost"
                  className="absolute -top-2 right-0"
                  onClick={handleRefreshRadio}
                  disabled={radioRefreshing}
                  title={i18n._('sys.system_management.halow_refresh_radio')}
                >
                    <SvgIcon icon="reload2" className={`w-6 h-6 ${radioRefreshing ? 'animate-spin' : ''}`} />
                </Button>
            </div>
            <div className="flex flex-col bg-gray-100 px-4 py-2 rounded-lg gap-1">
                <div className="flex justify-between items-center gap-2 py-2">
                    <div className="shrink-0">
                        <Label className="text-sm">{i18n._('sys.system_management.halow_tx_power')}</Label>
                        <p className="text-xs text-text-secondary">
                            {i18n._('sys.system_management.halow_tx_power_hint')}
                            {' '}
                            ({radioLimits.tx_power_dbm.min}–{radioLimits.tx_power_dbm.max} dBm)
                        </p>
                    </div>
                    <Input
                      type="number"
                      className="w-[120px] text-right bg-white"
                      min={radioLimits.tx_power_dbm.min}
                      max={radioLimits.tx_power_dbm.max}
                      value={radioConfig.tx_power_dbm}
                      onChange={(e) => setRadioConfig({
                          ...radioConfig,
                          tx_power_dbm: Number((e.target as HTMLInputElement).value),
                      })}
                    />
                </div>
                <Separator />
                <div className="flex justify-between items-center gap-2 py-2">
                    <div className="shrink-0">
                        <Label className="text-sm">{i18n._('sys.system_management.halow_scan_dwell')}</Label>
                        <p className="text-xs text-text-secondary">
                            {i18n._('sys.system_management.halow_scan_dwell_hint')}
                            {' '}
                            ({radioLimits.scan_dwell_ms.min}–{radioLimits.scan_dwell_ms.max} ms)
                        </p>
                    </div>
                    <Input
                      type="number"
                      className="w-[120px] text-right bg-white"
                      min={radioLimits.scan_dwell_ms.min}
                      max={radioLimits.scan_dwell_ms.max}
                      value={radioConfig.scan_dwell_ms}
                      onChange={(e) => setRadioConfig({
                          ...radioConfig,
                          scan_dwell_ms: Number((e.target as HTMLInputElement).value),
                      })}
                    />
                </div>
                <Separator />
                <div className="flex justify-between items-center gap-2 py-2">
                    <Label className="text-sm shrink-0">{i18n._('sys.system_management.halow_rate_mcs')}</Label>
                    <Select
                      value={radioConfig.rate_mcs < 0 ? RADIO_AUTO : String(radioConfig.rate_mcs)}
                      onValueChange={(value) => setRadioConfig({
                          ...radioConfig,
                          rate_mcs: value === RADIO_AUTO ? -1 : Number(value),
                      })}
                    >
                        <SelectTrigger className="w-[120px] bg-white">
                            <SelectValue />
                        </SelectTrigger>
                        <SelectContent>
                            <SelectItem value={RADIO_AUTO}>{i18n._('sys.system_management.halow_rate_auto')}</SelectItem>
                            {Array.from({ length: 10 }, (_, i) => (
                                <SelectItem key={i} value={String(i)}>{i}</SelectItem>
                            ))}
                        </SelectContent>
                    </Select>
                </div>
                <Separator />
                <div className="flex justify-between items-center gap-2 py-2">
                    <Label className="text-sm shrink-0">{i18n._('sys.system_management.halow_rate_bw')}</Label>
                    <Select
                      value={radioConfig.rate_bw_mhz < 0 ? RADIO_AUTO : String(radioConfig.rate_bw_mhz)}
                      onValueChange={(value) => setRadioConfig({
                          ...radioConfig,
                          rate_bw_mhz: value === RADIO_AUTO ? -1 : Number(value),
                      })}
                    >
                        <SelectTrigger className="w-[120px] bg-white">
                            <SelectValue />
                        </SelectTrigger>
                        <SelectContent>
                            <SelectItem value={RADIO_AUTO}>{i18n._('sys.system_management.halow_rate_auto')}</SelectItem>
                            {[1, 2, 4, 8].map((bw) => (
                                <SelectItem key={bw} value={String(bw)}>{`${bw} MHz`}</SelectItem>
                            ))}
                        </SelectContent>
                    </Select>
                </div>
                <Separator />
                <div className="flex justify-between items-center gap-2 py-2">
                    <Label className="text-sm shrink-0">{i18n._('sys.system_management.halow_rate_gi')}</Label>
                    <Select
                      value={radioConfig.rate_gi < 0 ? RADIO_AUTO : String(radioConfig.rate_gi)}
                      onValueChange={(value) => setRadioConfig({
                          ...radioConfig,
                          rate_gi: value === RADIO_AUTO ? -1 : Number(value),
                      })}
                    >
                        <SelectTrigger className="w-[120px] bg-white">
                            <SelectValue />
                        </SelectTrigger>
                        <SelectContent>
                            <SelectItem value={RADIO_AUTO}>{i18n._('sys.system_management.halow_rate_auto')}</SelectItem>
                            <SelectItem value="0">{i18n._('sys.system_management.halow_rate_gi_short')}</SelectItem>
                            <SelectItem value="1">{i18n._('sys.system_management.halow_rate_gi_long')}</SelectItem>
                        </SelectContent>
                    </Select>
                </div>
                <div className="flex justify-end py-2">
                    <Button size="sm" variant="primary" disabled={radioSaving || showReloadMask} onClick={handleSaveRadioConfig}>
                        {i18n._('common.save')}
                    </Button>
                </div>
            </div>
        </div>
    );

    const connectDialog = () => (
        <Dialog
          open={isConnectDialogOpen}
          onOpenChange={(open) => {
                setIsConnectDialogOpen(open);
                if (!open) setHalowInfo(null);
            }}
        >
            <DialogContent className="mx-8" showCloseButton={false}>
                <DialogClose asChild onClick={() => handleCancelConnect()}>
                    <Button
                      variant="ghost"
                      onClick={() => handleCancelConnect()}
                      className="absolute right-4 top-4 h-8 w-8 p-0 rounded-full opacity-70 hover:opacity-100 transition-opacity z-10"
                    >
                        <SvgIcon icon="close" className="w-4 h-4" />
                    </Button>
                </DialogClose>
                <DialogHeader>
                    <DialogTitle>{i18n._('sys.system_management.halow_dialog_title')}</DialogTitle>
                </DialogHeader>
                <div className="my-4">
                    <div className="mb-2">
                        <p>{i18n._('sys.system_management.join_network')} {halowInfo?.ssid ?? ''}</p>
                    </div>
                    <div className="relative">
                        <Input
                          placeholder={i18n._('sys.system_management.wifi-dialog-placeholder')}
                          type={isPasswordVisible ? 'text' : 'password'}
                          value={halowPassword}
                          onChange={(e) => setHalowPassword((e.target as HTMLInputElement).value)}
                        />
                        <button
                          type="button"
                          onClick={handlePasswordVisible}
                          className="absolute top-1/2 -translate-y-1/2 right-0 flex items-center bg-transparent pr-4 border-none cursor-pointer disabled:opacity-50"
                        >
                            {isPasswordVisible ? (
                                <SvgIcon className="w-4 h-4" icon="visibility" />
                            ) : (
                                <SvgIcon className="w-4 h-4" icon="visibility_off" />
                            )}
                        </button>
                    </div>
                    {isErrorPassword && (
                        <p className="text-sm text-red-500 mt-1">{i18n._('sys.system_management.password_error')}</p>
                    )}
                </div>
                <DialogFooter>
                    <Button size="sm" className="w-1/2 md:w-auto" variant="outline" onClick={() => handleCancelConnect()}>{i18n._('common.cancel')}</Button>
                    <Button size="sm" className="w-1/2 md:w-auto" variant="primary" onClick={() => handleConnect(halowInfo as HalowData)}>{i18n._('common.confirm')}</Button>
                </DialogFooter>
            </DialogContent>
        </Dialog>
    );

    return (
        <div className="mt-2">
            <div className="flex gap-2 justify-between items-center mb-4">
                <Label className="text-sm font-bold text-text-primary">{i18n._('sys.system_management.halow_region')}</Label>
                <Select
                  value={region}
                  onValueChange={handleRegionChange}
                  disabled={!!currentHalowData?.connected}
                >
                    <SelectTrigger className="w-[180px]">
                        <SelectValue placeholder={i18n._('sys.system_management.halow_region')}>
                            {region ? getHalowRegionLabel(region, locale) : null}
                        </SelectValue>
                    </SelectTrigger>
                    <SelectContent>
                        {supportedRegions.map((code) => (
                            <SelectItem key={code} value={code}>
                                {getHalowRegionLabel(code, locale)}
                            </SelectItem>
                        ))}
                    </SelectContent>
                </Select>
            </div>

            {isLoading && <CommunicationSkeleton />}
            {!isLoading && (
                <div className="relative">
                    <Button
                      variant="ghost"
                      className="absolute -top-2 right-0"
                      onClick={handleScan}
                      title={i18n._('sys.system_management.refresh')}
                    >
                        <SvgIcon icon="reload2" className="w-6 h-6" />
                    </Button>

                    {!currentHalowData?.connected && otherHalowDataList.length === 0 && (
                        <div className="h-[400px] flex flex-col items-center justify-center">
                            <SvgIcon icon="empty" className="w-40 h-40" />
                            <p className="text-sm text-text-secondary">{i18n._('sys.system_management.no_network')}</p>
                        </div>
                    )}

                    {currentHalowData?.connected && (
                        <>
                            <p className="text-sm font-bold mb-2">{i18n._('sys.system_management.current_network')}</p>
                            <div className="flex flex-col gap-2 bg-gray-100 p-4 rounded-lg">
                                <div className="flex justify-between">
                                    <div className="flex items-center gap-2">
                                        <div className="w-6 h-6 bg-primary rounded-md flex items-center justify-center">
                                            <SvgIcon icon="wifi-halow" className="w-4 h-4 text-white" />
                                        </div>
                                        <p>{i18n._('sys.system_management.halow')}</p>
                                    </div>
                                </div>
                                <Separator />
                                <div className="flex justify-between">
                                    <Label>{currentHalowData.ssid}</Label>
                                    <div className="flex items-center gap-1">
                                        <div className="flex items-center space-x-1 mr-1">
                                            <SvgIcon icon="check" className="w-4 h-4" />
                                            <p className="text-sm text-green-500">{i18n._('common.connected')}</p>
                                        </div>
                                        <SvgIcon icon={currentHalowData.rssi >= -55 ? 'wifi' : currentHalowData.rssi >= -75 ? 'wifi_middle' : 'wifi_low'} className="w-4 h-4 text-[#272E3B]" />
                                        <Popover>
                                            <PopoverTrigger onClick={(e: any) => e.stopPropagation()}>
                                                <SvgIcon icon="more" className="w-4 h-4 text-white cursor-pointer" />
                                            </PopoverTrigger>
                                            <PopoverContent className="w-auto p-0">
                                                <div className="flex flex-col m-1">
                                                    <div className="text-sm px-4 py-1 cursor-pointer hover:bg-gray-100 hover:rounded-md" onClick={() => handleDisconnect()}>{i18n._('sys.system_management.disconnect')}</div>
                                                    <Separator className="my-1" />
                                                    <div className="text-sm px-4 py-1 cursor-pointer hover:bg-gray-100 hover:rounded-md" onClick={() => setIsForgetDialogOpen(true)}>{i18n._('sys.system_management.halow_forget_network')}</div>
                                                </div>
                                            </PopoverContent>
                                        </Popover>
                                    </div>
                                </div>
                            </div>
                        </>
                    )}

                    {otherHalowDataList.length > 0 && (
                        <div className="mt-4">
                            <p className="text-sm font-bold mb-2">{i18n._('sys.system_management.other_network')}</p>
                            <div className="flex flex-col bg-gray-100 px-4 rounded-lg">
                                {otherHalowDataList.map((item, index) => (
                                    <div
                                      key={`${item.ssid}-${item.bssid}-${index}`}
                                      className="group"
                                      onClick={() => {
                                            if (!isMobile) return;
                                            if (item.quick_connect && item.security !== 'open') {
                                                handleQuickConnect(item);
                                            } else {
                                                openConnectDialog(item);
                                            }
                                        }}
                                    >
                                        <div className="flex justify-between py-2">
                                            <Label className="text-sm">{item.ssid}</Label>
                                            <div className="flex items-center gap-1">
                                                {item.quick_connect && item.security !== 'open' && (
                                                    <Button
                                                      size="sm"
                                                      variant="primary"
                                                      onClick={(e) => { e.stopPropagation(); handleQuickConnect(item); }}
                                                      className="mr-2 opacity-0 group-hover:opacity-100 transition-opacity"
                                                    >
                                                        {i18n._('sys.system_management.halow_quick_connect')}
                                                    </Button>
                                                )}
                                                <Button size="sm" variant="outline" onClick={(e) => { e.stopPropagation(); openConnectDialog(item); }} className="mr-2 opacity-0 group-hover:opacity-100 transition-opacity">{i18n._('common.connect')}</Button>
                                                {item.security !== 'open' && <SvgIcon icon="lock" className="w-4 h-4 mr-1" />}
                                                <SvgIcon icon={item.rssi >= -55 ? 'wifi' : item.rssi >= -75 ? 'wifi_middle' : 'wifi_low'} className="w-4 h-4 text-[#272E3B]" />
                                            </div>
                                        </div>
                                        {index !== otherHalowDataList.length - 1 && <Separator />}
                                    </div>
                                ))}
                            </div>
                        </div>
                    )}

                    {networkConfigSection()}
                    {radioSettingsSection()}
                </div>
            )}
            {connectDialog()}
            {forgetNetworkDialog()}
            {showReloadMask && <WifiReloadMask loadingText={loadingText} isLoading={isReloading} />}
        </div>
    );
}
