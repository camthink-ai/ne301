import { useState, useEffect } from 'preact/hooks';
import { Outlet, useNavigate } from 'react-router-dom';

import Menu from './pc/menu';
import SvgIcon from '@/components/svg-icon';
import ActionButtons from './pc/action-buttons';
import { Button } from '@/components/ui/button';

import { useAuthStore } from '@/store/auth';
import NavRight from './mobile/nav-right';
import { Link } from 'react-router-dom';
import DeviceInfo from './pc/deviceInfo';
import { useIsMobile } from '@/hooks/use-mobile';
import Log from './log';
import systemApis from '@/services/api/system';
import { useLingui } from '@lingui/react';
import {
    Dialog,
    DialogContent,
    DialogHeader,
    DialogTitle,
    DialogFooter,
    DialogDescription,
} from '@/components/dialog';

interface VersionCheckResult {
    fsbl_mismatch: boolean;
    wifi_mismatch: boolean;
    expected_fsbl: string;
    current_fsbl: string;
    expected_wifi: string;
    current_wifi: string;
}

export default function Layout() {
    const { isValidateToken } = useAuthStore();
    const [showMenu, setShowMenu] = useState(false);
    const isMobile = useIsMobile();
    const [isOpen, setIsOpen] = useState(false);
    const navigate = useNavigate();
    const { i18n } = useLingui();
    const { versionCheckReq } = systemApis;

    // --- firmware version mismatch dialogs (FSBL first, then WiFi) ---
    const [fsblCheck, setFsblCheck] = useState<VersionCheckResult | null>(null);
    const [wifiCheck, setWifiCheck] = useState<VersionCheckResult | null>(null);
    const [showFsblDialog, setShowFsblDialog] = useState(false);
    const [showWifiDialog, setShowWifiDialog] = useState(false);

    useEffect(() => {
        if (!isValidateToken) return;
        if (sessionStorage.getItem('_vc_checked')) return;
        versionCheckReq()
            .then((res: any) => {
                sessionStorage.setItem('_vc_checked', '1');
                const data = res.data as VersionCheckResult;
                setFsblCheck(data);
                setWifiCheck(data);
                if (data.fsbl_mismatch) {
                    setShowFsblDialog(true);
                } else if (data.wifi_mismatch) {
                    setShowWifiDialog(true);
                }
            })
            .catch(() => { /* version-check endpoint not available yet */ });
    }, [isValidateToken]);

    const handleFsblDismiss = () => {
        setShowFsblDialog(false);
        if (fsblCheck?.wifi_mismatch) setShowWifiDialog(true);
    };

    const handleWifiDismiss = () => setShowWifiDialog(false);
  const handleOpenLogs = () => {
    setShowMenu(false)
    setIsOpen(true)
  }
  return (
    <div className="h-screen w-screen bg-gray-100 relative flex flex-col">
      <header
        className={`${isValidateToken ? 'bg-white ' : 'bg-gray-100'}  relative z-11`}
      >
        <div className="w-full mx-auto px-4 sm:px-6 lg:px-8">
          <div className="flex justify-between h-16">
            {/* Left: Logo and device information */}
            <div className="flex items-center space-x-6">
              {/* Cam Think Logo */}
              <div className="flex items-center">
                <div className="relative h-full">
                  <Link to="/device-tool">
                    <SvgIcon className="w-25 h-14" icon="CamthinkLogo" />
                  </Link>
                </div>
              </div>
              {isValidateToken && (
                <div className="w-[1px] h-8 bg-gray-200"></div>
              )}
              {/* Device information */}
              {isValidateToken && <DeviceInfo />}
            </div>
            {/* Right: function buttons */}
            <div className="flex items-center space-x-3">
              {/* Import button - hidden on mobile */}
              {!isMobile && (
                <div className="md:flex items-center space-x-2">
                  <ActionButtons />
                </div>
              )}

              {isMobile
                && (
                  <div>
                    <Button
                      variant="ghost"
                      size="icon"
                      onClick={() => setShowMenu(true)}
                    >
                      <SvgIcon className="!w-8 !h-8" icon="menu" />
                    </Button>
                    {showMenu && <NavRight onClose={() => setShowMenu(false)} handleOpenLogs={handleOpenLogs} />}
                    <Log isOpen={isOpen} setOpen={setIsOpen} />
                  </div>
                )}
            </div>
          </div>
        </div>
      </header>

      {/* Navigation menu */}
      {isValidateToken && <Menu />}
      <main className="flex-1 overflow-auto w-full relative h-full">
        <Outlet />
      </main>

      {/* FSBL version mismatch dialog — shown first (FSBL takes priority) */}
      <Dialog open={showFsblDialog} onOpenChange={handleFsblDismiss}>
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
                .replace('{current}', fsblCheck?.current_fsbl || '?')
                .replace('{expected}', fsblCheck?.expected_fsbl || '?')}
            </DialogDescription>
          </DialogHeader>
          <DialogFooter className="mt-4">
            <Button variant="outline" className="w-1/2 md:w-auto" onClick={handleFsblDismiss}>
              {i18n._('sys.system_management.fw_mismatch_later')}
            </Button>
            <Button variant="primary" className="w-1/2 md:w-auto" onClick={() => { setShowFsblDialog(false); navigate('/import-fsbl'); }}>
              {i18n._('sys.system_management.fw_mismatch_upgrade')}
            </Button>
          </DialogFooter>
        </DialogContent>
      </Dialog>

      {/* WiFi version mismatch dialog — shown after FSBL dialog is dismissed */}
      <Dialog open={showWifiDialog} onOpenChange={handleWifiDismiss}>
        <DialogContent className="md:max-w-md mx-4">
          <DialogHeader>
            <div className="flex items-center gap-2">
              <SvgIcon icon="hint" className="w-5 h-5 text-yellow-500" />
              <DialogTitle>
                {i18n._('sys.system_management.fw_mismatch_wifi_title')}
              </DialogTitle>
            </div>
            <DialogDescription className="pt-2 text-left">
              {i18n._('sys.system_management.fw_mismatch_wifi_desc')
                .replace('{current}', wifiCheck?.current_wifi || '?')
                .replace('{expected}', wifiCheck?.expected_wifi || '?')}
            </DialogDescription>
          </DialogHeader>
          <DialogFooter className="mt-4">
            <Button variant="outline" className="w-1/2 md:w-auto" onClick={handleWifiDismiss}>
              {i18n._('sys.system_management.fw_mismatch_later')}
            </Button>
            <Button variant="primary" className="w-1/2 md:w-auto" onClick={() => { setShowWifiDialog(false); navigate('/import-wifi'); }}>
              {i18n._('sys.system_management.fw_mismatch_upgrade')}
            </Button>
          </DialogFooter>
        </DialogContent>
      </Dialog>
    </div>
  );
}
