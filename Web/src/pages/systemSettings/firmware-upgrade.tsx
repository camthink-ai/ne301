import { useEffect, useState } from 'preact/hooks';
import { useLingui } from '@lingui/react';
import { Label } from '@/components/ui/label';
import { Button } from '@/components/ui/button';
import { useIsMobile } from '@/hooks/use-mobile';
import SvgIcon from '@/components/svg-icon';
import ImportFirmware from './fireware/import-firmware';
import ExportFirmware from './fireware/export-firmware';
import systemApis from '@/services/api/system';
import { Skeleton } from '@/components/ui/skeleton';
import { Separator } from '@/components/ui/separator';
import { useNavigate } from 'react-router-dom';
import WifiReloadMask from '@/components/wifi-reload-mask';
import { sleep } from '@/utils';
import {
    Dialog,
    DialogContent,
    DialogHeader,
    DialogTitle,
    DialogFooter,
    DialogDescription,
} from '@/components/dialog';

interface FirmwareVersions {
    app: string;
    web: string;
    model: string;
    fsbl: string;
    wakecore: string;
    wifi: string;
    wifi_running: string;
    expected_fsbl: string;
    expected_wifi: string;
}

const EMPTY_VERSIONS: FirmwareVersions = {
    app: '',
web: '',
model: '',
fsbl: '',
wakecore: '',
    wifi: '',
wifi_running: '',
expected_fsbl: '',
expected_wifi: '',
};

function isUpgradableWifi(flash: string, running: string): boolean {
    return (
        flash !== 'N/A'
        && running !== 'N/A'
        && flash !== ''
        && running !== ''
        && flash !== running
    );
}

