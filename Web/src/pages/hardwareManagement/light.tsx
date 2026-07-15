import { useState, useEffect } from 'preact/hooks';
import { useLingui } from '@lingui/react';
import { Label } from '@/components/ui/label';
import { Input } from '@/components/ui/input';
import { Separator } from '@/components/ui/separator';
import { Skeleton } from '@/components/ui/skeleton';
import Slider from '@/components/slider';
import hardwareServiceApi, { type SetLightConfigReq } from '@/services/api/hardware-management';

export default function Light() {
   const { i18n } = useLingui();
   const [lightConfig, setLightConfig] = useState<SetLightConfigReq>({
      mode: 'auto',
      brightness_level: 0,
      connected: false,
      custom_schedule: {
         start_hour: 0,
         start_minute: 0,
         end_hour: 0,
         end_minute: 0,
      },
   })
   const [loading, setLoading] = useState(true);
   const { getLightConfigReq, setLightConfigReq } = hardwareServiceApi;
   const initLightConfig = async () => {
      try {
         setLoading(true);
         const res = await getLightConfigReq();
         setLightConfig(res.data);
      } catch (error) {
         console.error('initLightConfig', error);
         throw error;
      } finally {
         setLoading(false);
      }
   }
   useEffect(() => {
      initLightConfig();
   }, []);

   const handleSetLightConfig = async (config: SetLightConfigReq) => {
      try {
         await setLightConfigReq(config);
         setLightConfig(config);
      } catch (error) {
         console.error('handleSetLightConfig', error);
         throw error;
      }
   }
   const handleSetLightBrightness = async (value: number) => {
      await handleSetLightConfig({ ...lightConfig, brightness_level: value });
   }

   const skeletonScreen = () => (
      <div className="flex flex-col gap-2 w-full mt-2">
         <div className="flex justify-between gap-4">
            <Skeleton className="w-15 h-8" />
            <Skeleton className="w-15 h-8" />
         </div>
         <div className="flex justify-between gap-4">
            <Skeleton className="w-full h-8" />
         </div>
      </div>
   )
   return (
      <div>
         {loading ? skeletonScreen() : (
            <div className="flex flex-col gap-2 mt-4 bg-gray-100 p-4 rounded-lg">
               <div className="flex justify-between">
                  <Label>{i18n._('sys.hardware_management.connection_status')}</Label>
                  <div className="flex items-center gap-2">
                     <div className={`w-2 h-2 rounded-full ${lightConfig?.connected ? 'bg-green-500' : 'bg-gray-500'}`}></div>
                     <p>{i18n._(`common.${lightConfig?.connected ? 'connected' : 'disconnected'}`)}</p>
                  </div>
               </div>
               {lightConfig?.connected && (
                  <>
               <Separator />
                     <div className="flex justify-between">
                        <Label>{i18n._('sys.hardware_management.ir_light')}</Label>
                     </div>
                     <Separator />
                     <div className="flex justify-between">
                        <Label>{i18n._('sys.hardware_management.ir_light_intensity')}</Label>
                        <div className="flex items-center gap-2">
                           <Slider className="w-4xs md:w-2xs" value={lightConfig.brightness_level} onChange={value => setLightConfig({ ...lightConfig, brightness_level: value })} onChangeEnd={value => handleSetLightBrightness(value)} max={100} step={1} />
                           <Input
                             className="w-[65px]"
                             type="number"
                             value={lightConfig.brightness_level}
                             min={0}
                             max={100}
                             step={1}
                             onChange={(e) => {
                                      const input = e.target as HTMLInputElement;
                                      const n = Math.round(Number(input.value));
                                      const clamped = Math.max(0, Math.min(100, Number.isFinite(n) ? n : 0));
                                      input.value = String(clamped);
                                      setLightConfig({ ...lightConfig, brightness_level: clamped });
                                    }}
                             onBlur={e => handleSetLightBrightness(Math.max(0, Math.min(100, Number.isNaN(Number((e.target as HTMLInputElement).value)) ? 0 : Number((e.target as HTMLInputElement).value))))}
                           />
                        </div>
                     </div>
                  </>
               )}
            </div>
         )}
      </div>
   )
}