import { useRef, useState } from 'preact/hooks';
import {
  Dialog,
  DialogContent,
  DialogHeader,
  DialogTitle,
  DialogFooter,
} from '@/components/dialog';
import { Button } from '@/components/ui/button';
import { useLingui } from '@lingui/react';
import Upload from '@/components/upload';
import SvgIcon from '@/components/svg-icon';
import systemApis, { type FirmwareType } from '@/services/api/system';
import { toast } from 'sonner';
import WifiReloadMask from '@/components/wifi-reload-mask';
import { retryFetch, sleep, sliceFile } from '@/utils';
import { ScrollArea } from '@/components/ui/scroll-area';
import { useNavigate } from 'react-router-dom';
import {
  Popover,
  PopoverContent,
  PopoverTrigger,
} from '@/components/ui/popover';

type ImportFirmwareProps = {
  isImportFirmwareDialogOpen: boolean;
  setIsImportFirmwareDialogOpen: (open: boolean) => void;
};
export default function ImportFirmware({
  isImportFirmwareDialogOpen,
  setIsImportFirmwareDialogOpen,
}: ImportFirmwareProps) {
  const { i18n } = useLingui();
  const {
    uploadOTAFileReq,
    preCheckReq,
    updateOTAReq,
    restartDevice,
    uploadDeviceFileReq,
    getDeviceInfoReq,
  } = systemApis;
  const [appFile, setAppFile] = useState<File | null>(null);
  const [webFile, setWebFile] = useState<File | null>(null);
  const [aiModelFile, setAiModelFile] = useState<File | null>(null);
  const [deviceFile, setDeviceFile] = useState<File | null>(null);
  const [isAdvancedMenuOpen, setIsAdvancedMenuOpen] = useState(false);
  const navigate = useNavigate();
  // const [updateLoadingValue, setUpdateLoadingValue] = useState(10);
  const [isUpdateLoading, setIsUpdateLoading] = useState(false);
  const [restartLoading, setRestartLoading] = useState(false);
  const [uploadLoadings, setUploadLoadings] = useState({
    app: false,
    web: false,
    ai: false,
    // fsbl: false,
    device: false,
  });
  type UploadCategory = keyof Pick<typeof uploadLoadings, 'app' | 'web' | 'ai'>;
  const uploadQueueRef = useRef<Promise<void>>(Promise.resolve());

  const acceptFileType = {
    // Only accept .bin firmware files
    'application/octet-stream': ['.bin'],
  };
  // Compatible with different browsers/systems' MIME declarations for JSON
  const acceptDeviceFileType = {
    'application/json': ['.json'],
  };

  const uploadBtnSlot = (
    <>
      <SvgIcon icon="upload" />
      {i18n._('common.reupload')}
    </>
  );
  const customUpload = ({
    placeholder,
    fileName,
    type,
  }: {
    placeholder: string;
    fileName: string;
    type: string;
  }) => (
    <div className="flex flex-col gap-2 flex-1 items-center justify-center w-full h-full rounded-md">
      {fileName ? (
        <div className="flex flex-col items-center justify-center gap-2 h-full">
          <div className="flex flex-col items-center gap-2">
            <div className="w-14 h-14 bg-gray-400 rounded-md flex items-center justify-center">
              <SvgIcon className="w-10 h-10" icon="file" />
            </div>
            <p className="text-sm items-center  text-wrap text-text-primary">
              {fileName}
            </p>
          </div>
          <div className="absolute bottom-2 left-1/2 -translate-x-1/2 flex gap-2">
            <Upload
              onFileChange={(file: File) => onFileChange(file, type)}
              slot={uploadBtnSlot}
              className="flex flex-1 h-full justify-start"
              type="button"
              accept={acceptFileType}
              maxFiles={1}
              maxSize={1024 * 1024 * 10}
              multiple={false}
            />
          </div>
        </div>
      ) : (
        <div className="w-full relative flex-1 py-8 flex flex-col items-center justify-center pointer-events-none">
          <div className="w-16 mb-2">
            <SvgIcon className="w-10 h-10" icon="upload_single" />
          </div>
          <p className="text-sm text-text-secondary">{placeholder}</p>
        </div>
      )}
    </div>
  );
  const uploadOTAs = async (
    file: File,
    type: UploadCategory
  ): Promise<boolean> => {
    try {
      const contentPreview = await sliceFile(file, 2048);
      if (!contentPreview.size) {
        throw new Error(
          i18n._('sys.system_management.invalid_firmware_file')
          || 'Invalid firmware file'
        );
      }
      await preCheckReq(contentPreview, type as FirmwareType);
      await uploadOTAFileReq(file, type as FirmwareType);
      await updateOTAReq({
        filename: file.name,
        firmware_type: type,
        validate_crc32: true,
        validate_signature: true,
        allow_downgrade: true,
        auto_upgrade: true,
      });
      if (type === 'app') {
        setAppFile(file);
      } else if (type === 'web') {
        setWebFile(file);
      } else if (type === 'ai') {
        setAiModelFile(file);
      }
      return true;
    } catch {
      return false;
    } finally {
      setUploadLoadings(prev => ({ ...prev, [type]: false }));
    }
  };
  const enqueueUpload = (
    task: (type: UploadCategory) => Promise<boolean>,
    type: UploadCategory
  ) => {
    setUploadLoadings(prev => ({ ...prev, [type as UploadCategory]: true }));
    uploadQueueRef.current = uploadQueueRef.current
      .catch(() => undefined)
      .then(() => task(type as UploadCategory).catch(error => {
        toast.error(error instanceof Error ? error.message : String(error));
        throw error;
      }))
      .then(() => setUploadLoadings(prev => ({
        ...prev,
        [type as UploadCategory]: false,
      })));
    return uploadQueueRef.current;
  };
  const handleUpdate = async () => {
    try {
      if (!appFile && !webFile && !aiModelFile && !deviceFile) {
        toast.error(
          i18n._('sys.system_management.please_select_firmware_file')
        );
        return;
      }
      for (const key in uploadLoadings) {
        if (uploadLoadings[key as keyof typeof uploadLoadings]) {
          toast.error(i18n._('sys.system_management.firmware_file_uploading'));
          return;
        }
      }

      setIsUpdateLoading(true);
      setIsImportFirmwareDialogOpen(false);
      setRestartLoading(true);
      await restartDevice({ delay_seconds: 2 }, { skipErrorToast: true });
      await sleep(8000);
      const result = await retryFetch(
        (signal) => getDeviceInfoReq({ skipErrorToast: true, signal }),
        5000,
        10
      );

      if (result) {
        setIsUpdateLoading(false);
        toast.success(i18n._('sys.system_management.update_success'));
      }
    } finally {
      setRestartLoading(false);
    }
  };
  const onAppFileChange = (file: File) => {
    enqueueUpload(type => uploadOTAs(file, type), 'app');
  };
  const onWebFileChange = (files: File) => {
    enqueueUpload(type => uploadOTAs(files as File, type), 'web');
  };
  const onAIFileChange = (files: File) => {
    enqueueUpload(type => uploadOTAs(files, type), 'ai');
  };
  const onDeviceFileChange = (files: File) => {
    uploadDeviceFile(files);
  };
  const onFileChange = (file: File, type: string) => {
    if (type === 'app') {
      onAppFileChange(file);
    } else if (type === 'web') {
      onWebFileChange(file);
    } else if (type === 'ai') {
      onAIFileChange(file);
    } else if (type === 'device') {
      uploadDeviceFile(file);
    }
  };
  const uploadDeviceFile = async (file: File) => {
    try {
      setUploadLoadings(prev => ({ ...prev, device: true }));
      // Read text and validate JSON
      const text = await file.text();
      const data = JSON.parse(text);
      await uploadDeviceFileReq(data);
      setDeviceFile(file);
      return true;
    } catch {
      return false;
    } finally {
      setUploadLoadings(prev => ({ ...prev, device: false }));
    }
  };

  return (
    <div>
      {isUpdateLoading && (
        <WifiReloadMask
          isLoading={restartLoading}
          loadingText={i18n._('sys.system_management.firmware_upgrade_desc')}
          maskText={i18n._('sys.system_management.firmware_upgrade_success')}
        />
      )}
      <Dialog
        open={isImportFirmwareDialogOpen}
        onOpenChange={setIsImportFirmwareDialogOpen}
      >
        <DialogContent className="md:max-w-4xl mx-4">
          <DialogHeader>
            <DialogTitle>
              {i18n._('sys.system_management.header_import_firmware')}
            </DialogTitle>
            <ScrollArea className="h-[70vh] md:h-auto pt-2">
              <div className="flex flex-col gap-2">
                <p className="text-sm mt-2 self-start">
                  *{i18n._('sys.system_management.firmware_file')}
                </p>
                <div className="h-full w-full flex md:grid md:grid-cols-2 flex-col flex-1 justify-center items-center gap-4 mb-4">
                  <Upload
                    className="h-50 w-full"
                    type="customZone"
                    slot={customUpload({
                      placeholder: i18n._('sys.system_management.app_file'),
                      fileName: appFile?.name || '',
                      type: 'app',
                    })}
                    accept={acceptFileType}
                    maxFiles={1}
                    maxSize={1024 * 1024 * 10}
                    multiple={false}
                    onFileChange={onAppFileChange}
                    loading={uploadLoadings.app}
                  />
                  <Upload
                    className="h-50  w-full"
                    type="customZone"
                    slot={customUpload({
                      placeholder: i18n._('sys.system_management.web_file'),
                      fileName: webFile?.name || '',
                      type: 'web',
                    })}
                    accept={acceptFileType}
                    maxFiles={1}
                    maxSize={1024 * 1024 * 10}
                    multiple={false}
                    onFileChange={onWebFileChange}
                    loading={uploadLoadings.web}
                  />
                  <Upload
                    className="h-50 w-full"
                    type="customZone"
                    slot={customUpload({
                      placeholder: i18n._(
                        'sys.system_management.ai_model_file'
                      ),
                      fileName: aiModelFile?.name || '',
                      type: 'ai',
                    })}
                    accept={acceptFileType}
                    maxFiles={1}
                    maxSize={1024 * 1024 * 10}
                    multiple={false}
                    onFileChange={onAIFileChange}
                    loading={uploadLoadings.ai}
                  />
                  <Upload
                    className="h-50 w-full"
                    type="customZone"
                    slot={customUpload({
                      placeholder: i18n._('sys.system_management.device_file'),
                      fileName: deviceFile?.name || '',
                      type: 'device',
                    })}
                    accept={acceptDeviceFileType}
                    maxFiles={1}
                    maxSize={1024 * 1024 * 50}
                    multiple={false}
                    onFileChange={onDeviceFileChange}
                    loading={uploadLoadings.device}
                  />
                </div>
              </div>
            </ScrollArea>
          </DialogHeader>
          <DialogFooter className="mt-4 justify-between">
            <Popover
              open={isAdvancedMenuOpen}
              onOpenChange={setIsAdvancedMenuOpen}
            >
              <PopoverTrigger asChild>
                <div className="inline-flex">
                  <Button variant="outline" className="md:w-auto gap-1">
                    {i18n._('sys.system_management.advanced_options')}
                    <SvgIcon icon="bottom" className="w-4 h-4" />
                  </Button>
                </div>
              </PopoverTrigger>
              <PopoverContent
                className="w-auto p-1"
                align="start"
                side="top"
              >
                <button
                  type="button"
                  className="flex w-full items-center rounded-sm px-3 py-2 text-sm hover:bg-gray-100 dark:hover:bg-gray-800"
                  onClick={() => {
                    setIsAdvancedMenuOpen(false);
                    setIsImportFirmwareDialogOpen(false);
                    navigate('/import-fsbl');
                  }}
                >
                  {i18n._('sys.system_management.advanced_fsbl_upgrade')}
                </button>
                <button
                  type="button"
                  className="flex w-full items-center rounded-sm px-3 py-2 text-sm hover:bg-gray-100 dark:hover:bg-gray-800"
                  onClick={() => {
                    setIsAdvancedMenuOpen(false);
                    setIsImportFirmwareDialogOpen(false);
                    navigate('/import-wifi');
                  }}
                >
                  {i18n._('sys.system_management.advanced_wifi_upgrade')}
                </button>
              </PopoverContent>
            </Popover>
            <div className="flex flex-row space-x-2">
              <Button
                variant="outline"
                className="w-1/2 md:w-auto"
                onClick={() => setIsImportFirmwareDialogOpen(false)}
              >
                {i18n._('common.cancel')}
              </Button>
              <Button
                variant="primary"
                className="w-1/2 md:w-auto"
                onClick={() => handleUpdate()}
              >
                {i18n._('sys.system_management.confirm_burn')}
              </Button>
            </div>
          </DialogFooter>
        </DialogContent>
      </Dialog>
    </div>
  );
}
