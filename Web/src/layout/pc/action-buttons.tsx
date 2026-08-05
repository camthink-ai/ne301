import { useState } from 'preact/hooks';
import {
  Select,
  SelectContent,
  SelectItem,
  SelectTrigger,
  SelectValue,
} from '@/components/ui/select'
import { Link } from 'react-router-dom'
import { type LocaleType } from '@/locales'
import { useLanguage } from '@/hooks/useLanguage'
import SvgIcon from '@/components/svg-icon'
import { useAuthStore } from '@/store/auth';
import { useLingui } from '@lingui/react';
import { toast } from 'sonner';

import { Button } from "@/components/ui/button"
import {
  Dialog,
  DialogContent,
  DialogDescription,
  DialogFooter,
  DialogHeader,
  DialogTitle,
} from '@/components/dialog';
import systemSettings from '@/services/api/systemSettings';
import Log from '../log'

export default function ActionButtons() {
  const { i18n } = useLingui();
  const { locale, setLocale } = useLanguage()
  const { isValidateToken } = useAuthStore();
  const { restartDevice } = systemSettings;
  const handleLanguageChange = (value: string) => {
    setLocale(value as LocaleType);
  }
  const [isOpen, setIsOpen] = useState(false);
  const [isRestartDialogOpen, setIsRestartDialogOpen] = useState(false);
  const [restartLoading, setRestartLoading] = useState(false);

  const handleRestartDevice = async () => {
    try {
      setRestartLoading(true);
      await restartDevice({ delaySeconds: 2 });
      setIsRestartDialogOpen(false);
      toast.success(i18n._('sys.system_management.device_restarting'));
    } catch (error) {
      // eslint-disable-next-line no-console
      console.error(error);
    } finally {
      setRestartLoading(false);
    }
  };

  return (
    <>
      {isValidateToken && (
        <div className="flex items-center gap-2">
          <Button
            variant="outline"
            size="icon"
            className="w-9 h-9 shrink-0"
            title={i18n._('sys.header.restart_device')}
            onClick={() => setIsRestartDialogOpen(true)}
          >
            <SvgIcon icon="restart" className="w-4 h-4" />
          </Button>

          <Link
            to="https://wiki.camthink.ai/docs/neoeyes-ne301-series/overview"
            target="_blank"
            className="inline-flex shrink-0"
          >
            <Button variant="outline" size="icon" className="w-9 h-9">
              <SvgIcon icon="hint" className="w-4 h-4" />
            </Button>
          </Link>

          <Link to="https://github.com/camthink-ai" target="_blank" className="inline-flex shrink-0">
            <Button variant="outline" size="icon" className="w-9 h-9">
              <SvgIcon icon="github" className="w-4 h-4" />
            </Button>
          </Link>

          <div className="w-px h-8 bg-gray-200 shrink-0" />
        </div>
      )}

      <Select value={locale} onValueChange={handleLanguageChange}>
        <SelectTrigger className="w-[120px] bg-white">
          <SelectValue />
        </SelectTrigger>
        <SelectContent>
          <SelectItem value="zh">简体中文</SelectItem>
          <SelectItem value="en">English</SelectItem>
        </SelectContent>
      </Select>

      {isValidateToken && (
        <>
          <Button
            variant="outline"
            size="icon"
            className="w-9 h-9 bg-white shrink-0"
            onClick={() => setIsOpen(true)}
          >
            <SvgIcon className="w-4 h-4 text-text-primary" icon="logs" />
          </Button>
          <Log isOpen={isOpen} setOpen={setIsOpen} />
        </>
      )}

      <Dialog open={isRestartDialogOpen} onOpenChange={setIsRestartDialogOpen}>
        <DialogContent>
          <DialogHeader>
            <DialogTitle>{i18n._('sys.header.restart_device_confirm_title')}</DialogTitle>
          </DialogHeader>
          <DialogDescription className="text-sm text-text-primary my-4">
            {i18n._('sys.header.restart_device_confirm')}
          </DialogDescription>
          <DialogFooter>
            <Button variant="outline" onClick={() => setIsRestartDialogOpen(false)} disabled={restartLoading}>
              {i18n._('common.cancel')}
            </Button>
            <Button variant="primary" onClick={() => handleRestartDevice()} disabled={restartLoading}>
              {i18n._('common.confirm')}
            </Button>
          </DialogFooter>
        </DialogContent>
      </Dialog>
    </>
  )
}
