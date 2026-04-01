import { useState, useEffect } from "preact/hooks";
import { useLingui } from "@lingui/react";
import CommunicationSkeleton from './skeleton';
import { Separator } from "@/components/ui/separator";
import { Input } from '@/components/ui/input';
import { Label } from '@/components/ui/label';
import { Button } from '@/components/ui/button';
import { Tooltip, TooltipTrigger, TooltipContent } from '@/components/tooltip';
import { ScrollArea } from '@/components/ui/scroll-area';
import { Dialog, DialogContent, DialogDescription, DialogFooter, DialogHeader, DialogTitle } from '@/components/dialog';
import { Select, SelectContent, SelectItem, SelectTrigger, SelectValue } from '@/components/ui/select';
import { Switch } from '@/components/ui/switch';
import systemSettings, { type SetCellularReq } from '@/services/api/systemSettings';
import { toast } from 'sonner';
import SvgIcon from "@/components/svg-icon";
import { Textarea } from "@/components/ui/textarea";
import Loading from "@/components/loading";
import { useCommunicationData } from '@/store/communicationData';

type ErrorType = {
    error: boolean;
    message: string;
}
type Errors = {
    apn: ErrorType;
    username: ErrorType;
    password: ErrorType;
    pin_code: ErrorType;
    authentication: ErrorType;
    enable_roaming: ErrorType;
}
export default function CellularNetworkPage() {
    const { i18n } = useLingui();
    const [isLoading, setIsLoading] = useState(false);
    const [cellularStatus, setCellularStatus] = useState<any>(null);
    const [connectLoading, setConnectLoading] = useState(false);
    const { getCommunicationData } = useCommunicationData();
    const [saveLoading, setSaveLoading] = useState(false);
    const { getCellularStatusReq, saveCellularReq, getCellularInfoReq, connectCellularReq, atCmdCellularReq, disconnectCellularReq, refreshCellularReq } = systemSettings;
    const [autoCheck, setAutoCheck] = useState(false);
    const [atCommandLoading, setAtCommandLoading] = useState(false);
    const [atCommandsResponse, setAtCommandsResponse] = useState<string>('');
    const [atCommand, setAtCommand] = useState<string>('');
    const [errors, setErrors] = useState<Errors>({
        apn: {
            error: false,
            message: '',
        },
        username: {
            error: false,
            message: '',
        },
        password: {
            error: false,
            message: '',
        },
        pin_code: {
            error: false,
            message: '',
        },
        authentication: {
            error: false,
            message: '',
        },
        enable_roaming: {
            error: false,
            message: '',
        },
    });
    const [saveCellularData, setSaveCellularData] = useState<SetCellularReq>({
        apn: '',
        username: '',
        password: '',
        pin_code: '',
        authentication: 0,
        enable_roaming: false,
        operator: 0,
        save: true
    });
    const isChinese = (str: string): boolean => /^[\u4e00-\u9fa5]+$/.test(str)
    const validateCellularConfig = (): boolean => {
        let isValid = true;
        // eslint-disable-next-line camelcase
        const { apn, username, password, pin_code } = saveCellularData;
        if (apn && apn.length > 31) {
            setErrors(prev => ({ ...prev, apn: { error: true, message: 'sys.system_management.cellular_apn_error' } }));
            isValid = false;
        } else if (apn && isChinese(apn)) {
            setErrors(prev => ({ ...prev, apn: { error: true, message: 'sys.system_management.invalid_character' } }));
            isValid = false;
        } else {
            setErrors(prev => ({ ...prev, apn: { error: false, message: '' } }));
        }

        if (username && username.length > 63) {
            setErrors(prev => ({ ...prev, username: { error: true, message: 'sys.system_management.username_error' } }));
            isValid = false;
        } else if (username && isChinese(username)) {
            setErrors(prev => ({ ...prev, username: { error: true, message: 'sys.system_management.invalid_character' } }));
            isValid = false;
        } else {
            setErrors(prev => ({ ...prev, username: { error: false, message: '' } }));
        }

        if (password && password.length > 63) {
            setErrors(prev => ({ ...prev, password: { error: true, message: 'sys.system_management.password_error' } }));
            isValid = false;
        } else if (password && isChinese(password)) {
            setErrors(prev => ({ ...prev, password: { error: true, message: 'sys.system_management.invalid_character' } }));
            isValid = false;
        } else {
            setErrors(prev => ({ ...prev, password: { error: false, message: '' } }));
        }

        // eslint-disable-next-line camelcase
        if (pin_code && pin_code.length > 31) {
            setErrors(prev => ({ ...prev, pin_code: { error: true, message: 'sys.system_management.pin_code_error' } }));
            isValid = false;
            // eslint-disable-next-line camelcase
        } else if (pin_code && isChinese(pin_code)) {
            setErrors(prev => ({ ...prev, pin_code: { error: true, message: 'sys.system_management.invalid_character' } }));
            isValid = false;
        } else {
            setErrors(prev => ({ ...prev, pin_code: { error: false, message: '' } }));
        }

        return isValid;
    }
    const getCellularStatus = async () => {
        try {
            setIsLoading(true);
            const res = await getCellularStatusReq();
            setCellularStatus(() => ({
                ...res.data,
                authentication: res.data.settings.authentication.toString(),
                imei: res.data.imei,
            }));
            setSaveCellularData(() => ({
                ...res.data.settings,
                authentication: Number(res.data.settings.authentication),
                save: true
            }));
        } catch (error) {
            console.error('getCellularStatus', error);
        } finally {
            setIsLoading(false);
        }
    }
    useEffect(() => {
        if (autoCheck) {
            validateCellularConfig();
        }
    }, [saveCellularData]);
    const saveCellular = async () => {
        try {
            setAutoCheck(true);
            const isValidate = validateCellularConfig();
            if (!isValidate) {
                return;
            }
            setSaveLoading(true);
            await saveCellularReq(saveCellularData);
            toast.success(i18n._('sys.system_management.save_success'));
        } catch (error) {
            console.error('saveCellular', error);
        } finally {
            setSaveLoading(false);
        }
    }
    const connectCellular = async () => {
        try {
            const isValidate = validateCellularConfig();
            if (!isValidate) {
                return;
            }
            setConnectLoading(true);
            await saveCellularReq(saveCellularData);
            await connectCellularReq();
            getCellularStatus();
            getCommunicationData();
        } catch (error) {
            console.error('connectCellular', error);
        } finally {
            setConnectLoading(false);
        }
    }
    const disconnectCellular = async () => {
        try {
            setConnectLoading(true);
            await disconnectCellularReq();
            getCellularStatus();
            toast.success(i18n._('sys.system_management.disconnect_success'));
            getCommunicationData();
        } catch {
            // ignore error
        } finally {
            setConnectLoading(false);
            setAutoCheck(false);
        }
    }
    useEffect(() => {
        getCellularStatus();
    }, []);

    const [detailsOpen, setDetailsOpen] = useState(false);
    const [detailsLoading, setDetailsLoading] = useState(false);
    const [cellularInfo, setCellularInfo] = useState<any>(null);
    const getCellularInfo = async () => {
        try {
            setDetailsLoading(true);
            const res = await getCellularInfoReq();
            setCellularInfo(res.data);
        } catch (error) {
            console.error('getCellularInfo', error);
        } finally {
            setDetailsLoading(false);
        }
    };
    const refreshCellular = async () => {
        try {
            setDetailsLoading(true);
            await refreshCellularReq();
            const res = await getCellularInfoReq();
            setCellularInfo(res.data)
        } catch (error) {
            console.error('refreshCellular', error);
        } finally {
            setDetailsLoading(false);
        }
    }
    useEffect(() => {
        if (detailsOpen) {
            getCellularInfo();
        }
    }, [detailsOpen]);

    const [ATSendTipOpen, setATSendTipOpen] = useState(false);

    const sendATTipDialog = () => (
        <Dialog open={ATSendTipOpen} onOpenChange={setATSendTipOpen}>
            <DialogContent>
                <DialogHeader>
                    <DialogTitle>{i18n._('common.tip')}</DialogTitle>
                </DialogHeader>
                <DialogDescription className="text-sm text-text-primary my-4">
                    {i18n._('sys.system_management.send_at_command')}
                </DialogDescription>
                <DialogFooter>
                    <Button variant="outline" className="md:w-auto w-1/2" onClick={() => setATSendTipOpen(false)}>{i18n._('common.cancel')}</Button>
                    <Button
                      variant="primary"
                      className="md:w-auto w-1/2"
                      onClick={() => {
                            setATSendTipOpen(false);
                            handleSendATCommand();
                        }}
                    >{i18n._('common.confirm')}
                    </Button>
                </DialogFooter>
            </DialogContent>
        </Dialog>
    )

    const confirmSendATCommand = async () => {
        if (cellularStatus?.status === 'Connected') {
            setATSendTipOpen(true);
        } else {
            try {
                setAtCommandLoading(true);
                const res = await atCmdCellularReq({ command: atCommand, timeout_ms: 5000 });
                setAtCommandsResponse(res.data.response);
            } catch (error) {
                console.error('confirmSendATCommand', error);
            } finally {
                setAtCommandLoading(false);
            }
        }
    }
    const handleSendATCommand = async () => {
        try {
            setAtCommandLoading(true);
            await disconnectCellularReq();
            const cellularStatusRes = await getCellularStatusReq();
            setCellularStatus(() => ({
                ...cellularStatusRes.data,
                authentication: cellularStatusRes.data.settings.authentication.toString(),
                imei: cellularStatusRes.data.imei,
            }));

            const res = await atCmdCellularReq({ command: atCommand, timeout_ms: 5000 });
            setAtCommandsResponse(res.data.response);
        } catch (error) {
            console.error('handleSendATCommand', error);
        } finally {
            setAtCommandLoading(false);
        }
    }

    return (
        <div>
            {isLoading && <CommunicationSkeleton />}
            {!isLoading && (
                <div>
                    {cellularStatus?.status === 'Connected' && (
                        <div className="mb-4">
                            <p className="text-sm font-bold mb-2">{i18n._('sys.system_management.cellular_network')}</p>
                            <div className="flex justify-between items-center gap-2 bg-gray-100 p-4 rounded-lg">
                                <div className="flex items-center">
                                    <div className="flex items-center justify-center rounded-md bg-primary w-6 h-6">
                                        <SvgIcon icon="cellular" className="w-4 h-4 text-white" />
                                    </div>
                                    <p className="text-sm text-text-primary font-bold ml-2">{i18n._('sys.system_management.cellular_network')}</p>
                                </div>
                                <p className={`text-sm font-medium ${cellularStatus?.status === 'Connected' ? 'text-green-500' : 'text-red-500'}`}>{i18n._(`common.${cellularStatus?.status === 'Connected' ? 'connected' : 'disconnected'}`)}</p>
                            </div>
                        </div>
                    )}

                    <p className="text-sm font-bold mb-2">{i18n._('sys.system_management.connection_settings')}</p>
                    <div className="flex flex-col gap-2 bg-gray-100 p-4 rounded-lg">
                        <div className="flex justify-between">
                            <Label className="text-sm text-text-primary shrink-0">{i18n._('sys.system_management.imei')}</Label>
                            <Input variant="ghost" onChange={(e) => setSaveCellularData((prev) => ({ ...prev, imei: (e.target as HTMLInputElement).value }))} placeholder={i18n._('common.please_enter')} disabled value={cellularStatus?.imei} className="text-sm text-text-primary" />
                        </div>
                        <Separator />
                        <div className="flex justify-between">
                            <Label className="text-sm text-text-primary shrink-0">{i18n._('sys.system_management.apn')}</Label>
                            <div className="flex flex-col gap-2 flex-1">
                                <Input variant="ghost" onChange={(e) => setSaveCellularData((prev) => ({ ...prev, apn: (e.target as HTMLInputElement).value }))} placeholder={i18n._('common.please_enter')} value={saveCellularData?.apn} className="text-sm text-text-primary" />
                                {errors.apn.error && <p className="text-sm text-red-500 self-end pr-2">{i18n._('sys.system_management.cellular_apn_error')}</p>}
                            </div>
                        </div>
                        <Separator />
                        <div className="flex flex-col gap-2">
                            <div className="flex justify-between gap-2 flex-1 pr-0">
                                <Label className="text-sm text-text-primary shrink-0">{i18n._('sys.system_management.cellular_username')}</Label>
                                <Input variant="ghost" onChange={(e) => setSaveCellularData((prev) => ({ ...prev, username: (e.target as HTMLInputElement).value }))} placeholder={i18n._('common.please_enter')} value={saveCellularData?.username} className="text-sm text-text-primary" />
                            </div>
                            {errors.username.error && <p className="text-sm text-red-500 self-end pr-2">{i18n._('sys.system_management.cellular_username_error')}</p>}

                        </div>
                        <Separator />
                        <div className="flex flex-col gap-2">
                            <div className="flex justify-between gap-2 flex-1 pr-0">
                                <Label className="text-sm text-text-primary shrink-0">{i18n._('sys.system_management.cellular_password')}</Label>
                                <Input variant="ghost" onChange={(e) => setSaveCellularData((prev) => ({ ...prev, password: (e.target as HTMLInputElement).value }))} placeholder={i18n._('common.please_enter')} value={saveCellularData?.password} className="text-sm text-text-primary" />
                            </div>
                            {errors.password.error && <p className="text-sm text-red-500 self-end pr-2">{i18n._('sys.system_management.cellular_password_error')}</p>}

                        </div>
                        <Separator />
                        <div className="flex flex-col gap-2">
                            <div className="flex justify-between gap-2 flex-1 pr-0">
                                <div className="flex items-center gap-2">
                                    <Label className="text-sm text-text-primary shrink-0">{i18n._('sys.system_management.pin_code')}</Label>
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
                                            <p>{i18n._('sys.system_management.cellular_pin_code_note')}</p>
                                        </TooltipContent>
                                    </Tooltip>
                                </div>
                                <Input variant="ghost" onChange={(e) => setSaveCellularData((prev) => ({ ...prev, pin_code: (e.target as HTMLInputElement).value }))} placeholder={i18n._('common.please_enter')} value={saveCellularData?.pin_code} className="text-sm text-text-primary" />
                            </div>
                            {errors.pin_code.error && <p className="text-sm text-red-500 self-end pr-2">{i18n._('sys.system_management.cellular_pin_code_error')}</p>}

                        </div>
                        <Separator />
                        <div className="flex flex-col gap-2">
                            <div className="flex justify-between">
                                <Label className="text-sm text-text-primary shrink-0">{i18n._('sys.system_management.authentication')}</Label>
                                <Select
                                  value={(saveCellularData?.authentication ?? 0).toString()}
                                  onValueChange={(value) => {
                                        const authentication = Number(value);
                                        setSaveCellularData({ ...saveCellularData, authentication });
                                        setCellularStatus((prev: any) => (prev ? { ...prev, authentication: value } : prev));
                                    }}
                                >
                                    <SelectTrigger>
                                        <SelectValue />
                                    </SelectTrigger>
                                    <SelectContent>
                                        <SelectItem value="0">None</SelectItem>
                                        <SelectItem value="1">PAP</SelectItem>
                                        <SelectItem value="2">CHAP</SelectItem>
                                        <SelectItem value="3">PAP/CHAP</SelectItem>
                                    </SelectContent>
                                </Select>
                            </div>
                            <div className="flex justify-between">
                                <Label className="text-sm text-text-primary shrink-0">{i18n._('sys.system_management.cellular_operator') ?? 'Operator'}</Label>
                                <Select
                                  value={(saveCellularData?.operator ?? 0).toString()}
                                  onValueChange={(value) => {
                                        const operator = Number(value);
                                        setSaveCellularData({ ...saveCellularData, operator });
                                    }}
                                >
                                    <SelectTrigger>
                                        <SelectValue />
                                    </SelectTrigger>
                                    <SelectContent>
                                        <SelectItem value="0">{i18n._('sys.system_management.cellular_operator_auto') ?? 'Auto'}</SelectItem>
                                        <SelectItem value="1">{i18n._('sys.system_management.cellular_operator_cmcc') ?? 'China Mobile'}</SelectItem>
                                        <SelectItem value="2">{i18n._('sys.system_management.cellular_operator_cucc') ?? 'China Unicom'}</SelectItem>
                                        <SelectItem value="3">{i18n._('sys.system_management.cellular_operator_ctcc') ?? 'China Telecom'}</SelectItem>
                                        <SelectItem value="4">{i18n._('sys.system_management.cellular_operator_verizon') ?? 'American Verizon'}</SelectItem>
                                    </SelectContent>
                                </Select>
                            </div>
                            <Separator />
                            <div className="flex justify-between items-center gap-2">
                                <Label className="text-sm text-text-primary shrink-0" htmlFor="cellular-enable-roaming">{i18n._('sys.system_management.cellular_enable_roaming')}</Label>
                                <Switch
                                    id="cellular-enable-roaming"
                                    checked={!!saveCellularData.enable_roaming}
                                    onCheckedChange={(checked) => setSaveCellularData((prev) => ({ ...prev, enable_roaming: checked }))}
                                />
                            </div>
                        </div>
                    </div>

                    <div className="flex gap-2 w-full mt-4 justify-between">
                        <Button variant="outline" onClick={() => setDetailsOpen(true)}>{i18n._('common.details')}</Button>
                        <div className="flex gap-2">
                            <Button variant="outline" disabled={saveLoading} onClick={saveCellular}>{i18n._('common.save')}</Button>
                            <Button variant="primary" disabled={connectLoading} onClick={() => (cellularStatus?.status === 'Connected' ? disconnectCellular() : connectCellular())}>
                                {connectLoading ? (
                                    <div className="w-full h-full flex items-center justify-center">
                                        <div className="w-4 h-4 rounded-full border-2 border-[#f24a00] border-t-transparent animate-spin" aria-label="loading" />
                                    </div>
                                ) : (
                                    cellularStatus?.status === 'Connected' ? i18n._('common.disconnect') : i18n._('common.connect')
                                )}
                            </Button>
                        </div>
                    </div>

                    <div className="mt-6">
                        <p className="text-sm font-bold mb-2">{i18n._('sys.system_management.at_commands')}</p>
                        <div className="flex flex-col gap-2 bg-gray-100 p-4 rounded-lg">
                            <div className="flex justify-between gap-2 flex-1 pr-0">
                                <Input placeholder={i18n._('sys.system_management.at_commands_placeholder')} className="bg-white" onChange={(e) => setAtCommand((e.target as HTMLInputElement).value)} value={atCommand} />
                                <Button disabled={!atCommand} variant="outline" onClick={() => confirmSendATCommand()}>{i18n._('common.send')}</Button>
                            </div>
                            <div className="relative">
                                {atCommandLoading && (
                                    <div className="absolute top-1/2 left-1/2 -translate-x-1/2 -translate-y-1/2 w-full h-full flex items-center justify-center">
                                        <Loading placeholder={i18n._('common.loading')} />
                                    </div>
                                )}
                                <Textarea placeholder={i18n._('sys.system_management.at_commands_response')} disabled rows={10} value={atCommandsResponse} className="text-sm text-text-primary" />
                            </div>
                        </div>
                        {sendATTipDialog()}
                    </div>
                </div>
            )}
            <Dialog open={detailsOpen} onOpenChange={setDetailsOpen}>
                <DialogContent>
                    <DialogHeader>
                        <div className="flex justify-between">
                            <DialogTitle>{i18n._('common.details')}</DialogTitle>
                            <Button className="w-8 h-8 p-0 absolute right-10 top-4" variant="ghost" onClick={() => refreshCellular()}>
                                <SvgIcon icon="reload2" className="w-4 h-4" />
                            </Button>
                        </div>
                    </DialogHeader>
                    <ScrollArea className="pr-2 pt-2">
                        {detailsLoading && <CommunicationSkeleton />}
                        {!detailsLoading && (
                            <div className="flex flex-col gap-2 bg-gray-100 p-4 rounded-lg mt-4">
                                <div className="flex justify-between my-2">
                                    <Label className="text-sm text-text-primary shrink-0">{i18n._('sys.system_management.network_status')}</Label>
                                    <p className={`text-sm font-medium ${cellularInfo?.network_status === 'Connected' ? 'text-green-500' : 'text-red-500'}`}>{i18n._(`common.${cellularInfo?.network_status === 'Connected' ? 'connected' : 'disconnected'}`)}</p>
                                </div>
                                <Separator />
                                <div className="flex justify-between my-2">
                                    <Label className="text-sm text-text-primary shrink-0">SIM status</Label>
                                    <p>{cellularInfo?.sim_status || '-'}</p>
                                </div>
                                <Separator />
                                <div className="flex justify-between my-2">
                                    <Label className="text-sm text-text-primary shrink-0">model</Label>
                                    <p>{cellularInfo?.model || '-'}</p>
                                </div>
                                <Separator />
                                <div className="flex justify-between my-2">
                                    <Label className="text-sm text-text-primary shrink-0">register status</Label>
                                    <p>{cellularInfo?.register_status || '-'}</p>
                                </div>
                                <Separator />
                                <div className="flex justify-between my-2">
                                    <Label className="text-sm text-text-primary shrink-0">version</Label>
                                    <p>{cellularInfo?.version || '-'}</p>
                                </div>
                                <Separator />
                                <div className="flex justify-between my-2">
                                    <Label className="text-sm text-text-primary shrink-0">imei</Label>
                                    <p>{cellularInfo?.imei || '-'}</p>
                                </div>
                                <Separator />
                                <div className="flex justify-between my-2">
                                    <Label className="text-sm text-text-primary shrink-0">isp</Label>
                                    <p>{cellularInfo?.isp || '-'}</p>
                                </div>
                                <Separator />
                                <div className="flex justify-between my-2">
                                    <Label className="text-sm text-text-primary shrink-0">network type</Label>
                                    <p>{cellularInfo?.network_type || '-'}</p>
                                </div>
                                <Separator />
                                <div className="flex justify-between my-2">
                                    <Label className="text-sm text-text-primary shrink-0">plmn id</Label>
                                    <p>{cellularInfo?.plmn_id || '-'}</p>
                                </div>
                                <Separator />
                                <div className="flex justify-between my-2">
                                    <Label className="text-sm text-text-primary shrink-0">lac</Label>
                                    <p>{cellularInfo?.lac || '-'}</p>
                                </div>
                                <Separator />
                                <div className="flex justify-between my-2">
                                    <Label className="text-sm text-text-primary shrink-0">cell id</Label>
                                    <p>{cellularInfo?.cell_id || '-'}</p>
                                </div>
                                <Separator />
                                <div className="flex justify-between my-2">
                                    <Label className="text-sm text-text-primary shrink-0">signal level</Label>
                                    <p>{cellularInfo?.signal_level ?? '-'}</p>
                                </div>
                                <Separator />
                                <div className="flex justify-between my-2">
                                    <Label className="text-sm text-text-primary shrink-0">csq</Label>
                                    <p>{cellularInfo?.csq ?? '-'}</p>
                                </div>
                                <Separator />
                                <div className="flex justify-between my-2">
                                    <Label className="text-sm text-text-primary shrink-0">csq level</Label>
                                    <p>{cellularInfo?.csq_level ?? '-'}</p>
                                </div>
                                <Separator />
                                <div className="flex justify-between my-2">
                                    <Label className="text-sm text-text-primary shrink-0">rssi</Label>
                                    <p>{cellularInfo?.rssi ?? '-'}</p>
                                </div>
                                <Separator />
                                <div className="flex justify-between my-2">
                                    <Label className="text-sm text-text-primary shrink-0">iccid</Label>
                                    <p>{cellularInfo?.iccid || '-'}</p>
                                </div>
                                <Separator />
                                <div className="flex justify-between my-2">
                                    <Label className="text-sm text-text-primary shrink-0">imsi</Label>
                                    <p>{cellularInfo?.imsi || '-'}</p>
                                </div>
                                <Separator />
                                <div className="flex justify-between my-2">
                                    <Label className="text-sm text-text-primary shrink-0">ipv4 address</Label>
                                    <p>{cellularInfo?.ipv4_address || '-'}</p>
                                </div>
                                <Separator />
                                <div className="flex justify-between my-2">
                                    <Label className="text-sm text-text-primary shrink-0">ipv4 gateway</Label>
                                    <p>{cellularInfo?.ipv4_gateway || '-'}</p>
                                </div>
                                <Separator />
                                <div className="flex justify-between my-2">
                                    <Label className="text-sm text-text-primary shrink-0">ipv4 dns</Label>
                                    <p>{cellularInfo?.ipv4_dns || '-'}</p>
                                </div>
                                <Separator />
                                <div className="flex justify-between my-2">
                                    <Label className="text-sm text-text-primary shrink-0">ipv6 address</Label>
                                    <p>{cellularInfo?.ipv6_address || '-'}</p>
                                </div>
                                <Separator />
                                <div className="flex justify-between my-2">
                                    <Label className="text-sm text-text-primary shrink-0">ipv6 gateway</Label>
                                    <p>{cellularInfo?.ipv6_gateway || '-'}</p>
                                </div>
                                <Separator />
                                <div className="flex justify-between my-2">
                                    <Label className="text-sm text-text-primary shrink-0">ipv6 dns</Label>
                                    <p>{cellularInfo?.ipv6_dns || '-'}</p>
                                </div>
                            </div>
                        )}
                    </ScrollArea>
                </DialogContent>
            </Dialog>
        </div>
    )
}