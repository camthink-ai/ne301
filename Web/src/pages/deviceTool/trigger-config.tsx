import { useEffect, useState } from 'preact/hooks';
import { Separator } from '@/components/ui/separator';
import { Card, CardContent } from '@/components/ui/card';
import { Switch } from '@/components/ui/switch';
import { Input } from '@/components/ui/input';
import { Label } from '@/components/ui/label';
import { Skeleton } from '@/components/ui/skeleton';
import { Tooltip, TooltipContent, TooltipTrigger } from '@/components/tooltip';
import { Button } from '@/components/ui/button';
import TimePicker from '@/components/time-picker';
import {
  Select,
  SelectContent,
  SelectItem,
  SelectTrigger,
  SelectValue,
} from '@/components/ui/select';
import { useLingui } from '@lingui/react';
import SvgIcon from '@/components/svg-icon';
import deviceTool, { type PirConfigReq } from '@/services/api/deviceTool';
import { toast } from 'sonner';

type TriggerConfigType = {
  pir_trigger: {
    enable: boolean;
    trigger_type:
    | 'rising_edge'
    | 'falling_edge'
    | 'high_level'
    | 'low_level'
    | 'both_edges';
    sensitivity_level: number;
    ignore_time_s: number;
    pulse_count: number;
    window_time_s: number;
    disable_in_preview: boolean;
  };
  timer_trigger: {
    enable: boolean;
    capture_mode: string;
    interval_sec: number;
    time_node_count: number;
    time_node: string[];
    weekdays: number[];
    interval_mode: 'normal' | 'scheduled';
    start_time: string;
  };
  remote_trigger: {
    enable: boolean;
  };
};

export type { TriggerConfigType as TriggerConfig };

type TriggerConfigProps = {
  childeRef: React.RefObject<HTMLDivElement>;
};

function PIRSkeleton() {
  return (
    <div className="flex flex-col gap-2 justify-between items-center">
      <Skeleton className="w-full h-10 rounded-md"></Skeleton>
      <Skeleton className="w-full h-10 rounded-md"></Skeleton>
      <Skeleton className="w-full h-10 rounded-md"></Skeleton>
      <Skeleton className="w-full h-10 rounded-md"></Skeleton>
    </div>
  );
}

