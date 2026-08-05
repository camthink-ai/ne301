import { Card, CardContent } from '@/components/ui/card';
import { Tabs, TabsList, TabsTrigger, TabsContent } from '@/components/ui/tabs';
import CaptureConfig from './config';
import CaptureRecords from './records';
import { useLingui } from '@lingui/react';

export default function CaptureSettings() {
  const { i18n } = useLingui();
  return (
    <div className="flex justify-center">
      <Card className="sm:w-xl w-full mx-4 my-4">
        <CardContent>
          <Tabs defaultValue="config">
            <TabsList className="w-full">
              <TabsTrigger value="config">
                {i18n._('sys.capture_settings.config_tab')}
              </TabsTrigger>
              <TabsTrigger value="records">
                {i18n._('sys.capture_settings.records_tab')}
              </TabsTrigger>
            </TabsList>
            <TabsContent value="config">
              <CaptureConfig />
            </TabsContent>
            <TabsContent value="records">
              <CaptureRecords />
            </TabsContent>
          </Tabs>
        </CardContent>
      </Card>
    </div>
  );
}
