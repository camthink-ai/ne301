import { Card, CardContent } from '@/components/ui/card';
import { Tabs, TabsList, TabsTrigger, TabsContent } from '@/components/ui/tabs';
import Graphics from './graphics';
import Light from './light';
import CameraSource from './cameraSource';
import cameraApi from '@/services/api/camera';
import { useLingui } from '@lingui/react';
import { useEffect, useState } from 'preact/hooks';

export default function ApplicationManagement() {
  const { i18n } = useLingui();
  /* Image Management tunes the native sensor/ISP (flip, ISP profile,
   * grayscale...): with the UVC (MJPEG) source those controls do not
   * exist - hide the tab. */
  const [isUvc, setIsUvc] = useState(false);
  /* Tabs must mount with the correct defaultValue: rendering them before
   * the source is known mounts 'graphics' first, and removing that tab
   * once UVC arrives leaves the Tabs state dangling (internal error). */
  const [srcLoaded, setSrcLoaded] = useState(false);

  useEffect(() => {
    cameraApi.getCameraConfig()
      .then((res) => setIsUvc(res.data.source === 'uvc'))
      .catch(() => setIsUvc(false))
      .finally(() => setSrcLoaded(true));
  }, []);

if (!srcLoaded) {
  return (
    <div className="flex justify-center">
      <Card className="sm:w-xl w-full mx-4 my-4">
        <CardContent>
          <div className="h-40" />
        </CardContent>
      </Card>
    </div>
  );
}

return (
   <div className="flex justify-center">
    <Card className="sm:w-xl w-full mx-4 my-4">
      <CardContent>
        <Tabs defaultValue={isUvc ? 'camera_source' : 'graphics'}>
          <TabsList className="w-full">
            <TabsTrigger value="camera_source">{i18n._('sys.hardware_management.camera_source_title')}</TabsTrigger>
            {!isUvc && (
              <TabsTrigger value="graphics">{i18n._('sys.hardware_management.image_title')}</TabsTrigger>
            )}
            <TabsTrigger value="light">{i18n._('sys.hardware_management.light_title')}</TabsTrigger>
          </TabsList>
          <TabsContent value="camera_source">
            <CameraSource />
          </TabsContent>
          {!isUvc && (
            <TabsContent value="graphics">
              <Graphics />
            </TabsContent>
          )}
          <TabsContent value="light">
            <Light />
          </TabsContent>
        </Tabs>
      </CardContent>
    </Card>
   </div>
  )
}