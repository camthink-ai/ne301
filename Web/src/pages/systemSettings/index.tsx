import { useState, useEffect } from 'react';
import { Card, CardContent } from '@/components/ui/card';
import { Tabs, TabsList, TabsTrigger, TabsContent } from '@/components/ui/tabs';
import { Label } from '@/components/ui/label';
import { Separator } from '@/components/ui/separator';
import Communication from './communication/index';
// import LocalBluetooth from './local-bluetooth';
import DevicePassword from './device-password';
import FirmwareUpgrade from './firmware-upgrade';
import { useLingui } from '@lingui/react';
import SvgIcon from '@/components/svg-icon';
import { useIsMobile } from '@/hooks/use-mobile';
import { useCommunicationData } from '@/store/communicationData';

export default function ApplicationManagement() {
  const { i18n } = useLingui();
  const isMobile = useIsMobile();

  const { getCommunicationData, communicationData } = useCommunicationData();

  useEffect(() => {
    if (isMobile) {
      getCommunicationData();
    }
  }, []);

  const [currentPage, setCurrentPage] = useState<string | null>(null);
  const [isAnimating, setIsAnimating] = useState(false);
  const [direction, setDirection] = useState<'left' | 'right'>('right');

  const goMbPage = (page: string) => {
    setDirection('left');
    setIsAnimating(true);
    setTimeout(() => {
      setCurrentPage(page);
      setIsAnimating(false);
    }, 150);
  }

  const goBack = () => {
    setDirection('right');
    setIsAnimating(true);
    setTimeout(() => {
      setCurrentPage(null);
      setIsAnimating(false);
    }, 150);
  }

  const renderPage = () => {
    switch (currentPage) {
      case 'communication':
        return <Communication setCurrentPage={goBack} />;
      // case 'local-bluetooth':
      //   return <LocalBluetooth setCurrentPage={goBack} />;
      case 'device-password':
        return <DevicePassword setCurrentPage={goBack} />;
      case 'firmware-upgrade':
        return <FirmwareUpgrade setCurrentPage={goBack} />;
      default:
        return null;
    }
  }
  const getSelectedCommunicationTypeLabel = (type?: string) => {
    switch (type) {
      case 'wifi':
        return i18n._('sys.system_management.wifi');
      case 'halow':
        return i18n._('sys.system_management.halow');
      case 'cellular':
        return i18n._('sys.system_management.cellular_network');
      case 'poe':
        return i18n._('sys.system_management.poe_network');
      default:
        return i18n._('sys.system_management.wifi');
    }
  };

  const mobilePage = () => (
    <div className="flex justify-center pt-4">
      <Card className="sm:w-xl w-full mx-4">
        <CardContent>
          <div
            onClick={() => goMbPage('communication')}
            onKeyDown={(e) => e.key === 'Enter' && goMbPage('communication')}
            className="flex justify-between items-center h-12 cursor-pointer"
            tabIndex={0}
            role="button"
          >
            <Label>{i18n._('sys.system_management.communication_title')}</Label>
            <div className="flex items-center gap-2">
              {getSelectedCommunicationTypeLabel(communicationData?.selected_type)}
              <SvgIcon icon="right" className="w-4 h-4" />
            </div>
          </div>
          {/* <Separator />
          <div
            onClick={() => goMbPage('local-bluetooth')}
            onKeyDown={(e) => e.key === 'Enter' && goMbPage('local-bluetooth')}
            tabIndex={0}
            role="button"
            className="flex justify-between items-center h-12"
          >
            <Label>{i18n._('sys.system_management.bluetooth_title')}</Label>
            <div className="flex items-center gap-2">
              Normal
              <SvgIcon icon="right" className="w-4 h-4" />
            </div>
          </div> */}
          <Separator />
          <div
            onClick={() => goMbPage('device-password')}
            onKeyDown={(e) => e.key === 'Enter' && goMbPage('device-password')}
            tabIndex={0}
            role="button"
            className="flex justify-between items-center h-12"
          >
            <Label>{i18n._('sys.system_management.device_password')}</Label>
            <div className="flex items-center gap-2">

              <SvgIcon icon="right" className="w-4 h-4" />
            </div>
          </div>
          <Separator />
          <div
            onClick={() => goMbPage('firmware-upgrade')}
            onKeyDown={(e) => e.key === 'Enter' && goMbPage('firmware-upgrade')}
            tabIndex={0}
            role="button"
            className="flex justify-between items-center h-12"
          >
            <Label>{i18n._('sys.system_management.firmware_upgrade')}</Label>
            <div className="flex items-center gap-2">
              No update
              <SvgIcon icon="right" className="w-4 h-4" />
            </div>
          </div>
        </CardContent>
      </Card>
    </div>
  )

  const pcPage = () => (
    <div className="flex justify-center pt-4">
      <Card className="sm:w-xl w-full mx-4 mb-4">
        <CardContent>
          <Tabs defaultValue="communication">
            <TabsList className="w-full">
              <TabsTrigger value="communication">{i18n._('sys.system_management.communication_title')}</TabsTrigger>
              {/* <TabsTrigger value="local-bluetooth">{i18n._('sys.system_management.bluetooth_title')}</TabsTrigger> */}
              <TabsTrigger value="device-password">{i18n._('sys.system_management.device_password')}</TabsTrigger>
              <TabsTrigger value="firmware-upgrade">{i18n._('sys.system_management.firmware_upgrade')}</TabsTrigger>
            </TabsList>
            <TabsContent value="communication">
              <Communication setCurrentPage={setCurrentPage} />
            </TabsContent>
            {/* <TabsContent value="local-bluetooth">
              <LocalBluetooth setCurrentPage={setCurrentPage} />
            </TabsContent> */}
            <TabsContent value="device-password">
              <DevicePassword setCurrentPage={setCurrentPage} />
            </TabsContent>
            <TabsContent value="firmware-upgrade">
              <FirmwareUpgrade setCurrentPage={setCurrentPage} />
            </TabsContent>
          </Tabs>
        </CardContent>
      </Card>
    </div>
  )

  return (
    <div className="relative overflow-hidden">
      <div className={`transition-transform duration-300 ease-in-out ${isAnimating
        ? direction === 'left'
          ? '-translate-x-full'
          : 'translate-x-0'
        : !currentPage
          ? 'translate-x-0'
          : '-translate-x-full'
        }`}
      >
        {!currentPage && (isMobile ? mobilePage() : pcPage())}
      </div>
      <div className={`transition-transform duration-300 ease-in-out ${isAnimating
        ? direction === 'left'
          ? 'translate-x-0'
          : 'translate-x-full'
        : currentPage
          ? 'translate-x-0'
          : 'translate-x-full'
        }`}
      >
        {currentPage
          && (
            <Card className=" w-[calc(100%-2rem)] bg-white rounded-lg my-4 mx-auto">
              <CardContent>
                {renderPage()}
              </CardContent>
            </Card>
          )}
      </div>
    </div>
  )
} 