export default function TriggerConfig({ childeRef }: TriggerConfigProps) {
  const { i18n } = useLingui();
  const { configTriggerConfigReq, getTriggerConfigReq } = deviceTool;
  const [intervalCaptureTime, setIntervalCaptureTime] = useState(10);
  const [intervalCaptureTimeUnit, setIntervalCaptureTimeUnit] = useState('hour');
  const [scheduledStartTime, setScheduledStartTime] = useState('08:00');
  const [savePirTriggerLoading, setSavePirTriggerLoading] = useState(false);
  const [PIRLoading, setPIRLoading] = useState(false);
  const [savedNextCaptureAt, setSavedNextCaptureAt] = useState<number | null>(
    null
  );
  const WeekUnitMap = new Map([
    [0, i18n._('common.everyday').toString()],
    [1, i18n._('common.monday').toString()],
    [2, i18n._('common.tuesday').toString()],
    [3, i18n._('common.wednesday').toString()],
    [4, i18n._('common.thursday').toString()],
    [5, i18n._('common.friday').toString()],
    [6, i18n._('common.saturday').toString()],
    [7, i18n._('common.sunday').toString()],
  ]);
  const [triggerConfig, setTriggerConfig] = useState<TriggerConfigType>({
    pir_trigger: {
      enable: true,
      trigger_type: 'rising_edge',
      sensitivity_level: 30,
      ignore_time_s: 7,
      pulse_count: 1,
      window_time_s: 0,
      disable_in_preview: true,
    },
    timer_trigger: {
      enable: false,
      capture_mode: 'interval',
      interval_sec: 60,
      time_node_count: 0,
      time_node: [],
      weekdays: [],
      interval_mode: 'normal',
      start_time: '08:00',
    },
    remote_trigger: {
      enable: false,
    },
  });
  const getPirConfig = async () => {
    try {
      setPIRLoading(true);
      const res = await getTriggerConfigReq();
      setTriggerConfig(res.data);
      setSavedNextCaptureAt(res.data.timer_trigger?.next_capture_at ?? null);
    } catch (error) {
      console.error('getPirConfig', error);
      throw error;
    } finally {
      setPIRLoading(false);
    }
  };
  useEffect(() => {
    getPirConfig();
  }, []);
  const initIntervalCaptureTime = () => {
    if (!triggerConfig.timer_trigger) return;

    if (
      (triggerConfig.timer_trigger.interval_sec / 60) % 60 === 0
      && triggerConfig.timer_trigger.interval_sec / 60 / 60 > 0
    ) {
      setIntervalCaptureTimeUnit('hour');
      setIntervalCaptureTime(
        triggerConfig.timer_trigger.interval_sec / 60 / 60
      );
    } else {
      setIntervalCaptureTimeUnit('minute');
      setIntervalCaptureTime(triggerConfig.timer_trigger.interval_sec / 60);
    }

    // Handle case where one second is the base point
    triggerConfig.timer_trigger.time_node.map(item => {
      if (typeof item === 'number') {
        const hour = item / 60 / 60;
        const minute = (item / 60) % 60;
        return `${hour}:${minute}`;
      }
      return item;
    });

    // Initialize start_time for scheduled interval mode
    if (triggerConfig.timer_trigger.start_time) {
      const st = triggerConfig.timer_trigger.start_time;
      if (typeof st === 'string' && st.includes(':')) {
        setScheduledStartTime(st);
      }
    }
  };
  useEffect(() => {
    initIntervalCaptureTime();
  }, [triggerConfig.timer_trigger?.interval_sec]);

  const handleIntervalCaptureTimeChange = (e: Event) => {
    const target = e.target as HTMLInputElement;
    const inputValue = target.value;
    // Handle empty string
    if (inputValue === '') {
      setIntervalCaptureTime(1);
      return;
    }
    const value = Number(inputValue);
    // Limit minimum value to 1, cannot be negative or 0
    if (Number.isNaN(value)) {
      setIntervalCaptureTime(1);
    } else {
      const clampedValue = Math.max(1, value);
      setIntervalCaptureTime(clampedValue);
      // If value is clamped, immediately update input display
      if (value !== clampedValue) {
        target.value = clampedValue.toString();
      }
    }
  };
  const handleIntervalCaptureTimeUnitChange = (value: string) => {
    setIntervalCaptureTimeUnit(value);
  };
  const handleAddIntervalCapture = () => {
    if (!triggerConfig.timer_trigger) return;
    const newWeekdays = [...(triggerConfig.timer_trigger.weekdays || []), 0];
    const newTimeNode = [...triggerConfig.timer_trigger.time_node, '00:00'];
    setTriggerConfig({
      ...triggerConfig,
      timer_trigger: {
        ...triggerConfig.timer_trigger,
        time_node_count: newTimeNode.length,
        time_node: newTimeNode,
        weekdays: newWeekdays,
      },
    });
  };
  const handleCaptureTimeChange = (index: number, value: string) => {
    if (!triggerConfig.timer_trigger) return;
    let weekUnit = value.split(' ')[0];
    const timeStr = value.split(' ')[1];
    if (Number.isNaN(Number(weekUnit))) {
      weekUnit = Number(
        Array.from(WeekUnitMap.entries()).find(
          item => item[1] === weekUnit
        )?.[0]
      ) as unknown as string;
    }
    const newWeekdays = triggerConfig.timer_trigger.weekdays.map((item, i) => (i === index ? Number(weekUnit) : item));
    const newTimeNode = triggerConfig.timer_trigger.time_node.map((item, i) => (i === index ? timeStr : item));
    setTriggerConfig({
      ...triggerConfig,
      timer_trigger: {
        ...triggerConfig.timer_trigger,
        time_node_count: newTimeNode.length,
        time_node: newTimeNode,
        weekdays: newWeekdays,
      },
    });
  };

  const handleIntervalCapture = async () => {
    if (!triggerConfig.timer_trigger) return;
    const formateTime = intervalCaptureTimeUnit === 'hour'
      ? intervalCaptureTime * 60 * 60
      : intervalCaptureTime * 60;
    try {
      const newConfig = {
        ...triggerConfig,
        timer_trigger: {
          ...triggerConfig.timer_trigger,
          interval_sec: formateTime,
          interval_mode: triggerConfig.timer_trigger.interval_mode || 'normal',
          start_time:
            triggerConfig.timer_trigger.interval_mode === 'scheduled'
              ? scheduledStartTime
              : '00:00',
        },
      };
      await setTriggerConfigApi(newConfig);
      toast.success(i18n._('common.configSuccess'));
    } catch (error) {
      console.error('handleIntervalCapture', error);
      throw error;
    }
  };
  const handleFixedCapture = async () => {
    if (!triggerConfig.timer_trigger) return;
    try {
      const newConfig = {
        ...triggerConfig,
        timer_trigger: { ...triggerConfig.timer_trigger, capture_mode: 'once' },
      };
      await setTriggerConfigApi(newConfig);
      toast.success(i18n._('common.configSuccess'));
    } catch (error) {
      console.error('handleFixedCapture', error);
      throw error;
    }
  };
  const setTriggerConfigApi = async (config = triggerConfig) => {
    try {
      await configTriggerConfigReq(config as unknown as PirConfigReq);
      // Re-fetch from server to get computed fields like next_capture_at
      const res = await getTriggerConfigReq();
      setTriggerConfig(res.data);
      setSavedNextCaptureAt(res.data.timer_trigger?.next_capture_at ?? null);
    } catch (error) {
      console.error('setImageTrigger', error);
      throw error;
    }
  };

  const handlePirTriggerSensitivityLevelBlur = (e: Event) => {
    // 10-255
    const target = e.target as HTMLInputElement;
    const value = Number(target.value);
    if (value < 10) {
      setTriggerConfig({
        ...triggerConfig,
        pir_trigger: { ...triggerConfig.pir_trigger, sensitivity_level: 10 },
      });
    } else if (value > 255) {
      setTriggerConfig({
        ...triggerConfig,
        pir_trigger: { ...triggerConfig.pir_trigger, sensitivity_level: 255 },
      });
    } else {
      setTriggerConfig({
        ...triggerConfig,
        pir_trigger: { ...triggerConfig.pir_trigger, sensitivity_level: value },
      });
    }
  };

  const handlePirTriggerSave = async () => {
    try {
      setSavePirTriggerLoading(true);
      const newConfig = {
        ...triggerConfig,
        pir_trigger: { ...triggerConfig.pir_trigger },
      };
      await setTriggerConfigApi(newConfig);
      toast.success(i18n._('common.configSuccess'));
    } catch (error) {
      console.error('handlePirTriggerSave', error);
      throw error;
    } finally {
      setSavePirTriggerLoading(false);
    }
  };

  const handlePirTriggerChange = async (value: boolean) => {
    if (!triggerConfig.pir_trigger) return;
    const newConfig = {
      ...triggerConfig,
      pir_trigger: { ...triggerConfig.pir_trigger, enable: value },
    };
    setTriggerConfigApi(newConfig);
  };
  const handleTimerTriggerChange = (value: boolean) => {
    if (!triggerConfig.timer_trigger) return;
    const newConfig = {
      ...triggerConfig,
      timer_trigger: { ...triggerConfig.timer_trigger, enable: value },
    };
    setTriggerConfigApi(newConfig);
  };
  const handleRemoteTriggerChange = (value: boolean) => {
    if (!triggerConfig.remote_trigger) return;
    const newConfig = {
      ...triggerConfig,
      remote_trigger: { ...triggerConfig.remote_trigger, enable: value },
    };
    setTriggerConfigApi(newConfig);
  };

  const intervalCapture = () => (
    <div className="">
      {/* Interval type selector */}
      <div className="flex justify-between items-center gap-2 mb-2">
        <Label className="text-sm text-text-primary">
          {i18n._('sys.device_tool.interval_type')}
        </Label>
        <div className="flex rounded-md border border-gray-200 overflow-hidden">
          <button
            className={`px-3 py-1.5 text-xs font-medium ${triggerConfig.timer_trigger?.interval_mode !== 'scheduled' ? 'bg-[#f24a00] text-white' : 'bg-white text-gray-500'}`}
            onClick={() => {
              setSavedNextCaptureAt(null);
              setTriggerConfig({
                ...triggerConfig,
                timer_trigger: {
                  ...triggerConfig.timer_trigger,
                  interval_mode: 'normal',
                },
              });
            }}
          >
            {i18n._('sys.device_tool.normal_interval')}
          </button>
          <button
            className={`px-3 py-1.5 text-xs font-medium ${triggerConfig.timer_trigger?.interval_mode === 'scheduled' ? 'bg-[#f24a00] text-white' : 'bg-white text-gray-500'}`}
            onClick={() => {
              setSavedNextCaptureAt(null);
              setTriggerConfig({
                ...triggerConfig,
                timer_trigger: {
                  ...triggerConfig.timer_trigger,
                  interval_mode: 'scheduled',
                },
              });
            }}
          >
            {i18n._('sys.device_tool.scheduled_interval')}
          </button>
        </div>
      </div>

      {/* Interval value */}
      <div className="flex justify-between items-center gap-2">
        <Label className="text-sm text-text-primary">
          {i18n._('sys.device_tool.interval_capture')}
        </Label>
        <div className="flex items-center">
          <Input
            type="number"
            min={1}
            max={999}
            className="w-20"
            value={intervalCaptureTime}
            onChange={handleIntervalCaptureTimeChange}
            onBlur={e => {
              const value = Number((e.target as HTMLInputElement).value);
              const clampedValue = Math.max(
                1,
                Math.min(999, Number.isNaN(value) ? 1 : value)
              );
              setIntervalCaptureTime(clampedValue);
              (e.target as HTMLInputElement).value = clampedValue.toString();
            }}
          />
          <Select
            value={intervalCaptureTimeUnit}
            onValueChange={handleIntervalCaptureTimeUnitChange}
          >
            <SelectTrigger className="border-0 shadow-none focus-visible:ring-0 focus-visible:border-transparent">
              <SelectValue />
            </SelectTrigger>
            <SelectContent>
              <SelectItem value="hour">{i18n._('common.hour')}</SelectItem>
              <SelectItem value="minute">{i18n._('common.minute')}</SelectItem>
            </SelectContent>
          </Select>
        </div>
      </div>

      {/* Start time — only for scheduled mode */}
      {triggerConfig.timer_trigger?.interval_mode === 'scheduled' && (
        <div className="flex justify-between items-center gap-2 mt-2">
          <Label className="text-sm text-text-primary">
            {i18n._('sys.device_tool.start_time')}
          </Label>
          <TimePicker
            value={scheduledStartTime}
            onChange={(value: string) => setScheduledStartTime(value)}
            className="w-32"
          />
        </div>
      )}

      {/* Next capture info — only shown after config is saved to server */}
      {savedNextCaptureAt && triggerConfig.timer_trigger?.enable && (
        <div className="mt-2 p-2 bg-orange-50 border border-orange-100 rounded-md text-xs text-orange-700 flex justify-between">
          <span>{i18n._('sys.device_tool.next_capture')}</span>
          <span className="font-medium">
            {new Date(savedNextCaptureAt * 1000).toLocaleString()}
          </span>
        </div>
      )}

      <div className="flex justify-end mt-2">
        <Button variant="primary" onClick={handleIntervalCapture}>
          {i18n._('common.confirm')}
        </Button>
      </div>
    </div>
  );

  const handleDeleteFixedCapture = (index: number) => {
    if (!triggerConfig.timer_trigger) return;
    const newTimeNode = triggerConfig.timer_trigger.time_node.filter(
      (_, i) => i !== index
    );
    setTriggerConfig({
      ...triggerConfig,
      timer_trigger: {
        ...triggerConfig.timer_trigger,
        time_node_count: newTimeNode.length,
        time_node: newTimeNode,
      },
    });
  };
  const customSlot = (cb: () => void) => (
    <Button variant="outline" size="sm" onClick={cb}>
      {i18n._('common.delete')}
    </Button>
  );

  const fixedCapture = () => (
    <div className="mt-2">
      <div className="flex justify-between items-center">
        <Label className="text-sm text-text-primary">
          {i18n._('sys.device_tool.capture_mode')}
        </Label>

        <Button
          disabled={(triggerConfig.timer_trigger?.time_node_count || 0) >= 10}
          variant="outline"
          onClick={handleAddIntervalCapture}
        >
          <SvgIcon icon="add" />
          {i18n._('common.add')}
        </Button>
      </div>
      {triggerConfig.timer_trigger?.time_node?.map((_, index) => (
        <div key={index}>
          <TimePicker
            showWeekSelect
            customSlot={() => customSlot(() => handleDeleteFixedCapture(index))}
            value={
              `${WeekUnitMap.get(triggerConfig.timer_trigger?.weekdays?.[index] as unknown as number) || `${i18n._('common.everyday')}`} ${triggerConfig.timer_trigger?.time_node?.[index]}`
              || 'Everyday 00:00'
            }
            className="w-full mt-2"
            onChange={value => handleCaptureTimeChange(index, value)}
          />
        </div>
      ))}
      {(triggerConfig.timer_trigger?.time_node_count || 0) > 0 && (
        <div className="flex justify-end mt-2">
          <Button variant="primary" onClick={handleFixedCapture}>
            {i18n._('common.confirm')}
          </Button>
        </div>
      )}
    </div>
  );

  return (
    <>
      {/* Trigger method */}
      <p className="text-sm text-text-primary mb-2 font-semibold">
        {i18n._('sys.device_tool.trigger')}
      </p>
      <Card className="bg-gray-50">
        <CardContent className="">
          <div ref={childeRef} className="w-full h-full ">
            <div className="flex items-center justify-between">
              <div className="flex items-center gap-2">
                <Label className="text-sm text-text-primary">
                  {' '}
                  {i18n._('sys.device_tool.trigger_pir')}
                </Label>
                <Tooltip mbEnhance>
                  <TooltipTrigger>
                    <div className="flex justify-center items-center">
                      <SvgIcon className="w-4 h-4" icon="info" />
                    </div>
                  </TooltipTrigger>
                  <TooltipContent className="max-w-80 text-pretty">
                    <p>{i18n._('sys.device_tool.pir_note')}</p>
                  </TooltipContent>
                </Tooltip>
              </div>
              <Switch
                checked={triggerConfig.pir_trigger?.enable || false}
                onCheckedChange={value => handlePirTriggerChange(value)}
                aria-label="Toggle theme"
                className="transition-all duration-700 ease-[cubic-bezier(0.34,1.56,0.64,1)] hover:scale-110"
              />
            </div>
            {triggerConfig.pir_trigger?.enable && (
              <div className="border border-gray-200 border-solid p-4 rounded-md mt-2">
                {PIRLoading ? (
                  <PIRSkeleton />
                ) : (
                  <>
                    <div className="flex justify-between gap-2 flex-1 pr-0">
                      <Label className="text-sm text-text-primary shrink-0">
                        {' '}
                        {i18n._('sys.device_tool.trigger_type')}
                      </Label>
                      <Select
                        value={
                          triggerConfig.pir_trigger?.trigger_type
                          || 'rising_edge'
                        }
                        onValueChange={value => setTriggerConfig({
                          ...triggerConfig,
                          pir_trigger: {
                            ...triggerConfig.pir_trigger,
                            trigger_type: value as
                              | 'rising_edge'
                              | 'falling_edge'
                              | 'both_edges'
                              | 'high_level'
                              | 'low_level',
                          },
                        })}
                      >
                        <SelectTrigger className="border-0 shadow-none focus-visible:ring-0 focus-visible:border-transparent">
                          <SelectValue
                            placeholder={i18n._('sys.device_tool.trigger_in')}
                          />
                        </SelectTrigger>
                        <SelectContent>
                          <SelectItem value="rising_edge">
                            {i18n._('sys.device_tool.rising_edge')}
                          </SelectItem>
                          <SelectItem value="falling_edge">
                            {i18n._('sys.device_tool.falling_edge')}
                          </SelectItem>
                        </SelectContent>
                      </Select>
                    </div>
                    <div className="flex justify-between gap-2 flex-1 pr-0">
                      <div className="flex items-center gap-2">
                        <Label className="text-sm text-text-primary shrink-0">
                          {' '}
                          {i18n._('sys.device_tool.sensitivity_level')}
                        </Label>
                        <Tooltip mbEnhance>
                          <TooltipTrigger>
                            <div className="w-4 flex justify-center items-center">
                              <SvgIcon className="w-4 h-4" icon="info" />
                            </div>
                          </TooltipTrigger>
                          <TooltipContent className="max-w-80 text-pretty">
                            <p className="max-w-80 text-pretty">
                              {i18n._('sys.device_tool.sensitivity_level_note')}
                            </p>
                          </TooltipContent>
                        </Tooltip>
                      </div>
                      <Input
                        type="number"
                        min={1}
                        max={255}
                        className="w-20"
                        value={
                          triggerConfig.pir_trigger?.sensitivity_level || 10
                        }
                        onBlur={e => handlePirTriggerSensitivityLevelBlur(e)}
                      />
                    </div>
                    <div className="flex justify-between gap-2 flex-1 pr-0">
                      <div className="flex items-center gap-2">
                        <Label className="text-sm text-text-primary shrink-0">
                          {' '}
                          {i18n._('sys.device_tool.ignore_time')}
                        </Label>
                        <Tooltip mbEnhance>
                          <TooltipTrigger>
                            <div className="w-4 flex justify-center items-center">
                              <SvgIcon className="w-4 h-4" icon="info" />
                            </div>
                          </TooltipTrigger>
                          <TooltipContent className="max-w-80 text-pretty">
                            <p className="max-w-80 text-pretty">
                              {i18n._('sys.device_tool.ignore_time_note')}
                            </p>
                          </TooltipContent>
                        </Tooltip>
                      </div>
                      <Select
                        value={(
                          triggerConfig.pir_trigger?.ignore_time_s || 0
                        ).toString()}
                        onValueChange={value => setTriggerConfig({
                          ...triggerConfig,
                          pir_trigger: {
                            ...triggerConfig.pir_trigger,
                            ignore_time_s: Number(value),
                          },
                        })}
                      >
                        <SelectTrigger className="border-0 shadow-none focus-visible:ring-0 focus-visible:border-transparent">
                          <SelectValue
                            placeholder={i18n._('sys.device_tool.trigger_in')}
                          />
                        </SelectTrigger>
                        <SelectContent>
                          {Array.from({ length: 16 }).map((_, index) => (
                            <SelectItem key={index} value={index.toString()}>
                              {index}
                            </SelectItem>
                          ))}
                        </SelectContent>
                      </Select>
                    </div>
                    <div className="flex justify-between gap-2 flex-1 pr-0">
                      <div className="flex items-center gap-2">
                        <Label className="text-sm text-text-primary shrink-0">
                          {' '}
                          {i18n._('sys.device_tool.pulse_count')}
                        </Label>
                        <Tooltip mbEnhance>
                          <TooltipTrigger>
                            <div className="w-4 flex justify-center items-center">
                              <SvgIcon className="w-4 h-4" icon="info" />
                            </div>
                          </TooltipTrigger>
                          <TooltipContent className="max-w-80 text-pretty">
                            <p className="max-w-80 text-pretty">
                              {i18n._('sys.device_tool.pulse_count_note')}
                            </p>
                          </TooltipContent>
                        </Tooltip>
                      </div>
                      <Select
                        value={(
                          triggerConfig.pir_trigger?.pulse_count || 1
                        ).toString()}
                        onValueChange={value => setTriggerConfig({
                          ...triggerConfig,
                          pir_trigger: {
                            ...triggerConfig.pir_trigger,
                            pulse_count: Number(value),
                          },
                        })}
                      >
                        <SelectTrigger className="border-0 shadow-none focus-visible:ring-0 focus-visible:border-transparent">
                          <SelectValue
                            placeholder={i18n._('sys.device_tool.trigger_in')}
                          />
                        </SelectTrigger>
                        <SelectContent>
                          {Array.from({ length: 4 }).map((_, index) => (
                            <SelectItem
                              key={index + 1}
                              value={(index + 1).toString()}
                            >
                              {index + 1}
                            </SelectItem>
                          ))}
                        </SelectContent>
                      </Select>
                    </div>
                    <div className="flex justify-between gap-2 flex-1 pr-0">
                      <div className="flex items-center gap-2">
                        <Label className="text-sm text-text-primary shrink-0">
                          {' '}
                          {i18n._('sys.device_tool.window_time')}
                        </Label>
                        <Tooltip mbEnhance>
                          <TooltipTrigger>
                            <div className="w-4 flex justify-center items-center">
                              <SvgIcon className="w-4 h-4" icon="info" />
                            </div>
                          </TooltipTrigger>
                          <TooltipContent className="max-w-80 text-pretty">
                            <p>{i18n._('sys.device_tool.window_time_note')}</p>
                          </TooltipContent>
                        </Tooltip>
                      </div>
                      <Select
                        value={(
                          triggerConfig.pir_trigger?.window_time_s || 0
                        ).toString()}
                        onValueChange={value => setTriggerConfig({
                          ...triggerConfig,
                          pir_trigger: {
                            ...triggerConfig.pir_trigger,
                            window_time_s: Number(value),
                          },
                        })}
                      >
                        <SelectTrigger className="border-0 shadow-none focus-visible:ring-0 focus-visible:border-transparent">
                          <SelectValue
                            placeholder={i18n._('sys.device_tool.trigger_in')}
                          />
                        </SelectTrigger>
                        <SelectContent>
                          {Array.from({ length: 4 }).map((_, index) => (
                            <SelectItem key={index} value={index.toString()}>
                              {index}
                            </SelectItem>
                          ))}
                        </SelectContent>
                      </Select>
                    </div>
                    <div className="flex justify-between gap-2 flex-1 pr-0">
                      <div className="flex items-center gap-2">
                        <Label className="text-sm text-text-primary shrink-0">
                          {' '}
                          {i18n._('sys.device_tool.disable_pir_in_preview')}
                        </Label>
                        <Tooltip mbEnhance>
                          <TooltipTrigger>
                            <div className="w-4 flex justify-center items-center">
                              <SvgIcon className="w-4 h-4" icon="info" />
                            </div>
                          </TooltipTrigger>
                          <TooltipContent className="max-w-80 text-pretty">
                            <p>{i18n._('sys.device_tool.disable_pir_in_preview_note')}</p>
                          </TooltipContent>
                        </Tooltip>
                      </div>
                      <Select
                        value={
                          (triggerConfig.pir_trigger?.disable_in_preview ?? true)
                            ? 'yes'
                            : 'no'
                        }
                        onValueChange={value => setTriggerConfig({
                          ...triggerConfig,
                          pir_trigger: {
                            ...triggerConfig.pir_trigger,
                            disable_in_preview: value === 'yes',
                          },
                        })}
                      >
                        <SelectTrigger className="border-0 shadow-none focus-visible:ring-0 focus-visible:border-transparent">
                          <SelectValue
                            placeholder={i18n._('sys.device_tool.trigger_in')}
                          />
                        </SelectTrigger>
                        <SelectContent>
                          <SelectItem value="yes">
                            {i18n._('common.yes')}
                          </SelectItem>
                          <SelectItem value="no">
                            {i18n._('common.no')}
                          </SelectItem>
                        </SelectContent>
                      </Select>
                    </div>
                    <div className="flex justify-end mt-2">
                      <Button
                        variant="primary"
                        disabled={savePirTriggerLoading}
                        onClick={handlePirTriggerSave}
                      >
                        {savePirTriggerLoading ? (
                          <div className="w-full h-full flex items-center justify-center">
                            <div
                              className="w-4 h-4 rounded-full border-2 border-[#f24a00] border-t-transparent animate-spin"
                              aria-label="loading"
                            />
                          </div>
                        ) : (
                          i18n._('common.save')
                        )}
                      </Button>
                    </div>
                  </>
                )}
              </div>
            )}
            <Separator className="my-2" />
            <div className="">
              <div className="flex items-center  justify-between">
                <div className="flex items-center gap-2">
                  <Label className="text-sm text-text-primary">
                    {' '}
                    {i18n._('sys.device_tool.remote_control')}
                  </Label>
                  <Tooltip mbEnhance>
                    <TooltipTrigger>
                      <div className="w-4 flex justify-center items-center">
                        <SvgIcon className="w-4 h-4" icon="info" />
                      </div>
                    </TooltipTrigger>
                    <TooltipContent className="max-w-80 text-pretty">
                      <p>{i18n._('sys.device_tool.remote_control_note')}</p>
                    </TooltipContent>
                  </Tooltip>
                </div>
                <Switch
                  checked={triggerConfig.remote_trigger?.enable || false}
                  onCheckedChange={value => handleRemoteTriggerChange(value)}
                  aria-label="Toggle theme"
                  className="transition-all duration-700 ease-[cubic-bezier(0.34,1.56,0.64,1)] hover:scale-110"
                />
              </div>
            </div>
            <Separator className="my-2" />
            <div className="">
              <div className="flex items-center  justify-between">
                <div className="flex items-center gap-2">
                  <Label className="text-sm text-text-primary">
                    {' '}
                    {i18n._('sys.device_tool.schedule')}
                  </Label>
                  <Tooltip mbEnhance>
                    <TooltipTrigger>
                      <div className="w-4 flex justify-center items-center">
                        <SvgIcon className="w-4 h-4" icon="info" />
                      </div>
                    </TooltipTrigger>
                    <TooltipContent className="max-w-80 text-pretty">
                      <p>{i18n._('sys.device_tool.timing_capture_note')}</p>
                    </TooltipContent>
                  </Tooltip>
                </div>
                <Switch
                  checked={triggerConfig.timer_trigger?.enable || false}
                  onCheckedChange={value => handleTimerTriggerChange(value)}
                  aria-label="Toggle theme"
                  className="transition-all duration-700 ease-[cubic-bezier(0.34,1.56,0.64,1)] hover:scale-110"
                />
              </div>
            </div>
            {triggerConfig.timer_trigger?.enable && (
              <div className="border border-gray-200 border-solid p-4 rounded-md mt-2">
                <div className="flex items-center  justify-between">
                  <Label className="text-sm text-text-primary">
                    {i18n._('sys.device_tool.capture_mode')}
                  </Label>
                  <Select
                    value={
                      triggerConfig.timer_trigger?.capture_mode || 'interval'
                    }
                    onValueChange={value => setTriggerConfig({
                      ...triggerConfig,
                      timer_trigger: {
                        ...triggerConfig.timer_trigger,
                        capture_mode: value,
                      },
                    })}
                  >
                    <SelectTrigger className="border-0 shadow-none focus-visible:ring-0 focus-visible:border-transparent">
                      <SelectValue
                        placeholder={i18n._('sys.device_tool.trigger_in')}
                      />
                    </SelectTrigger>
                    <SelectContent>
                      <SelectItem value="interval">
                        {i18n._('sys.device_tool.interval_capture')}
                      </SelectItem>
                      <SelectItem value="once">
                        {i18n._('sys.device_tool.fixed_capture')}
                      </SelectItem>
                    </SelectContent>
                  </Select>
                </div>
                {/* Mode parameter options */}
                {triggerConfig.timer_trigger?.capture_mode === 'interval'
                  && intervalCapture()}
                {triggerConfig.timer_trigger?.capture_mode === 'once'
                  && fixedCapture()}
              </div>
            )}
          </div>
        </CardContent>
      </Card>
    </>
  );
}
