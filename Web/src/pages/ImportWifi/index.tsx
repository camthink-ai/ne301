import { useState } from 'preact/hooks';
import Upload from '@/components/upload';
import { useLingui } from '@lingui/react';
import SvgIcon from '@/components/svg-icon';
import systemApis from '@/services/api/system';
import { Button } from '@/components/ui/button';
import { toast } from 'sonner';
import WifiReloadMask from '@/components/wifi-reload-mask';
import { sleep, sliceFile } from '@/utils';
import { useNavigate } from 'react-router-dom';
import {
  Dialog,
  DialogContent,
  DialogHeader,
  DialogTitle,
  DialogFooter,
  DialogDescription,
} from '@/components/dialog';

// Total time to wait for the device to come back after a WiFi upgrade restart.
// WiFi firmware load into the SiWG917 chip takes minutes; the frontend only
// waits briefly, then redirects to the LED-guidance page so the user can watch
// the device LED instead of staring at a spinner.
const UPGRADE_WAIT_TOTAL_MS = 60 * 1000; // 60 seconds
const UPGRADE_WAIT_INTERVAL_MS = 5 * 1000; // 5s between polls
const UPGRADE_POLL_TIMEOUT_MS = 5 * 1000; // per-attempt abort (fail fast)

export default function ImportWifi() {
    const { i18n } = useLingui();
    const {
        uploadOTAFileReq,
        preCheckReq,
        updateOTAReq,
        restartDevice,
        getDeviceInfoReq,
    } = systemApis;
    const navigate = useNavigate();
    const [wifiFile, setWifiFile] = useState<File | null>(null);
    const [uploading, setUploading] = useState(false);
    const [upgrading, setUpgrading] = useState(false);
    const [isConfirmOpen, setIsConfirmOpen] = useState(false);

    const acceptFileType = {
        'application/octet-stream': ['.bin'],
    };

    // Upload + apply the WiFi firmware package. The streaming /ota/upload
    // endpoint writes the flash image (flash_header_t + .rps) to WIFI_FW_BASE
    // *during* upload and marks a pending WiFi update — so when this resolves
    // the firmware is already in flash; the device just needs a reboot to push
    // it to the SiWG917 chip, which "确认升级" triggers.
    const uploadWifi = async (file: File) => {
        try {
            setUploading(true);
            const contentPreview = await sliceFile(file, 2048);
            if (!contentPreview.size) {
                throw new Error(
                    i18n._('sys.system_management.invalid_firmware_file')
                        || 'Invalid firmware file'
                );
            }
            await preCheckReq(contentPreview, 'wifi');
            await uploadOTAFileReq(file, 'wifi');
            // NOTE: the wifi-mode update flag is NOT set here; the streaming
            // upload only writes the flash image (flash_header_t + .rps) to
            // WIFI_FW_BASE.  The flag is set later via updateOTAReq in the
            // confirm-upgrade step so the user can decide when to apply.
            setWifiFile(file);
            toast.success(
                i18n._('sys.system_management.wifi_written_pending_upgrade')
            );
        } catch (error) {
            setWifiFile(null);
            toast.error(
                error instanceof Error ? error.message : String(error)
            );
            console.error('uploadWifi', error);
        } finally {
            setUploading(false);
        }
    };

    // Poll the device until it responds again, with a hard timeout.
    // Each attempt is aborted quickly so a hung TCP connection (device
    // mid-reboot) does not stall a poll for the full axios timeout.
    const waitForDeviceBack = async (): Promise<boolean> => {
        /* eslint-disable no-await-in-loop */
        const deadline = Date.now() + UPGRADE_WAIT_TOTAL_MS;
        while (Date.now() < deadline) {
            const controller = new AbortController();
            const timer = setTimeout(
                () => controller.abort(),
                UPGRADE_POLL_TIMEOUT_MS
            );
            try {
                await getDeviceInfoReq({
                    skipErrorToast: true,
                    signal: controller.signal,
                });
                clearTimeout(timer);
                return true;
            } catch {
                clearTimeout(timer);
                // device still rebooting / pushing WiFi firmware, keep waiting
            }
            await sleep(UPGRADE_WAIT_INTERVAL_MS);
        }
        /* eslint-enable no-await-in-loop */
        return false;
    };

    const handleUpgrade = async () => {
        setIsConfirmOpen(false);
        setUpgrading(true);
        // 1. Tell the backend to set NVS wifi_mode=update.  The streaming upload
        //    already wrote the flash image to WIFI_FW_BASE; this flag makes the
        //    next reboot run firmware_upgrade_from_flash() to push the .rps to
        //    the SiWG917 chip.
        try {
            await updateOTAReq({
                filename: wifiFile?.name || 'wifi.bin',
                firmware_type: 'wifi',
                validate_crc32: true,
                validate_signature: true,
                allow_downgrade: true,
                auto_upgrade: true,
            });
        } catch (e) {
            console.error('updateOTAReq (wifi)', e);
        }
        // 2. Restart.  The device usually resets before sending the HTTP
        //    response, so swallow the transport error.
        try {
            await restartDevice(
                { delay_seconds: 2 },
                { skipErrorToast: true }
            );
        } catch (e) {
            console.error('restartDevice', e);
        }
        try {
            await sleep(8000);
            const back = await waitForDeviceBack();
            if (back) {
                toast.success(
                    i18n._('sys.system_management.firmware_upgrade_success')
                );
                navigate('/device-tool');
            } else {
                // Still unreachable after the 60-min wait window — guide the
                // user to check the device LED and reconnect manually.
                navigate('/upgrade-waiting');
            }
        } catch (error) {
            console.error('handleUpgrade', error);
            navigate('/upgrade-waiting');
        } finally {
            setUpgrading(false);
        }
    };

    const customUpload = ({
        placeholder,
        fileName,
    }: {
        placeholder: string;
        fileName: string;
    }) => (
        <div className="flex flex-col gap-2 flex-1 items-center justify-center w-full h-full rounded-md">
            {fileName ? (
                <div className="flex flex-col items-center gap-2">
                    <div className="w-14 h-14 bg-gray-400 rounded-md flex items-center justify-center">
                        <SvgIcon className="w-10 h-10" icon="file" />
                    </div>
                    <p className="text-sm items-center text-wrap text-text-primary">
                        {fileName}
                    </p>
                </div>
            ) : (
                <div className="flex flex-col gap-2 items-center justify-center w-full h-full">
                    <SvgIcon className="w-10 h-10" icon="upload_single" />
                    <p className="text-sm items-center text-wrap text-text-secondary">
                        {placeholder}
                    </p>
                </div>
            )}
        </div>
    );

    return (
        <div className="w-full h-full flex justify-center pt-4">
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
            <div className="md:max-w-xl mx-4 w-full flex flex-col gap-2">
                <Upload
                  className="h-50"
                  type="customZone"
                  slot={customUpload({
                        placeholder: i18n._(
                            'sys.system_management.wifi_file'
                        ),
                        fileName: wifiFile?.name || '',
                    })}
                  accept={acceptFileType}
                  maxFiles={1}
                  maxSize={1024 * 1024 * 10}
                  multiple={false}
                  onFileChange={uploadWifi}
                  loading={uploading}
                />

                <div className="flex gap-2 justify-end">
                    <Button
                      variant="primary"
                      className="w-1/2 md:w-auto"
                      onClick={() => setIsConfirmOpen(true)}
                      disabled={uploading || upgrading || !wifiFile}
                    >
                        {i18n._('sys.system_management.confirm_upgrade')}
                    </Button>
                </div>
            </div>

            {/* Secondary confirmation before the reboot/upgrade. */}
            <Dialog open={isConfirmOpen} onOpenChange={setIsConfirmOpen}>
                <DialogContent className="md:max-w-md mx-4" showCloseButton={false}>
                    <DialogHeader>
                        <div className="flex items-center gap-2">
                            <SvgIcon
                              icon="hint"
                              className="w-6 h-6 text-destructive"
                            />
                            <DialogTitle>
                                {i18n._(
                                    'sys.system_management.upgrade_confirm_title'
                                )}
                            </DialogTitle>
                        </div>
                        <DialogDescription className="pt-2 text-left whitespace-pre-line">
                            {i18n._(
                                'sys.system_management.upgrade_confirm_warning'
                            )}
                        </DialogDescription>
                    </DialogHeader>
                    <DialogFooter className="mt-4">
                        <Button
                          variant="outline"
                          className="w-1/2 md:w-auto"
                          onClick={() => setIsConfirmOpen(false)}
                          disabled={upgrading}
                        >
                            {i18n._('common.cancel')}
                        </Button>
                        <Button
                          variant="destructive"
                          className="w-1/2 md:w-auto"
                          onClick={handleUpgrade}
                          disabled={upgrading}
                        >
                            {i18n._('sys.system_management.confirm_upgrade')}
                        </Button>
                    </DialogFooter>
                </DialogContent>
            </Dialog>
        </div>
    );
}
