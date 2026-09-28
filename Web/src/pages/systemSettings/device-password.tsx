import { useState, useEffect } from 'preact/hooks';
import { useLingui } from '@lingui/react';
import { Label } from '@/components/ui/label';
import { Separator } from '@/components/ui/separator';
import { Input } from '@/components/ui/input';
import { Button } from '@/components/ui/button';
import { Select, SelectContent, SelectItem, SelectTrigger, SelectValue } from '@/components/ui/select';
import SvgIcon from '@/components/svg-icon';
import { useIsMobile } from '@/hooks/use-mobile';
import loginApis from '@/services/api/login';
import systemSettings from '@/services/api/systemSettings';
import { toast } from 'sonner';
import { Skeleton } from '@/components/ui/skeleton';
import { Dialog, DialogContent, DialogHeader, DialogTitle, DialogFooter } from '@/components/dialog';
import { getItem } from '@/utils/storage';
import { useNavigate } from 'react-router-dom';
import WifiReloadMask from '@/components/wifi-reload-mask';
import { retryFetch } from '@/utils';
import { useAuthStore } from '@/store/auth';

export default function DevicePassword({ setCurrentPage }: { setCurrentPage: (page: string | null) => void }) {
    const { i18n } = useLingui();
    const { initValidateToken, clearToken } = useAuthStore();
    const isMobile = useIsMobile();
    const navigate = useNavigate();
    const { changePassword } = loginApis;
    const [isLoading, setIsLoading] = useState(false);
    const { getAPConfigReq, setWifiConfig } = systemSettings;
    const [wifiParams, setWifiParams] = useState({
        ssid: '',
        password: '',
        ap_sleep_time: '600'
    });

    // const [isWifiPasswordVisible, setIsWifiPasswordVisible] = useState(false);
    const [loginPassword, setLoginPassword] = useState('');
    const [errorWifiPassword, setErrorWifiPassword] = useState({ valid: true, message: '' });
    const [errorLoginPassword, setErrorLoginPassword] = useState({ valid: true, message: '' });
    const [oldLoginPassword, setOldLoginPassword] = useState('');
    // const [oldWifiPassword, setOldWifiPassword] = useState('');

    const getOldLoginPassword = async () => {
        try {
            const tokenRaw = (getItem('token') ?? '').trim();
            let decoded = '';
            try {
                const base64 = tokenRaw.startsWith('Basic ')
                    ? tokenRaw.slice(6).trim()
                    : tokenRaw;
                const padded = base64 + '==='.slice((base64.length + 3) % 4);
                decoded = window.atob(padded);
            } catch (error: any) {
                console.error('getOldLoginPassword', error);
                decoded = '';
            }
            const [, currentPassword = ''] = decoded.split(':', 2);
           return currentPassword;
        } catch (error) {
            console.error('getOldLoginPassword', error);
            return '';
        }
    }
    const initWifiConfig = async () => {
        try {
            setIsLoading(true);
            // Initialize WiFi configuration
            const res = await getAPConfigReq();
            console.log(res.data);
            setWifiParams({
                ssid: String(res.data?.ssid ?? ''),
                password: String(res.data?.password ?? ''),
                ap_sleep_time: String(Number(res.data?.ap_sleep_time ?? 0) / 60)
            });
            // setOldWifiPassword(res.data.network_service?.password ?? '');
            const currentPassword = await getOldLoginPassword();
            setLoginPassword(currentPassword ?? '');
            setOldLoginPassword(currentPassword ?? '');
        } catch (error) {
            console.error('initWifiConfig', error);
        } finally {
            setIsLoading(false);
        }
    }
    useEffect(() => {
        initWifiConfig();
    }, []);
    const isValidatePassword = (password: string, customValidateCbs?: (() => { valid: boolean, message: string })[]): { valid: boolean, message: string } => {
        // Only allow letters, numbers, and common symbols
        const allowedPattern = /^[a-zA-Z0-9!@#$%^&*()_+\-=[\]{}|;':",./<>?`~]*$/;
        if (!allowedPattern.test(password)) {
            return {
                valid: false,
                message: i18n._('sys.system_management.password_error')
            };
        }
        if (customValidateCbs) {
            for (const cb of customValidateCbs) {
                if (!cb().valid) {
                    return {
                        valid: false,
                        message: cb().message
                    };
                }
            }
        }   
        return { valid: true, message: '' };
    }
    // const handleWifiPasswordVisible = (e: MouseEvent) => {
    //     e.preventDefault();
    //     setIsWifiPasswordVisible(!isWifiPasswordVisible);
    // };

    const [isPasswordVisible, setIsPasswordVisible] = useState(false);

    const handlePasswordVisible = (e: MouseEvent) => {
        e.preventDefault();
        setIsPasswordVisible(!isPasswordVisible);
    };

    const goBack = () => {
        setCurrentPage(null);
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
    // const [isConfirmDialogOpen, setIsConfirmDialogOpen] =    (false);
    const [isConfirmChangeWifiPasswordDialogOpen, setIsConfirmChangeWifiPasswordDialogOpen] = useState(false);
    const  [isConfirmChangeLoginPasswordDialogOpen, setIsConfirmChangeLoginPasswordDialogOpen] = useState(false);
    const confirmWifiPasswordDialog = () => (
        <Dialog open={isConfirmChangeWifiPasswordDialogOpen} onOpenChange={setIsConfirmChangeWifiPasswordDialogOpen}>
            <DialogContent className="mx-8">
                <DialogHeader>
                    <DialogTitle>{i18n._('common.confirm')}</DialogTitle>
                    <div className="text-sm text-text-primary my-4">{i18n._('sys.system_management.confirm_connect_wifi')}</div>
                </DialogHeader>
                <DialogFooter>
                    <Button className="md:w-auto w-1/2" variant="outline" onClick={() => setIsConfirmChangeWifiPasswordDialogOpen(false)}>{i18n._('common.cancel')}</Button>
                    <Button className="md:w-auto w-1/2" variant="primary" onClick={handleChangeWifiPasswordChange}>{i18n._('common.confirm')}</Button>
                </DialogFooter>
            </DialogContent>
        </Dialog>
    )
    const confirmLoginPasswordDialog = () => (
        <Dialog open={isConfirmChangeLoginPasswordDialogOpen} onOpenChange={setIsConfirmChangeLoginPasswordDialogOpen}>
            <DialogContent className="mx-8">
                <DialogHeader>
                    <DialogTitle>{i18n._('common.confirm')}</DialogTitle>
                    <div className="text-sm text-text-primary my-4">{i18n._('sys.system_management.confirm_change_login_password')}</div>
                </DialogHeader>
                <DialogFooter>
                    <Button className="md:w-auto w-1/2" variant="outline" onClick={() => setIsConfirmChangeLoginPasswordDialogOpen(false)}>{i18n._('common.cancel')}</Button>
                    <Button className="md:w-auto w-1/2" variant="primary" onClick={handleChangeLoginPassword}>{i18n._('common.confirm')}</Button>
                </DialogFooter>
            </DialogContent>
        </Dialog>
    )

    const samePassword = (password1: string, password2: string) => {
        if (password1 === password2) {
            return { valid: false, message: i18n._('sys.system_management.new_password_cannot_be_the_same') };
        }
        return { valid: true, message: '' };
    }
    const validateLoginPassword = (password: string): { valid: boolean, message: string } => {
        const customValidateCbs = [
            () => samePassword(oldLoginPassword, loginPassword),
            () => {
                if (loginPassword.length < 8 || loginPassword.length > 32) {
                    return { valid: false, message: i18n._('sys.system_management.password_error') };
                }
                return { valid: true, message: '' };
            }
        ]
        return isValidatePassword(password, customValidateCbs);
    }

    const handleConfirmLoginPasswordChange = () => {
        const validateResult = validateLoginPassword(loginPassword);
        if (!validateResult?.valid) {
            setErrorLoginPassword(validateResult as { valid: boolean, message: string });
            return;
        }
        setErrorLoginPassword({ valid: true, message: '' });
        setIsConfirmChangeLoginPasswordDialogOpen(true);
    }
    const validateWifiPassword = (password: string): { valid: boolean, message: string } => {
        const customValidateCbs = [
            // () => samePassword(oldWifiPassword, password),
            () => {
                if ((password.length < 8 && password.length !== 0) || password.length > 64) {
                    return { valid: false, message: i18n._('sys.system_management.password_error') };
                }
                return { valid: true, message: '' };
            }
        ]
        return isValidatePassword(password, customValidateCbs);
    }
    const handleConfirmWifiPasswordChange = () => {
        const validateResult = validateWifiPassword(wifiParams.password);
        if (!validateResult?.valid) {
            setErrorWifiPassword(validateResult as { valid: boolean, message: string });
            return;
        }
        setErrorWifiPassword({ valid: true, message: '' });
        setIsConfirmChangeWifiPasswordDialogOpen(true);
    }
    const [showWifiReloadMask, setShowWifiReloadMask] = useState(false);
    const handleChangeWifiPasswordChange = async () => {
        try {
            setIsConfirmChangeWifiPasswordDialogOpen(false);
            await setWifiConfig({
                ssid: wifiParams.ssid,
                password: wifiParams.password,
                interface: 'ap',
                ap_sleep_time: Number(wifiParams.ap_sleep_time) * 60
            });
            const result = await retryFetch(initWifiConfig, 3000, 3);
            if (result) {
                setShowWifiReloadMask(false);
            }
        } catch (error) {
            setShowWifiReloadMask(true);
            console.error('handleConfirmWifiPasswordChange', error);
        } finally {
            setIsConfirmChangeWifiPasswordDialogOpen(false);
        }
    }
    const handleChangeLoginPassword = async () => {
        try {
            await changePassword({ password: loginPassword });
            clearToken();
            navigate('/login');
            initValidateToken();
            
            toast.success(i18n._('common.configSuccess'));
        } catch (error) {
            console.error('handleChangeLoginPassword', error);
        } finally {
            setIsConfirmChangeLoginPasswordDialogOpen(false);
        }
    }
    useEffect(() => {
        if (!errorLoginPassword.valid) {
            if (loginPassword !== '' && validateLoginPassword(loginPassword).valid) {
                setErrorLoginPassword({ valid: true, message: "" });
            } else {
                setErrorLoginPassword({ valid: false, message: i18n._('sys.system_management.password_error') });
            }
        }
    }, [loginPassword]);
    useEffect(() => {
        if (!errorWifiPassword.valid) {
            if (validateWifiPassword(wifiParams.password).valid) {
                setErrorWifiPassword({ valid: true, message: '' });
            } else {
                setErrorWifiPassword({ valid: false, message: i18n._('sys.system_management.password_error') });
            }
        }
    }, [wifiParams.password]);

    const skeleton = () => (
        <div className="flex flex-col items-start mt-4">
            <div className="w-full">
                <Skeleton className="h-6 w-25 mb-2" />
                <Skeleton className="h-10 w-full mb-2" />
                <Skeleton className="h-10 w-full mb-2" />
                <Skeleton className="h-10 w-full mb-4" />
                <Skeleton className="h-6 w-20 mb-2" />
                <Skeleton className="h-10 w-full mb-2" />
                <Skeleton className="h-10 w-full mb-4" />
            </div>
            <Skeleton className="h-10 w-15 mb-2" />
        </div>
    )
    return (
        <div>
            {isMobile && renderBackSlot()}
            {isLoading && skeleton()}
            {showWifiReloadMask && <WifiReloadMask loadingText={i18n._('sys.system_management.connecting_network')} />}
            {!isLoading && (
                <div className="flex flex-col">

                    <p className="text-sm text-text-primary font-bold mt-5 mb-2">{i18n._('sys.system_management.wifi_settings')}</p>
                    <div className="flex flex-col gap-2 bg-gray-100 p-4 rounded-lg">
                        <div className="flex justify-between">
                            <Label className="text-sm text-text-primary shrink-0">{i18n._('sys.system_management.wifi_name')}</Label>
                            <Input variant="ghost" value={wifiParams.ssid} onChange={(e) => setWifiParams({ ...wifiParams, ssid: (e.target as HTMLInputElement).value })} placeholder={i18n._('common.please_enter')} className="text-sm text-text-primary" />
                        </div>
                        {/* <Separator /> */}
                        {/* <div className="flex justify-between">
                            <Label className="text-sm text-text-primary shrink-0">{i18n._('sys.system_management.wifi_password')}</Label>
                            <div className="flex flex-col gap-2">
                                <div className="relative flex justify-end flex-1">
                                    <Input
                                      placeholder={i18n._('sys.system_management.wifi-dialog-placeholder')}
                                      type={isWifiPasswordVisible ? 'text' : 'password'}
                                      variant="ghost"
                                      value={wifiParams.password}
                                      onChange={(e) => setWifiParams({ ...wifiParams, password: (e.target as HTMLInputElement).value })}
                                    />
                                    <button
                                      type="button"
                                      onClick={handleWifiPasswordVisible}
                                      className="flex items-center bg-transparent pr-4 border-none cursor-pointer disabled:opacity-50"
                                    >
                                        {isWifiPasswordVisible ? (
                                            <SvgIcon className="w-4 h-4" icon="visibility" />
                                        ) : (
                                            <SvgIcon className="w-4 h-4" icon="visibility_off" />
                                        )}
                                    </button>
                                </div>
                                {!errorWifiPassword.valid && (
                                    <p className="text-sm text-red-500 mt-1 self-end">{errorWifiPassword.message}</p>
                                )}
                            </div>
                        </div> */}
                        <Separator />
                        <div className="flex justify-between">
                            <Label className="text-sm text-text-primary shrink-0">{i18n._('sys.system_management.sleep_time')}</Label>
                            <Select value={wifiParams.ap_sleep_time} onValueChange={(value) => setWifiParams({ ...wifiParams, ap_sleep_time: value })}>
                                <SelectTrigger
                                  value={wifiParams.ap_sleep_time}
                                  className="bg-transparent border-0 !shadow-none !outline-none
             focus:!outline-none focus:!ring-0 focus:!ring-offset-0 focus:!shadow-none focus:!border-transparent
             focus-visible:!outline-none focus-visible:!ring-0 focus-visible:!ring-offset-0
             text-right"
                                >
                                    <SelectValue />
                                </SelectTrigger>
                                <SelectContent>
                                    <SelectItem value="0">{i18n._('sys.system_management.never_sleep')}</SelectItem>
                                    <SelectItem value="10">10 {i18n._('sys.system_management.minutes')}</SelectItem>
                                    <SelectItem value="20">20 {i18n._('sys.system_management.minutes')}</SelectItem>
                                    <SelectItem value="30">30 {i18n._('sys.system_management.minutes')}</SelectItem>
                                </SelectContent>
                            </Select>
                        </div>
                    </div>
                    <div className="flex justify-end mt-4 mb-4">
                        <Button variant="primary" className="" onClick={handleConfirmWifiPasswordChange}>{i18n._('common.save')}</Button>
                    </div>
                    <p className="text-sm text-text-primary font-bold mt-4 mb-2">{i18n._('sys.system_management.login_password')}</p>
                    <div className="flex flex-col gap-2 bg-gray-100 p-4 rounded-lg">
                        <div className="flex justify-between">
                            <Label className="text-sm text-text-primary shrink-0">{i18n._('sys.system_management.username')}</Label>
                            <Input variant="ghost" placeholder={i18n._('common.please_enter')} disabled value="admin" className="text-sm text-text-primary" />
                        </div>
                        <Separator />
                        <div className="flex justify-between">
                            <Label className="text-sm text-text-primary shrink-0">{i18n._('sys.system_management.connection_password')}</Label>
                            <div className="flex flex-col gap-2 flex-1">
                                <div className="relative flex justify-end flex-1">
                                    <Input
                                      placeholder={i18n._('sys.system_management.password_placeholder')}
                                      type={isPasswordVisible ? 'text' : 'password'}
                                      variant="ghost"
                                      value={loginPassword}
                                      onChange={(e) => setLoginPassword((e.target as HTMLInputElement).value)}
                                    />
                                    <button
                                      type="button"
                                      onClick={handlePasswordVisible}
                                      className="flex items-center bg-transparent pr-4 border-none cursor-pointer disabled:opacity-50"
                                    >
                                        {isPasswordVisible ? (
                                            <SvgIcon className="w-4 h-4" icon="visibility" />
                                        ) : (
                                            <SvgIcon className="w-4 h-4" icon="visibility_off" />
                                        )}
                                    </button>
                                </div>
                                {!errorLoginPassword.valid && (
                                    <p className="text-sm text-red-500 mt-1 self-end">{errorLoginPassword.message}</p>
                                )}
                            </div>

                        </div>
                    </div>
                    <Button variant="primary" className="mt-4 grow-0 self-end" onClick={() => handleConfirmLoginPasswordChange()}>{i18n._('common.save')}</Button>
                </div>
            )}
            {confirmWifiPasswordDialog()}
            {confirmLoginPasswordDialog()}
        </div>
    )
}