import { useEffect, useState } from 'react';
import { useLingui } from '@lingui/react';
import { Label } from '@/components/ui/label';
import { Input } from '@/components/ui/input';
import { Separator } from '@/components/ui/separator';
import TimePicker from '@/components/time-picker';
import { Select, SelectContent, SelectItem, SelectTrigger, SelectValue } from '@/components/ui/select';
import SvgIcon from '@/components/svg-icon';
import hardwareServiceApi, { type DayNightConfig, type DayNightSource } from '@/services/api/hardware-management';

const DEFAULT_CONFIG: DayNightConfig = {
  mode: 'auto',
  auto_source: 'isp_stats',
  custom_schedule: {
    start_hour: 18,
    start_minute: 0,
    end_hour: 6,
    end_minute: 0,
  },
  light_sensor: {
    night_threshold: 20,
    day_threshold: 40,
  },
  isp_stats: {
    night_enter_lux: 50,
    day_enter_lux: 150,
    night_enter_exposure_us: 20000,
    night_enter_gain_mdb: 6000,
    night_enter_avgl: 70,
    day_enter_exposure_us: 8000,
    day_enter_gain_mdb: 3000,
    day_enter_avgl: 80,
    ema_alpha_num: 1,
    ema_alpha_den: 4,
  },
  ir_brightness: 50,
};

const pad2 = (n: number) => n.toString().padStart(2, '0');

const clampNum = (v: number, min: number, max: number) => Math.max(min, Math.min(max, Number.isFinite(v) ? v : min));

type IspStats = DayNightConfig['isp_stats'];
/* Bounds mirror the firmware's actual value ranges:
 *  lux        — ISP_GetLuxEstimation returns real lux (uint32, bright scenes can reach 10^5+)
 *  exposure   — sensor exposure in µs (uint32, AEC capped by anti-flicker/exposure_max)
 *  gain       — sensor gain in mdB (OS04C10 GAIN_MIN=0, max ~47800 mdB)
 *  avgl       — average luminance, uint8 (0-255)
 */
const ISP_STAT_ROWS: { night: keyof IspStats; day: keyof IspStats; min: number; max: number }[] = [
  { night: 'night_enter_lux', day: 'day_enter_lux', min: 0, max: 1000000 },
  { night: 'night_enter_exposure_us', day: 'day_enter_exposure_us', min: 0, max: 1000000 },
  { night: 'night_enter_gain_mdb', day: 'day_enter_gain_mdb', min: 0, max: 60000 },
  { night: 'night_enter_avgl', day: 'day_enter_avgl', min: 0, max: 255 },
];

