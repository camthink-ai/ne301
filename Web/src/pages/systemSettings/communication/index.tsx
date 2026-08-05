import { useState, useEffect, useRef } from 'react';
import { useLingui } from '@lingui/react';
import SvgIcon from '@/components/svg-icon';
import WifiNetworkPage from './wifi-network';
import HalowNetworkPage from './halow-network';
import CellularNetworkPage from './cellular-network';
import { Label } from '@/components/ui/label';
import { Select, SelectTrigger, SelectValue, SelectContent, SelectItem } from '@/components/ui/select';
import { Dialog, DialogContent, DialogDescription, DialogFooter, DialogHeader, DialogTitle } from '@/components/dialog';
import PoeNetworkPage from './poe';
import { useIsMobile } from '@/hooks/use-mobile';
import systemSettings from '@/services/api/systemSettings';
import CommunicationSkeleton from './skeleton';
import { useCommunicationData } from '@/store/communicationData';
import { Button } from '@/components/ui/button';
// import { retryFetch } from '@/utils';
// import WifiReloadMask from '@/components/wifi-reload-mask';

export default function Communication({ setCurrentPage }: { setCurrentPage: (page: string | null) => void }) {
    const { i18n } = useLingui();
    const isMobile = useIsMobile();
    const [isLoading, setIsLoading] = useState(false);
    const { switchNetworkTypeReq } = systemSettings;
    const { getCommunicationData, communicationData } = useCommunicationData();
    const [netWorkStatus, setNetWorkStatus] = useState<any>(null);
    const [communicationMode, setCommunicationMode] = useState(communicationData?.selected_type ?? 'wifi');
    const communicationModeDialogValue = useRef<string>('');
    // const [showWifiReloadMask, setShowWifiReloadMask] = useState(false);
    const getNetworkStatus = async () => {
        try {
            setIsLoading(true);
            const res = await getCommunicationData();
            setNetWorkStatus(() => ({
                    ...res.data,
                    available_comm_types: Array.isArray(res.data.available_comm_types) ? res.data.available_comm_types : []
                }));
            setCommunicationMode(res.data.selected_type);
            return res.data;
        } catch (error) {
            // eslint-disable-next-line no-console
            console.error(error);
            throw error;
        } finally {
            setIsLoading(false);
        }
    };

    useEffect(() => {
        getNetworkStatus()
    }, []);
    const goBack = () => {
        setCurrentPage(null);
    }

    const handleChangeCommunicationMode = async () => {
        setIsCommunicationModeDialogOpen(false);
        setCommunicationMode(communicationModeDialogValue.current);
        try {
            setIsLoading(true);
            const switchTimeoutMs = communicationModeDialogValue.current === 'halow' ? 30000 : 3000;
            await switchNetworkTypeReq({ type: communicationModeDialogValue.current, timeout_ms: switchTimeoutMs });
            await getNetworkStatus();
        } catch (error) {
            // eslint-disable-next-line no-console
            console.error(error);
        } finally {
            setIsLoading(false);
        }
    }

    const renderBackSlot = () => {
        if (isMobile) {
            return (
                <div className="flex text-lg font-bold justify-start items-center gap-2">
                    <div onClick={() => goBack()} className="cursor-pointer">
                        <SvgIcon icon="arrow_left" className="w-6 h-6" />
                    </div>
                    <p>{i18n._('sys.system_management.device_password')}</p>
                </div>
            )
        }
    }
    const [isCommunicationModeDialogOpen, setIsCommunicationModeDialogOpen] = useState(false);
    const communicationModeDialog = () => (
        <Dialog open={isCommunicationModeDialogOpen} onOpenChange={setIsCommunicationModeDialogOpen}>
            <DialogContent>
                <DialogHeader>
                    <DialogTitle>{i18n._('common.tip')}</DialogTitle>
                </DialogHeader>
                <DialogDescription className="text-sm text-text-primary my-4">
                    {communicationModeDialogValue.current === 'wifi' ? i18n._('sys.system_management.confirm_switch_wifi_mode') : i18n._('sys.system_management.confirm_switch_communication_mode')}
                </DialogDescription>
                <DialogFooter>
                    <Button className="md:w-auto w-1/2" variant="outline" onClick={() => setIsCommunicationModeDialogOpen(false)}>{i18n._('common.cancel')}</Button>
                    <Button className="md:w-auto w-1/2" variant="primary" onClick={handleChangeCommunicationMode}>{i18n._('common.confirm')}</Button>
                </DialogFooter>
            </DialogContent>
        </Dialog>
    )

    const openCommunicationModeDialog = (value: string) => {
        setIsCommunicationModeDialogOpen(true);
        communicationModeDialogValue.current = value;
    }

    const isAutoSelection = netWorkStatus?.is_auto_selection ?? netWorkStatus?.preferred_type === 'none';

    return (
        <div>
            {/* {showWifiReloadMask && <WifiReloadMask loadingText={i18n._('sys.system_management.network_disconnected')} isLoading={false} />} */}
            {isMobile && renderBackSlot()}
            <div className="flex gap-2 justify-between items-center my-4">
                <Label className="text-sm font-bold text-text-primary">{i18n._('sys.system_management.change_communication')}</Label>
                <div className="flex items-center gap-2">
                    {!isLoading && netWorkStatus && (
                        <span className="text-sm text-text-secondary shrink-0">
                            {isAutoSelection
                                ? i18n._('sys.system_management.comm_selection_auto')
                                : i18n._('sys.system_management.comm_selection_manual')}
                        </span>
                    )}
                    <Select value={communicationMode} onValueChange={(value) => openCommunicationModeDialog(value)}>
                        <SelectTrigger>
                            <SelectValue placeholder={i18n._('sys.system_management.communication_mode')} />
                        </SelectTrigger>
                        <SelectContent>
                            {netWorkStatus?.available_comm_types.find((item: any) => item.type === 'wifi') && <SelectItem value="wifi">{i18n._('sys.system_management.wifi')}</SelectItem>}
                            {netWorkStatus?.available_comm_types.find((item: any) => item.type === 'halow') && <SelectItem value="halow">{i18n._('sys.system_management.halow')}</SelectItem>}
                            {netWorkStatus?.available_comm_types.find((item: any) => item.type === 'cellular') && <SelectItem value="cellular">{i18n._('sys.system_management.cellular_network')}</SelectItem>}
                            {netWorkStatus?.available_comm_types.find((item: any) => item.type === 'poe') && <SelectItem value="poe">{i18n._('sys.system_management.poe_network')}</SelectItem>}
                        </SelectContent>
                    </Select>
                </div>
            </div>
            {isLoading && <CommunicationSkeleton />}
            {communicationMode === 'wifi' && !isLoading && <WifiNetworkPage />}
            {communicationMode === 'halow' && !isLoading && <HalowNetworkPage />}
            {communicationMode === 'cellular' && !isLoading && <CellularNetworkPage />}
            {communicationMode === 'poe' && !isLoading && <PoeNetworkPage />}
            {communicationModeDialog()}
        </div>
    )
}