export default function FirmwareUpgrade({
    setCurrentPage,
}: {
    setCurrentPage: (page: string | null) => void;
}) {
    const isMobile = useIsMobile();
    const { i18n } = useLingui();
    const { getVersionsReq, updateOTAReq, restartDevice } = systemApis;
    const navigate = useNavigate();
    const [isLoading, setIsLoading] = useState(false);
    const [upgrading, setUpgrading] = useState(false);
    const [versions, setVersions] = useState<FirmwareVersions>(EMPTY_VERSIONS);
    const [wifiUpgradeOpen, setWifiUpgradeOpen] = useState(false);
    const [fsblExpectedOpen, setFsblExpectedOpen] = useState(false);
    const [wifiExpectedOpen, setWifiExpectedOpen] = useState(false);

    function isValidVer(v: string): boolean {
        return v !== 'N/A' && v !== '' && v !== 'unknown';
    }

    const goBack = () => {
        setCurrentPage(null);
    };

    const getVersions = async () => {
        try {
            setIsLoading(true);
            const result = await getVersionsReq();
            const data = result.data as FirmwareVersions;
            setVersions({
                app: data.app || '',
                web: data.web || '',
                model: data.model || '',
                fsbl: data.fsbl || '',
                wakecore: data.wakecore || '',
                wifi: data.wifi || '',
                wifi_running: data.wifi_running || '',
                expected_fsbl: data.expected_fsbl || '',
                expected_wifi: data.expected_wifi || '',
            });
        } catch (error) {
            console.error('getVersions', error);
            throw error;
        } finally {
            setIsLoading(false);
        }
    };

    useEffect(() => {
        getVersions();
    }, []);

    const handleWifiUpgrade = async () => {
        setWifiUpgradeOpen(false);
        setUpgrading(true);
        try {
            await updateOTAReq({
                filename: 'wifi.bin',
                firmware_type: 'wifi',
                validate_crc32: true,
                validate_signature: true,
                allow_downgrade: true,
                auto_upgrade: true,
            });
        } catch (e) {
            console.error('updateOTAReq (wifi)', e);
        }
        try {
            await restartDevice(
                { delay_seconds: 2 },
                { skipErrorToast: true }
            );
        } catch (e) {
            console.error('restartDevice', e);
        }
        await sleep(8000);
        navigate('/upgrade-waiting');
    };

    const skeleton = () => (
        <div className="flex flex-col gap-2 p-4 rounded-lg">
            <Skeleton className="w-full h-10" />
            <Skeleton className="w-full h-10" />
            <Skeleton className="w-full h-10" />
            <Skeleton className="w-full h-10" />
            <Skeleton className="w-full h-10" />
        </div>
    );

    const showWifiHint = isUpgradableWifi(versions.wifi, versions.wifi_running);
    // When flash and running versions match, compare against the expected version
    // (compiled from version.mk via EXPECTED_WIFI_VERSION).
    const wifiMatched = isValidVer(versions.wifi)
        && isValidVer(versions.wifi_running)
        && versions.wifi === versions.wifi_running;
    const showWifiExpectedHint = wifiMatched
        && isValidVer(versions.expected_wifi)
        && versions.wifi_running !== versions.expected_wifi;
    // FSBL expected version check
    const showFsblExpectedHint = isValidVer(versions.fsbl)
        && isValidVer(versions.expected_fsbl)
        && versions.fsbl !== versions.expected_fsbl;

    const [
        isImportFirmwareDialogOpen,
        setIsImportFirmwareDialogOpen,
    ] = useState(false);
    const [
        isExportFirmwareDialogOpen,
        setIsExportFirmwareDialogOpen,
    ] = useState(false);

    const renderBackSlot = () => {
        if (isMobile) {
            return (
                <div className="flex text-lg font-bold justify-start items-center gap-2">
                    <div onClick={() => goBack()} className="cursor-pointer">
                        <SvgIcon icon="arrow_left" className="w-6 h-6" />
                    </div>
                    <p>
                        {i18n._('sys.system_management.firmware_upgrade')}
                    </p>
                </div>
            );
        }
    };

    return (
        <div>
            {upgrading && (
                <WifiReloadMask
                  isLoading={upgrading}
                  loadingText={i18n._(
                        'sys.system_management.wifi_upgrade_loading'
                    )}
                  maskText={i18n._(
                        'sys.system_management.firmware_upgrade_success'
                    )}
                />
            )}
            {isMobile && renderBackSlot()}
            <p className="text-sm text-text-primary font-bold mt-5 mb-2">
                {i18n._('sys.system_management.firmware_info')}
            </p>
            {isLoading ? (
                skeleton()
            ) : (
                <div className="flex flex-col gap-2 bg-gray-100 p-4 rounded-lg">
                    <div className="flex justify-between mb-2">
                        <Label className="text-sm text-text-primary shrink-0">
                            {i18n._('sys.system_management.web_version')}
                        </Label>
                        <p className="text-sm text-text-primary">
                            {versions.web}
                        </p>
                    </div>
                    <Separator />
                    <div className="flex justify-between mb-2">
                        <Label className="text-sm text-text-primary shrink-0">
                            {i18n._('sys.system_management.app_version')}
                        </Label>
                        <p className="text-sm text-text-primary">
                            {versions.app}
                        </p>
                    </div>
                    <Separator />
                    <div className="flex justify-between mb-2">
                        <Label className="text-sm text-text-primary shrink-0">
                            {i18n._('sys.system_management.ai_model_version')}
                        </Label>
                        <p className="text-sm text-text-primary">
                            {versions.model}
                        </p>
                    </div>
                    <Separator />
                    <div className="flex justify-between mb-2">
                        <Label className="text-sm text-text-primary shrink-0">
                            {i18n._('sys.system_management.fsbl_version')}
                        </Label>
                        <div className="flex items-center gap-1.5">
                            <p className="text-sm text-text-primary">
                                {versions.fsbl}
                            </p>
                            {showFsblExpectedHint && (
                                <span
                                  className="cursor-pointer inline-flex items-center"
                                  onClick={() => setFsblExpectedOpen(true)}
                                  role="button"
                                  tabIndex={0}
                                  onKeyDown={(e) => { if (e.key === 'Enter') setFsblExpectedOpen(true); }}
                                >
                                    <SvgIcon icon="hint" className="w-4 h-4 text-yellow-500" />
                                </span>
                            )}
                        </div>
                    </div>
                    <Separator />
                    <div className="flex justify-between mb-2">
                        <Label className="text-sm text-text-primary shrink-0">
                            {i18n._('sys.system_management.wakeCore_version')}
                        </Label>
                        <p className="text-sm text-text-primary">
                            {versions.wakecore}
                        </p>
                    </div>
                    <Separator />
                    <div className="flex justify-between">
                        <Label className="text-sm text-text-primary shrink-0">
                            {i18n._('sys.system_management.wifi_version')}
                        </Label>
                        <div className="flex items-center gap-1.5">
                            <p className="text-sm text-text-primary">
                                {versions.wifi_running}
                            </p>
                            {showWifiHint && (
                                <span
                                  className="cursor-pointer inline-flex items-center"
                                  onClick={() => setWifiUpgradeOpen(true)}
                                  onKeyDown={(e) => {
                                        if (e.key === 'Enter' || e.key === ' ') {
                                            setWifiUpgradeOpen(true);
                                        }
                                    }}
                                  role="button"
                                  tabIndex={0}
                                  aria-label={i18n._(
                                        'sys.system_management.wifi_upgrade_available_title'
                                    )}
                                >
                                    <SvgIcon
                                      icon="hint"
                                      className="w-4 h-4 text-yellow-500"
                                    />
                                </span>
                            )}
                            {showWifiExpectedHint && (
                                <span
                                  className="cursor-pointer inline-flex items-center"
                                  onClick={() => setWifiExpectedOpen(true)}
                                  role="button"
                                  tabIndex={0}
                                  onKeyDown={(e) => { if (e.key === 'Enter') setWifiExpectedOpen(true); }}
                                >
                                    <SvgIcon icon="hint" className="w-4 h-4 text-orange-500" />
                                </span>
                            )}
                        </div>
                    </div>
                </div>
            )}
            <div className="flex gap-2 justify-end">
                <Button
                  variant="outline"
                  className="mt-4"
                  onClick={() => setIsImportFirmwareDialogOpen(true)}
                >
                    {i18n._('sys.system_management.header_import_firmware')}
                </Button>
                <Button
                  variant="primary"
                  className="mt-4"
                  onClick={() => setIsExportFirmwareDialogOpen(true)}
                >
                    {i18n._('sys.system_management.export_firmware')}
                </Button>
            </div>
            <ImportFirmware
              isImportFirmwareDialogOpen={isImportFirmwareDialogOpen}
              setIsImportFirmwareDialogOpen={
                    setIsImportFirmwareDialogOpen
                }
            />
            <ExportFirmware
              isExportFirmwareDialogOpen={isExportFirmwareDialogOpen}
              setIsExportFirmwareDialogOpen={
                    setIsExportFirmwareDialogOpen
                }
            />

            {/* WiFi upgrade hint dialog */}
            <Dialog
              open={wifiUpgradeOpen}
              onOpenChange={setWifiUpgradeOpen}
            >
                <DialogContent className="md:max-w-md mx-4">
                    <DialogHeader>
                        <div className="flex items-center gap-2">
                            <SvgIcon
                              icon="hint"
                              className="w-5 h-5 text-yellow-500"
                            />
                            <DialogTitle>
                                {i18n._(
                                    'sys.system_management.wifi_upgrade_available_title'
                                )}
                            </DialogTitle>
                        </div>
                        <DialogDescription className="pt-2 text-left flex flex-col gap-1">
                            <span>
                                {i18n._(
                                    'sys.system_management.wifi_upgrade_flash_ver_label'
                                )}
                                ：{versions.wifi}
                            </span>
                            <span>
                                {i18n._(
                                    'sys.system_management.wifi_upgrade_running_ver_label'
                                )}
                                ：{versions.wifi_running}
                            </span>
                            <span className="pt-1">
                                {i18n._(
                                    'sys.system_management.wifi_upgrade_available_desc'
                                )}
                            </span>
                        </DialogDescription>
                    </DialogHeader>
                    <DialogFooter className="mt-4">
                        <Button
                          variant="outline"
                          className="w-1/2 md:w-auto"
                          onClick={() => setWifiUpgradeOpen(false)}
                        >
                            {i18n._('common.cancel')}
                        </Button>
                        <Button
                          variant="primary"
                          className="w-1/2 md:w-auto"
                          onClick={handleWifiUpgrade}
                        >
                            {i18n._(
                                'sys.system_management.wifi_upgrade_start'
                            )}
                        </Button>
                    </DialogFooter>
                </DialogContent>
            </Dialog>

            {/* FSBL expected-version mismatch dialog */}
            <Dialog open={fsblExpectedOpen} onOpenChange={setFsblExpectedOpen}>
                <DialogContent className="md:max-w-md mx-4">
                    <DialogHeader>
                        <div className="flex items-center gap-2">
                            <SvgIcon icon="hint" className="w-5 h-5 text-yellow-500" />
                            <DialogTitle>
                                {i18n._('sys.system_management.fw_mismatch_fsbl_title')}
                            </DialogTitle>
                        </div>
                        <DialogDescription className="pt-2 text-left">
                            {i18n._('sys.system_management.fw_mismatch_fsbl_desc')
                                .replace('{current}', versions.fsbl)
                                .replace('{expected}', versions.expected_fsbl)}
                        </DialogDescription>
                    </DialogHeader>
                    <DialogFooter className="mt-4">
                        <Button variant="outline" className="w-1/2 md:w-auto" onClick={() => setFsblExpectedOpen(false)}>
                            {i18n._('sys.system_management.fw_mismatch_later')}
                        </Button>
                        <Button variant="primary" className="w-1/2 md:w-auto" onClick={() => { setFsblExpectedOpen(false); navigate('/import-fsbl'); }}>
                            {i18n._('sys.system_management.fw_mismatch_upgrade')}
                        </Button>
                    </DialogFooter>
                </DialogContent>
            </Dialog>

            {/* WiFi expected-version mismatch dialog */}
            <Dialog open={wifiExpectedOpen} onOpenChange={setWifiExpectedOpen}>
                <DialogContent className="md:max-w-md mx-4">
                    <DialogHeader>
                        <div className="flex items-center gap-2">
                            <SvgIcon icon="hint" className="w-5 h-5 text-orange-500" />
                            <DialogTitle>
                                {i18n._('sys.system_management.fw_mismatch_wifi_title')}
                            </DialogTitle>
                        </div>
                        <DialogDescription className="pt-2 text-left">
                            {i18n._('sys.system_management.fw_mismatch_wifi_desc')
                                .replace('{current}', versions.wifi_running)
                                .replace('{expected}', versions.expected_wifi)}
                        </DialogDescription>
                    </DialogHeader>
                    <DialogFooter className="mt-4">
                        <Button variant="outline" className="w-1/2 md:w-auto" onClick={() => setWifiExpectedOpen(false)}>
                            {i18n._('sys.system_management.fw_mismatch_later')}
                        </Button>
                        <Button variant="primary" className="w-1/2 md:w-auto" onClick={() => { setWifiExpectedOpen(false); navigate('/import-wifi'); }}>
                            {i18n._('sys.system_management.fw_mismatch_upgrade')}
                        </Button>
                    </DialogFooter>
                </DialogContent>
            </Dialog>
        </div>
    );
}