export default function DayNightAutoConfig() {
  const { i18n } = useLingui();
  const { getDayNightConfigReq, setDayNightConfigReq, getDayNightStatusReq } = hardwareServiceApi;
  const [cfg, setCfg] = useState<DayNightConfig>(DEFAULT_CONFIG);
  const [startTime, setStartTime] = useState('18:00');
  const [endTime, setEndTime] = useState('06:00');
  const [advancedOpen, setAdvancedOpen] = useState(false);
  const [lightPercent, setLightPercent] = useState<number | null>(null);
  const [ispMetrics, setIspMetrics] = useState<{ lux: number | null; exposure: number | null; avgL: number | null; gain: number | null }>({ lux: null, exposure: null, avgL: null, gain: null });
  const [refreshing, setRefreshing] = useState(false);

  const refreshStatus = async () => {
    setRefreshing(true);
    try {
      const res = await getDayNightStatusReq();
      const m = res.data?.metrics;
      setLightPercent(m ? m.light_sensor_percent : null);
      setIspMetrics({
        lux: m?.lux_ema ?? null,
        exposure: m?.exposure_us ?? null,
        avgL: m?.avg_l_ema ?? null,
        gain: m?.gain_mdb ?? null,
      });
    } catch (e) {
      console.error('refreshStatus', e);
    } finally {
      setRefreshing(false);
    }
  };

  useEffect(() => {
    if (cfg.auto_source === 'light_sensor' || cfg.auto_source === 'isp_stats') {
      refreshStatus();
    }
  }, [cfg.auto_source]);

  useEffect(() => {
    (async () => {
      try {
        const res = await getDayNightConfigReq();
        const data = res.data ?? DEFAULT_CONFIG;
        setCfg(data);
        setStartTime(`${pad2(data.custom_schedule.start_hour)}:${pad2(data.custom_schedule.start_minute)}`);
        setEndTime(`${pad2(data.custom_schedule.end_hour)}:${pad2(data.custom_schedule.end_minute)}`);
      } catch (e) {
        console.error('initDayNight', e);
      }
    })();
  }, []);

  const patch = async (p: Partial<DayNightConfig>) => {
    const next = { ...cfg, ...p };
    setCfg(next);
    try {
      await setDayNightConfigReq(p);
    } catch (e) {
      console.error('setDayNight', e);
    }
  };

  const setSource = (s: DayNightSource) => patch({ auto_source: s });

  const setSchedule = (which: 'start' | 'end', value: string) => {
    const [h, m] = value.split(':').map(Number);
    const sched = { ...cfg.custom_schedule };
    if (which === 'start') { sched.start_hour = h; sched.start_minute = m; setStartTime(value); } else { sched.end_hour = h; sched.end_minute = m; setEndTime(value); }
    patch({ custom_schedule: sched });
  };

  const setLsThreshold = (key: 'night_threshold' | 'day_threshold', v: number) => {
    patch({ light_sensor: { ...cfg.light_sensor, [key]: v } });
  };

  const setIspStat = (key: keyof DayNightConfig['isp_stats'], v: number) => {
    patch({ isp_stats: { ...cfg.isp_stats, [key]: v } });
  };

  return (
    <>
      <Separator />
      <div className="flex justify-between gap-4 items-center">
        <Label className="shrink-0">{i18n._('sys.hardware_management.auto_source')}</Label>
        <Select value={cfg.auto_source} onValueChange={v => setSource(v as DayNightSource)}>
          <SelectTrigger className="w-32 border-0 shadow-none focus-visible:ring-0">
            <SelectValue />
          </SelectTrigger>
          <SelectContent>
            <SelectItem value="time">{i18n._('sys.hardware_management.source_time')}</SelectItem>
            <SelectItem value="light_sensor">{i18n._('sys.hardware_management.source_light_sensor')}</SelectItem>
            <SelectItem value="isp_stats">{i18n._('sys.hardware_management.source_isp_stats')}</SelectItem>
          </SelectContent>
        </Select>
      </div>

      {cfg.auto_source === 'isp_stats' && (
        <div className="flex items-start gap-1 rounded-md bg-red-50 p-2 text-xs text-red-600">
          <span className="shrink-0 font-bold">!</span>
          <span>{i18n._('sys.hardware_management.isp_stats_warning')}</span>
        </div>
      )}

      {cfg.auto_source === 'time' && (
        <>
          <Separator />
          <div className="flex md:flex-row flex-col md:gap-12 gap-2 justify-between">
            <Label className="shrink-0">{i18n._('sys.hardware_management.time_range')}</Label>
            <div className="flex items-center gap-2">
              <TimePicker value={startTime} className="flex-1 text-sm" onChange={v => setSchedule('start', v)} />
              <span className="text-gray-500">-</span>
              <TimePicker value={endTime} className="flex-1" onChange={v => setSchedule('end', v)} />
            </div>
          </div>
        </>
      )}

      {cfg.auto_source === 'light_sensor' && (
        <>
          <Separator />
          <div className="flex justify-between gap-4 items-center">
            <Label>{i18n._('sys.hardware_management.ls_current_value')}</Label>
            <div className="flex items-center gap-2">
              <span className="text-sm text-gray-700 min-w-[3rem] text-right">
                {lightPercent != null ? `${lightPercent}%` : '--'}
              </span>
              <button
                type="button"
                onClick={refreshStatus}
                disabled={refreshing}
                className="inline-flex h-5 w-5 items-center justify-center text-gray-800 disabled:opacity-50"
                title={i18n._('sys.hardware_management.ls_refresh')}
              >
                <SvgIcon icon="reload" className={`h-4 w-4 ${refreshing ? 'animate-spin' : ''}`} />
              </button>
            </div>
          </div>
          <Separator />
          <div className="flex justify-between gap-4 items-center">
            <Label>{i18n._('sys.hardware_management.ls_night_threshold')}</Label>
            <Input
              className="w-24 text-right"
              type="number"
              min={0}
              max={100}
              value={cfg.light_sensor.night_threshold}
              onChange={e => setLsThreshold('night_threshold', Number((e.target as HTMLInputElement).value))}
              onBlur={e => setLsThreshold('night_threshold', Math.max(0, Math.min(100, Number((e.target as HTMLInputElement).value) || 0)))}
            />
          </div>
          <Separator />
          <div className="flex justify-between gap-4 items-center">
            <Label>{i18n._('sys.hardware_management.ls_day_threshold')}</Label>
            <Input
              className="w-24 text-right"
              type="number"
              min={0}
              max={100}
              value={cfg.light_sensor.day_threshold}
              onChange={e => setLsThreshold('day_threshold', Number((e.target as HTMLInputElement).value))}
              onBlur={e => setLsThreshold('day_threshold', Math.max(0, Math.min(100, Number((e.target as HTMLInputElement).value) || 0)))}
            />
          </div>
        </>
      )}

      {cfg.auto_source === 'isp_stats' && (
        <>
          <Separator />
          <div className="flex justify-between items-center">
            <Label>{i18n._('sys.hardware_management.isp_current_stats')}</Label>
            <button
              type="button"
              onClick={refreshStatus}
              disabled={refreshing}
              className="inline-flex h-5 w-5 items-center justify-center text-gray-800 disabled:opacity-50"
              title={i18n._('sys.hardware_management.ls_refresh')}
            >
              <SvgIcon icon="reload" className={`h-4 w-4 ${refreshing ? 'animate-spin' : ''}`} />
            </button>
          </div>
          <div className="grid grid-cols-4 gap-2 text-center">
            <div>
              <div className="text-xs text-gray-500">{i18n._('sys.hardware_management.isp_current_lux')}</div>
              <div className="text-sm">{ispMetrics.lux != null ? `${ispMetrics.lux}` : '--'}</div>
            </div>
            <div>
              <div className="text-xs text-gray-500">{i18n._('sys.hardware_management.isp_current_exposure')}</div>
              <div className="text-sm">{ispMetrics.exposure != null ? `${ispMetrics.exposure}` : '--'}</div>
            </div>
            <div>
              <div className="text-xs text-gray-500">{i18n._('sys.hardware_management.isp_current_avgl')}</div>
              <div className="text-sm">{ispMetrics.avgL != null ? `${ispMetrics.avgL}` : '--'}</div>
            </div>
            <div>
              <div className="text-xs text-gray-500">{i18n._('sys.hardware_management.isp_current_gain')}</div>
              <div className="text-sm">{ispMetrics.gain != null ? `${ispMetrics.gain}` : '--'}</div>
            </div>
          </div>
          <Separator />
          <button
            type="button"
            className="flex w-full items-center justify-between text-left"
            onClick={() => setAdvancedOpen(o => !o)}
          >
            <Label>{i18n._('sys.hardware_management.isp_stats_thresholds')}</Label>
            <span className="inline-flex h-4 w-4 shrink-0 items-center justify-center text-gray-500">
              <SvgIcon icon="right" className={`h-4 w-4 transition-transform duration-200 ${advancedOpen ? 'rotate-90' : 'rotate-0'}`} />
            </span>
          </button>
          {advancedOpen && (
            <div className="border border-gray-200 border-solid p-3 rounded-md flex flex-col gap-2">
              {ISP_STAT_ROWS.map(row => (
                <div key={row.night} className="grid grid-cols-2 gap-2">
                  <div className="flex flex-col gap-1">
                    <Label className="text-xs">{i18n._(`sys.hardware_management.isp_stat_${row.night}`)}</Label>
                    <Input
                      className="text-right"
                      type="number"
                      min={row.min}
                      max={row.max}
                      value={cfg.isp_stats[row.night]}
                      onChange={e => setIspStat(row.night, Number((e.target as HTMLInputElement).value))}
                      onBlur={e => setIspStat(row.night, clampNum(Number((e.target as HTMLInputElement).value) || 0, row.min, row.max))}
                    />
                  </div>
                  <div className="flex flex-col gap-1">
                    <Label className="text-xs">{i18n._(`sys.hardware_management.isp_stat_${row.day}`)}</Label>
                    <Input
                      className="text-right"
                      type="number"
                      min={row.min}
                      max={row.max}
                      value={cfg.isp_stats[row.day]}
                      onChange={e => setIspStat(row.day, Number((e.target as HTMLInputElement).value))}
                      onBlur={e => setIspStat(row.day, clampNum(Number((e.target as HTMLInputElement).value) || 0, row.min, row.max))}
                    />
                  </div>
                </div>
              ))}
              <div className="grid grid-cols-2 gap-2">
                <div className="flex flex-col gap-1">
                  <Label className="text-xs">{i18n._('sys.hardware_management.isp_stat_ema_alpha_num')}</Label>
                  <Input
                    className="text-right"
                    type="number"
                    min={0}
                    max={255}
                    value={cfg.isp_stats.ema_alpha_num}
                    onChange={e => setIspStat('ema_alpha_num', Number((e.target as HTMLInputElement).value))}
                    onBlur={e => setIspStat('ema_alpha_num', clampNum(Number((e.target as HTMLInputElement).value) || 0, 0, 255))}
                  />
                </div>
                <div className="flex flex-col gap-1">
                  <Label className="text-xs">{i18n._('sys.hardware_management.isp_stat_ema_alpha_den')}</Label>
                  <Input
                    className="text-right"
                    type="number"
                    min={1}
                    max={255}
                    value={cfg.isp_stats.ema_alpha_den}
                    onChange={e => setIspStat('ema_alpha_den', Number((e.target as HTMLInputElement).value))}
                    onBlur={e => setIspStat('ema_alpha_den', clampNum(Number((e.target as HTMLInputElement).value) || 1, 1, 255))}
                  />
                </div>
              </div>
            </div>
          )}
        </>
      )}
    </>
  );
}
