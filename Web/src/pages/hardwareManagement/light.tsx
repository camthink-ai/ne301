import { useState, useEffect, useRef } from 'preact/hooks';
import { useLingui } from '@lingui/react';
import { Label } from '@/components/ui/label';
import { Input } from '@/components/ui/input';
import TimePicker from '@/components/time-picker';
import { Separator } from '@/components/ui/separator';
import { Skeleton } from '@/components/ui/skeleton';
import { Select, SelectContent, SelectItem, SelectTrigger, SelectValue } from '@/components/ui/select';
import { Switch } from '@/components/ui/switch';
import Slider from '@/components/slider';
import hardwareServiceApi, { type SetLightConfigReq } from '@/services/api/hardware-management';

export default function Light() {
   const { i18n } = useLingui();
   const [startTime, setStartTime] = useState('10:00');
   const [endTime, setEndTime] = useState('12:00');
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
   const lightConfigRef = useRef(lightConfig);
   const testLightOnRef = useRef(false);
   const brightnessApplyTimerRef = useRef<ReturnType<typeof setTimeout> | null>(null);
   const latestBrightnessRef = useRef<number>(lightConfig.brightness_level);
   const [testLightOn, setTestLightOn] = useState(false);
   const [loading, setLoading] = useState(true);
   const { getLightConfigReq, setLightConfigReq, controlLightReq } = hardwareServiceApi;

   useEffect(() => {
      lightConfigRef.current = lightConfig;
   }, [lightConfig]);

   useEffect(() => {
      testLightOnRef.current = testLightOn;
   }, [testLightOn]);

   const initLightConfig = async () => {
      try {
         setLoading(true);
         const res = await getLightConfigReq();
         setLightConfig(res.data);
         setStartTime(`${res.data.custom_schedule.start_hour.toString().padStart(2, '0')}:${res.data.custom_schedule.start_minute.toString().padStart(2, '0')}`);
         setEndTime(`${res.data.custom_schedule.end_hour.toString().padStart(2, '0')}:${res.data.custom_schedule.end_minute.toString().padStart(2, '0')}`);
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
   const handleSetMode = async (value: 'auto' | 'custom' | 'off') => {
      await handleSetLightConfig({ ...lightConfig, mode: value });
      setLightConfig({ ...lightConfig, mode: value });
   }
   const handleSetLightBrightness = async (value: number) => {
      const nextCfg: SetLightConfigReq = { ...lightConfigRef.current, brightness_level: value };
      await handleSetLightConfig(nextCfg);
      // When test switch is ON, update duty immediately by re-applying manual ON.
      if (testLightOnRef.current) {
         await controlLightReq({ enable: true });
      }
   }

   const handleToggleTestLight = async (checked: boolean) => {
      if (!checked && brightnessApplyTimerRef.current) {
         clearTimeout(brightnessApplyTimerRef.current);
         brightnessApplyTimerRef.current = null;
      }
      testLightOnRef.current = checked;
      setTestLightOn(checked);
      try {
         if (checked) {
            // Ensure brightness is applied before turning ON.
            const value = latestBrightnessRef.current;
            const nextCfg: SetLightConfigReq = {
               ...lightConfigRef.current,
               brightness_level: value,
            };
            await handleSetLightConfig(nextCfg);
            await controlLightReq({ enable: true });
         } else {
            await controlLightReq({ enable: false });
         }
      } catch (error) {
         console.error('handleToggleTestLight', error);
         testLightOnRef.current = !checked;
         setTestLightOn(!checked);
         if (checked) {
            // Best-effort revert: turn light off if enabling failed.
            controlLightReq({ enable: false }).catch(() => {});
         }
      }
   }

   const handleSetStartTime = async (value: string) => {
      await handleSetLightConfig({ ...lightConfig, custom_schedule: { ...lightConfig.custom_schedule, start_hour: Number(value.split(':')[0]), start_minute: Number(value.split(':')[1]) } });
      setStartTime(value);
   }
   const handleSetEndTime = async (value: string) => {
      await handleSetLightConfig({ ...lightConfig, custom_schedule: { ...lightConfig.custom_schedule, end_hour: Number(value.split(':')[0]), end_minute: Number(value.split(':')[1]) } });
      setEndTime(value);
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

   // If leaving the brightness-adjustable modes, ensure test switch turns light off
   useEffect(() => {
      if (loading) return;
      if (!lightConfig?.connected || lightConfig.mode === 'off') {
         if (testLightOn) {
            if (brightnessApplyTimerRef.current) {
               clearTimeout(brightnessApplyTimerRef.current);
               brightnessApplyTimerRef.current = null;
            }
            controlLightReq({ enable: false }).catch(error => {
               console.error('auto-disable test light', error);
            });
            setTestLightOn(false);
         }
      }
   }, [lightConfig?.connected, lightConfig?.mode, loading, testLightOn]);

   const scheduleRealtimeBrightnessApply = (value: number) => {
      latestBrightnessRef.current = value;
      if (!testLightOnRef.current) return;
      if (brightnessApplyTimerRef.current) {
         clearTimeout(brightnessApplyTimerRef.current);
      }
      // Throttle to reduce POST spam while dragging.
      brightnessApplyTimerRef.current = setTimeout(() => {
         handleSetLightBrightness(latestBrightnessRef.current).catch(error => {
            console.error('scheduleRealtimeBrightnessApply', error);
         });
      }, 150);
   };

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
                        <Label>{i18n._('sys.hardware_management.fill_light')}</Label>
                        <Select value={lightConfig.mode} onValueChange={handleSetMode}>
                           <SelectTrigger className=" bg-transparent border-0 !shadow-none !outline-none
                                  focus:!outline-none focus:!ring-0 focus:!ring-offset-0 focus:!shadow-none focus:!border-transparent
                                  focus-visible:!outline-none focus-visible:!ring-0 focus-visible:!ring-offset-0
                                  text-right"
                           >
                              <SelectValue
                                placeholder={i18n._('sys.hardware_management.fill_light_desc')}

                              />
                           </SelectTrigger>
                           <SelectContent>
                              <SelectItem value="custom">{i18n._('sys.hardware_management.fill_light_type_custom')}</SelectItem>
                              <SelectItem value="auto">{i18n._('sys.hardware_management.fill_light_type_open')}</SelectItem>
                              <SelectItem value="off">{i18n._('sys.hardware_management.fill_light_type_close')}</SelectItem>
                           </SelectContent>
                        </Select>
                     </div>
                     {lightConfig.mode === 'custom' && (
                        <>
                           <Separator />
                           <div className="flex md:flex-row flex-col md:gap-12 gap-2 justify-between">
                              <Label className="shrink-0">{i18n._('sys.hardware_management.time_range')}</Label>
                              <div className="flex items-center gap-2">
                                 <TimePicker value={startTime} className="flex-1 text-sm" onChange={handleSetStartTime} />
                                 <span className="text-gray-500">-</span>
                                 <TimePicker value={endTime} className="flex-1" onChange={handleSetEndTime} />
                              </div>
                           </div>
                        </>
                     )}
                     {(lightConfig.mode === 'auto' || lightConfig.mode === 'custom') ? (
                        <>
                           <Separator />
                           <div className="flex justify-between">
                              <Label>{i18n._('sys.hardware_management.light_brightness')}</Label>
                              <div className="flex items-center gap-2">
                                 <Slider
                                   className="w-4xs md:w-2xs"
                                   value={lightConfig.brightness_level}
                                   onChange={value => {
                                      const nextCfg: SetLightConfigReq = {
                                         ...lightConfigRef.current,
                                         brightness_level: value,
                                      };
                                      setLightConfig(nextCfg);
                                      scheduleRealtimeBrightnessApply(value);
                                   }}
                                   onChangeEnd={value => handleSetLightBrightness(value)}
                                   max={100}
                                   step={1}
                                 />
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
                           <Separator />
                           <div className="flex justify-between items-center">
                              <Label>{i18n._('sys.hardware_management.fill_light_test')}</Label>
                              <Switch checked={testLightOn} onCheckedChange={handleToggleTestLight} />
                           </div>
                        </>
                     ) : (
                        null
                     )}
                  </>
               )}
            </div>
         )}
      </div>
   )
}