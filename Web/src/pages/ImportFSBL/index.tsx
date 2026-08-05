import { useState } from 'preact/hooks';
import Upload from '@/components/upload';
import { useLingui } from '@lingui/react';
import SvgIcon from '@/components/svg-icon';
import systemApis from '@/services/api/system';
import { Button } from '@/components/ui/button';
import { Card, CardContent } from '@/components/ui/card';
import WifiReloadMask from '@/components/wifi-reload-mask';
import { toast } from 'sonner';
import { retryFetch, sleep } from '@/utils';

const ACCEPT_FILE_TYPE = {
    'application/octet-stream': ['.bin'],
};
const MAX_SIZE = 1024 * 1024 * 10;

export default function ImportFSBL() {
    const { i18n } = useLingui();
    const { uploadOTAFileReq, updateOTAReq, restartDevice, getDeviceInfoReq } = systemApis;
    const [fsblFile, setFsblFile] = useState<File | null>(null);
    const [isBurning, setIsBurning] = useState(false);
    const [isRestarting, setIsRestarting] = useState(false);

    // Selecting a file only stores it locally. The actual burn starts on "confirm burn".
    const uploadFSBL = async (file: File) => {
        setFsblFile(file);
    };

    const handleUpdate = async () => {
        if (!fsblFile) {
            toast.error(i18n._('sys.system_management.please_select_firmware_file'));
            return;
        }
        const file = fsblFile;
        try {
            setIsBurning(true);
            setIsRestarting(true);
            // /ota/upload writes the firmware to flash (the actual burn); upgrade-local
            // finalizes it, then restart activates it. All of this runs on "confirm burn".
            await uploadOTAFileReq(file, 'fsbl');
            await updateOTAReq({
                filename: file.name,
                firmware_type: 'fsbl',
                validate_crc32: true,
                validate_signature: true,
                allow_downgrade: true,
                auto_upgrade: true,
            });
            await restartDevice({ delay_seconds: 1 }, { skipErrorToast: true });
            await sleep(8000);
            const result = await retryFetch(
                (signal) => getDeviceInfoReq({ skipErrorToast: true, signal }),
                5000,
                10,
            );
            if (result) {
                setIsBurning(false);
                toast.success(i18n._('sys.system_management.update_success'));
            }
        } catch (error) {
            console.error('handleUpdate', error);
        } finally {
            setIsRestarting(false);
        }
    };

    const renderUploadSlot = () => (
        <div className="flex flex-col gap-3 flex-1 items-center justify-center w-full h-full px-4 py-4 text-center">
            {fsblFile ? (
                <div className="flex flex-col items-center gap-3 pointer-events-none">
                    <div className="w-14 h-14 bg-primary/10 text-primary rounded-lg flex items-center justify-center">
                        <SvgIcon className="w-8 h-8" icon="file" />
                    </div>
                    <p className="text-sm text-text-primary text-wrap break-all max-w-[85%]">{fsblFile.name}</p>
                    <p className="text-xs text-text-secondary">{i18n._('common.reupload')}</p>
                </div>
            ) : (
                <div className="flex flex-col gap-2 items-center justify-center pointer-events-none">
                    <SvgIcon className="w-12 h-12 text-text-secondary" icon="upload_single" />
                    <p className="text-sm text-text-secondary">{i18n._('sys.system_management.fsbl_file')}</p>
                    <p className="text-xs text-text-secondary/70">.bin</p>
                </div>
            )}
        </div>
    );

    return (
        <div className="w-full h-full flex justify-center items-start pt-4">
            {isBurning && (
                <WifiReloadMask
                  isLoading={isRestarting}
                  loadingText={i18n._('sys.system_management.firmware_upgrade_desc')}
                  maskText={i18n._('sys.system_management.network_disconnected')}
                />
            )}
            <Card className="md:max-w-xl mx-4 w-full">
                <CardContent className="flex flex-col gap-4">
                    <div className="flex flex-col gap-1">
                        <p className="text-sm text-text-primary font-bold">{i18n._('sys.system_management.header_import_firmware')}</p>
                        <p className="text-xs text-text-secondary">*{i18n._('sys.system_management.fsbl_file')}</p>
                    </div>

                    <Upload
                      className="h-64 w-full"
                      type="customZone"
                      slot={renderUploadSlot()}
                      accept={ACCEPT_FILE_TYPE}
                      maxFiles={1}
                      maxSize={MAX_SIZE}
                      multiple={false}
                      onFileChange={uploadFSBL}
                    />

                    <div className="flex gap-2 justify-end">
                        <Button
                          variant="primary"
                          className="w-1/2 md:w-auto"
                          disabled={!fsblFile || isBurning}
                          onClick={() => handleUpdate()}
                        >
                            {i18n._('sys.system_management.confirm_burn')}
                        </Button>
                    </div>
                </CardContent>
            </Card>
        </div>
    );
}
