import { useEffect, useRef, useState } from 'preact/hooks';
import Upload from '@/components/upload';
import { createPortal } from 'preact/compat';
import { useBlocker, useNavigate } from 'react-router-dom';
import { useLingui } from '@lingui/react';
import SvgIcon from '@/components/svg-icon';
import systemApis, { type BundlePlanEntry, type BundlePreCheckRes } from '@/services/api/system';
import { Button } from '@/components/ui/button';
import { Card, CardContent } from '@/components/ui/card';
import { Progress } from '@/components/ui/progress';
import { Checkbox } from '@/components/ui/checkbox';
import WifiReloadMask from '@/components/wifi-reload-mask';
import { toast } from 'sonner';
import { retryFetch, sleep } from '@/utils';

const BUNDLE_HEADER_SIZE = 4096;
const ACCEPT_FILE_TYPE = {
    'application/octet-stream': ['.bin'],
};
const MAX_SIZE = 1024 * 1024 * 40;

/** bundle entry fw_type code -> /ota/upload firmwareType param */
const FW_TYPE_TO_UPLOAD: Record<number, 'fsbl' | 'app' | 'web' | 'ai' | 'wifi'> = {
    0x01: 'fsbl',
    0x02: 'app',
    0x03: 'web',
    0x04: 'ai',
    0x08: 'wifi',
};
const FW_LABEL: Record<string, string> = {
    fsbl: 'FSBL',
    app: 'App',
    web: 'Web',
    ai: 'Model',
    wifi: 'WiFi',
};

type Phase = 'select' | 'plan' | 'burning' | 'done';

const formatSize = (bytes: number) => {
    if (bytes >= 1024 * 1024) return `${(bytes / 1024 / 1024).toFixed(1)} MB`;
    return `${(bytes / 1024).toFixed(0)} KB`;
};

/** flash address as 0x70900000 (+size); -1 base renders as absent */
const hexAddr = (n: number) => `0x${(n >>> 0).toString(16).toUpperCase().padStart(8, '0')}`;
const partAddr = (base: number, size: number) => (base < 0 ? '—' : `${hexAddr(base)}+${hexAddr(size)}`);

export default function ImportBundle() {
    const { i18n } = useLingui();
    const navigate = useNavigate();
    const {
        bundlePreCheckReq,
        bundleBeginReq,
        bundleFinishReq,
        uploadOTAFileReq,
        restartDevice,
        getDeviceInfoReq,
    } = systemApis;
    const [bundleFile, setBundleFile] = useState<File | null>(null);
    const [plan, setPlan] = useState<BundlePreCheckRes | null>(null);
    const [phase, setPhase] = useState<Phase>('select');
    const [curStep, setCurStep] = useState(0);
    const [curPercent, setCurPercent] = useState(0);
    const [uploadedBytes, setUploadedBytes] = useState(0);
    const [isRestarting, setIsRestarting] = useState(false);
    // Entry type names the user ticked "skip" — offered for same-version
    // entries only; the device re-validates the list at bundle/begin.
    const [skipSet, setSkipSet] = useState<Set<string>>(new Set());
    // Stamp of the last upload-progress event (or step start) — a stall older
    // than a few seconds usually means the device is erasing the target
    // partition before writing (an 8MB AI partition erase takes seconds).
    const lastProgressTick = useRef(Date.now());
    const [progressStalled, setProgressStalled] = useState(false);

    // While sub-firmwares stream the page must not be left: refresh/close kills
    // the XHRs mid-write, and an in-app route change loses the progress/error UI
    // (the upload loop would keep running headless). Guard all three ways:
    // unload listener, router blocker (covers browser back), and the full
    // viewport click shield rendered below. Once isRestarting is set the burn
    // is committed and leaving is safe, so all guards stand down.
    const burnShieldActive = phase === 'burning' && !isRestarting;

    useEffect(() => {
        if (!burnShieldActive) return;
        const onBeforeUnload = (e: BeforeUnloadEvent) => {
            e.preventDefault();
            e.returnValue = '';
        };
        window.addEventListener('beforeunload', onBeforeUnload);
        return () => window.removeEventListener('beforeunload', onBeforeUnload);
    }, [burnShieldActive]);

    const blocker = useBlocker(burnShieldActive);
    useEffect(() => {
        if (blocker.state === 'blocked') {
            blocker.reset();
            toast.warning(i18n._('sys.system_management.bundle_burning_warning'));
        }
    }, [blocker, i18n]);

    // Stall hint: while a sub-firmware uploads, no progress events for >4s
    // most likely means the device is erasing the partition (not a hang) —
    // surface that instead of looking frozen.
    useEffect(() => {
        if (!burnShieldActive) {
            setProgressStalled(false);
            return;
        }
        const timer = setInterval(() => {
            setProgressStalled(Date.now() - lastProgressTick.current > 4000);
        }, 1000);
        return () => clearInterval(timer);
    }, [burnShieldActive]);

    // Forced full reflash: every planned entry burns unless the user skips a
    // same-version one (the WiFi / FSBL re-burns are the ones worth avoiding).
    const updateEntries: BundlePlanEntry[] = plan?.entries ?? [];
    const selectedEntries = updateEntries.filter((e) => !skipSet.has(e.type));
    const burnBytes = selectedEntries.reduce((sum, e) => sum + e.size, 0);
    const overallPercent = burnBytes > 0 ? Math.min(100, (uploadedBytes / burnBytes) * 100) : 0;

    // Selecting a file sends the 4096-byte bundle header to the device; the
    // device validates it against its own partition table and answers with
    // the burn plan. Every entry is a forced burn at a device-resolved
    // direct address (no AB slots — full reflash, OTA info rebuilt on reboot).
    const onBundleFileChange = async (file: File) => {
        if (file.size < BUNDLE_HEADER_SIZE) {
            toast.error(i18n._('sys.system_management.invalid_firmware_file'));
            return;
        }
        try {
            const header = file.slice(0, BUNDLE_HEADER_SIZE);
            const res = await bundlePreCheckReq(header);
            const data: BundlePreCheckRes = res?.data;
            if (!data || !Array.isArray(data.entries) || data.entries.length === 0) {
                toast.error(i18n._('sys.system_management.invalid_firmware_file'));
                return;
            }
            setBundleFile(file);
            setPlan(data);
            setSkipSet(new Set());
            setPhase('plan');
            setUploadedBytes(0);
            setCurStep(0);
            setCurPercent(0);
        } catch {
            // error toast is raised by the request interceptor
        }
    };

    const handleBurn = async () => {
        if (!bundleFile || !plan) return;
        if (selectedEntries.length === 0) {
            toast.error(i18n._('sys.system_management.bundle_nothing_selected'));
            return;
        }
        setPhase('burning');
        try {
            await bundleBeginReq([...skipSet]);
            // Burn in plan order (Model -> Web -> WiFi -> App -> FSBL, least
            // to most critical). Every burn is direct at the device-resolved
            // address (forced full reflash, no AB slots).
            let done = 0;
            // Sequential on purpose: burn order is the recoverability order
            // (Model -> Web -> WiFi -> App -> FSBL), never parallelized.
            for (let i = 0; i < selectedEntries.length; i++) {
                const entry = selectedEntries[i];
                const entryDone = done;
                setCurStep(i);
                setCurPercent(0);
                lastProgressTick.current = Date.now();
                const start = BUNDLE_HEADER_SIZE + entry.offset;
                const blob = bundleFile.slice(start, start + entry.size);
                // eslint-disable-next-line no-await-in-loop -- see above
                await uploadOTAFileReq(blob, FW_TYPE_TO_UPLOAD[entry.fw_type], {
                    ...(entry.addr ? { direct: true, addr: entry.addr } : {}),
                    // eslint-disable-next-line no-loop-func -- reads the const snapshot of this iteration
                    onUploadProgress: (pe) => {
                        const loaded = Math.min(pe.loaded, entry.size);
                        lastProgressTick.current = Date.now();
                        setCurPercent((loaded / entry.size) * 100);
                        setUploadedBytes(entryDone + loaded);
                    },
                });
                done += entry.size;
                setUploadedBytes(done);
                setCurPercent(100);
            }
            await bundleFinishReq('ok');

            // Reboot is driven here, after everything is burned and confirmed
            setIsRestarting(true);
            await restartDevice({ delay_seconds: 1 }, { skipErrorToast: true });
            await sleep(5000);
            // retryFetch THROWS when the retries are exhausted (it never
            // resolves falsy) — catch it here or the outer catch would
            // silently reset the page to file-select instead of guiding the
            // user to reconnect.
            let result: unknown = null;
            try {
                result = await retryFetch(
                    (signal) => getDeviceInfoReq({ skipErrorToast: true, signal }),
                    5000,
                    2,
                );
            } catch {
                /* device not back yet — handled by the else branch below */
            }
            if (result) {
                toast.success(i18n._('sys.system_management.update_success'));
                // Terminal state: phase must leave 'burning', or the burn
                // shield and the route blocker come back once the mask drops.
                setPhase('done');
            } else {
                // Device never came back on this network — typical when the
                // bundle carried a WiFi update (the 917 push takes minutes)
                // or the device's network changed and it cannot rejoin.
                // Same guidance page the WiFi upgrade uses: reconnect the
                // device AP / watch the LED, then re-enter.
                toast.error(i18n._('sys.system_management.network_disconnected'));
                navigate('/upgrade-waiting');
            }
        } catch (error) {
            // eslint-disable-next-line no-console -- the request interceptor already toasts; keep the error trace
            console.error('bundle burn', error);
            try {
                await bundleFinishReq('abort');
            } catch {
                /* device clears the session on timeout anyway */
            }
            /* The abort above (or an idle-timeout / rebooted device) leaves no
             * bundle session, so the plan still on screen is not backed by one
             * — a bare retry would fail begin with "No prechecked bundle".
             * Re-precheck to re-arm the session; skip choices stay as they
             * were. If the device is unreachable, fall back to file select. */
            let backToPlan = false;
            try {
                const header = bundleFile.slice(0, BUNDLE_HEADER_SIZE);
                const res = await bundlePreCheckReq(header);
                const data: BundlePreCheckRes = res?.data;
                if (data && Array.isArray(data.entries) && data.entries.length > 0) {
                    setPlan(data);
                    backToPlan = true;
                }
            } catch {
                /* request interceptor already toasts */
            }
            setPhase(backToPlan ? 'plan' : 'select');
            setUploadedBytes(0);
            setCurStep(0);
            setCurPercent(0);
        } finally {
            setIsRestarting(false);
        }
    };

    const renderUploadSlot = () => (
        <div className="flex flex-col gap-3 flex-1 items-center justify-center w-full h-full px-4 py-4 text-center">
            {bundleFile ? (
                <div className="flex flex-col items-center gap-3 pointer-events-none">
                    <div className="w-14 h-14 bg-primary/10 text-primary rounded-lg flex items-center justify-center">
                        <SvgIcon className="w-8 h-8" icon="file" />
                    </div>
                    <p className="text-sm text-text-primary text-wrap break-all max-w-[85%]">{bundleFile.name}</p>
                    <p className="text-xs text-text-secondary">{i18n._('common.reupload')}</p>
                </div>
            ) : (
                <div className="flex flex-col gap-2 items-center justify-center pointer-events-none">
                    <SvgIcon className="w-12 h-12 text-text-secondary" icon="upload_single" />
                    <p className="text-sm text-text-secondary">{i18n._('sys.system_management.bundle_file')}</p>
                    <p className="text-xs text-text-secondary/70">.bin</p>
                </div>
            )}
        </div>
    );

    const renderPlan = () => {
        if (!plan) return null;
        return (
            <div className="flex flex-col gap-3">
                <div className="flex items-center gap-2 flex-wrap">
                    <span className="text-xs px-2 py-1 rounded bg-red-500/10 text-red-500">
                        {i18n._('sys.system_management.bundle_mode_force')}
                    </span>
                    <span className="text-xs text-text-secondary">
                        {i18n._('sys.system_management.bundle_total')}:{' '}
                        {formatSize(burnBytes)}
                    </span>
                </div>
                {plan.layout_changed && (
                    <div className="flex flex-col gap-1">
                        <p className="text-xs text-red-500">
                            *{i18n._('sys.system_management.bundle_layout_warning')}
                        </p>
                        {/* per-partition diff straight from the device: its
                            compile-time table vs the bundle table */}
                        {!!plan.layout_diff?.length && (
                            <div className="flex flex-col gap-0.5 rounded-md bg-red-500/5 px-2 py-1.5">
                                {plan.layout_diff.map((d, idx) => (
                                    <p
                                      key={idx}
                                      className="text-xs text-red-500 font-mono break-all"
                                    >
                                        {d.part}:{' '}
                                        {partAddr(d.device_base, d.device_size)} →{' '}
                                        {partAddr(d.bundle_base, d.bundle_size)}
                                    </p>
                                ))}
                            </div>
                        )}
                        {/* data-bearing partitions moving = their content resets */}
                        {!!plan.layout_diff?.some((d) => d.part === 'NVS' || d.part === 'LITTLEFS') && (
                            <p className="text-xs text-red-500 font-bold">
                                *{i18n._('sys.system_management.bundle_data_loss_warning')}
                            </p>
                        )}
                    </div>
                )}
                <div className="flex flex-col gap-2">
                    {/* column header — the fixed column widths (size/addr/skip)
                        are shared with the rows below so every label lines up
                        with its data */}
                    <div className="flex items-center gap-4 px-3 text-xs font-medium text-text-primary">
                        <span className="w-16 shrink-0">
                            {i18n._('sys.system_management.bundle_col_fw')}
                        </span>
                        <span className="flex-1 min-w-0">
                            {i18n._('sys.system_management.bundle_col_version')}
                        </span>
                        <span className="w-[72px] shrink-0 text-right">
                            {i18n._('sys.system_management.bundle_col_size')}
                        </span>
                        <span className="w-[88px] shrink-0 text-right font-mono">
                            {i18n._('sys.system_management.bundle_col_addr')}
                        </span>
                        <span className="w-7 shrink-0 text-right">
                            {i18n._('sys.system_management.bundle_skip')}
                        </span>
                    </div>
                    {updateEntries.map((entry, idx) => {
                        const skipped = skipSet.has(entry.type);
                        const isCurrent = phase === 'burning'
                            && selectedEntries[curStep] === entry;
                        return (
                            <div
                              key={idx}
                              className={`flex items-center gap-4 rounded-md border px-3 py-2 ${
                                    isCurrent ? 'border-primary' : 'border-border'
                                } ${skipped ? 'opacity-50' : ''}`}
                            >
                                <span className="text-sm text-text-primary w-16 shrink-0">
                                    {FW_LABEL[entry.type] ?? entry.type}
                                </span>
                                <span className="text-xs text-text-secondary flex-1 min-w-0 text-wrap break-all">
                                    {entry.cur_version} → {entry.new_version}
                                </span>
                                <span className="text-xs text-text-secondary w-[72px] shrink-0 text-right">
                                    {formatSize(entry.size)}
                                </span>
                                {entry.addr ? (
                                    <span
                                      className="text-xs text-text-secondary/70 w-[88px] shrink-0 text-right font-mono"
                                      title={i18n._('sys.system_management.bundle_burn_addr')}
                                    >
                                        {hexAddr(entry.addr)}
                                    </span>
                                ) : null}
                                {entry.skippable && (
                                    <label
                                      className="flex w-7 shrink-0 items-center justify-end cursor-pointer"
                                      title={i18n._('sys.system_management.bundle_skip')}
                                    >
                                        <Checkbox
                                          checked={skipSet.has(entry.type)}
                                          disabled={phase === 'burning'}
                                          onCheckedChange={(v) => setSkipSet((prev) => {
                                              const next = new Set(prev);
                                              if (v) next.add(entry.type);
                                              else next.delete(entry.type);
                                              return next;
                                          })}
                                        />
                                    </label>
                                )}
                            </div>
                        );
                    })}
                </div>
                {phase === 'burning' && selectedEntries[curStep] && (
                    <div className="flex flex-col gap-2 mt-1">
                        <div className="flex items-center justify-between text-xs text-text-secondary">
                            <span>
                                {FW_LABEL[selectedEntries[curStep].type]} · {curStep + 1}/
                                {selectedEntries.length}
                            </span>
                            <span>{Math.floor(curPercent)}%</span>
                        </div>
                        <Progress value={curPercent} className="h-2" />
                        <div className="flex items-center justify-between text-xs text-text-secondary">
                            <span>{i18n._('sys.system_management.bundle_overall')}</span>
                            <span>{Math.floor(overallPercent)}%</span>
                        </div>
                        <Progress value={overallPercent} className="h-2" />
                    </div>
                )}
            </div>
        );
    };

    return (
        <div className="w-full h-full flex justify-center items-start pt-4">
            {/* Only mask once the device actually goes down for the reboot —
                during burning the network is up and the progress bars must
                stay visible/interactive. */}
            {isRestarting && (
                <WifiReloadMask
                  isLoading={isRestarting}
                  loadingText={i18n._('sys.system_management.firmware_upgrade_desc')}
                  maskText={i18n._('sys.system_management.network_disconnected')}
                />
            )}
            {burnShieldActive && createPortal(
                <div className="fixed inset-0 z-50 flex items-center justify-center">
                    {/* Full-viewport click shield: every menu/route control stays
                        unreachable while sub-firmwares stream; live progress
                        rides above it. */}
                    <div className="fixed inset-0 bg-black/70 backdrop-blur-sm" />
                    <div className="relative z-[51] flex flex-col gap-4 w-[min(92vw,28rem)] rounded-lg border border-border bg-background p-5 shadow-xl">
                        <div className="flex flex-col gap-1">
                            <p className="text-sm font-bold text-text-primary">
                                {i18n._('sys.system_management.bundle_burning_title')}
                            </p>
                            <p className="text-xs text-red-500">
                                *{i18n._('sys.system_management.bundle_burning_warning')}
                            </p>
                        </div>
                        <div className="flex flex-wrap gap-2">
                            {selectedEntries.map((entry, idx) => (
                                <span
                                  key={idx}
                                  className={`flex items-center gap-1 rounded-md border px-2 py-1 text-xs ${
                                        idx < curStep
                                            ? 'border-primary/40 text-primary'
                                            : idx === curStep
                                              ? 'border-primary text-primary animate-pulse'
                                              : 'border-border text-text-secondary'
                                    }`}
                                >
                                    <SvgIcon className="w-3.5 h-3.5" icon={idx < curStep ? 'check' : 'file'} />
                                    {FW_LABEL[entry.type] ?? entry.type}
                                </span>
                            ))}
                        </div>
                        <div className="flex flex-col gap-1">
                            <div className="flex items-center justify-between text-xs text-text-secondary">
                                <span>
                                    {FW_LABEL[selectedEntries[curStep]?.type] ?? ''} · {curStep + 1}/
                                    {selectedEntries.length}
                                </span>
                                <span>{Math.floor(curPercent)}%</span>
                            </div>
                            <Progress value={curPercent} className="h-2" />
                            <div className="flex items-center justify-between text-xs text-text-secondary">
                                <span>{i18n._('sys.system_management.bundle_overall')}</span>
                                <span>{Math.floor(overallPercent)}%</span>
                            </div>
                            <Progress value={overallPercent} className="h-2" />
                            {progressStalled && (
                                <p className="text-xs text-text-secondary animate-pulse">
                                    {i18n._('sys.system_management.bundle_erase_hint')}
                                </p>
                            )}
                        </div>
                    </div>
                </div>,
                document.body,
            )}
            <Card className="md:max-w-xl mx-4 w-full">
                <CardContent className="flex flex-col gap-4">
                    <div className="flex flex-col gap-1">
                        <p className="text-sm text-text-primary font-bold">
                            {i18n._('sys.system_management.bundle_upgrade_title')}
                        </p>
                        <p className="text-xs text-text-secondary">
                            *{i18n._('sys.system_management.bundle_file')}
                        </p>
                    </div>

                    {phase === 'select' && (
                        <Upload
                          className="h-48 w-full"
                          type="customZone"
                          slot={renderUploadSlot()}
                          accept={ACCEPT_FILE_TYPE}
                          maxFiles={1}
                          maxSize={MAX_SIZE}
                          multiple={false}
                          onFileChange={onBundleFileChange}
                        />
                    )}

                    {phase !== 'select' && phase !== 'done' && (
                        <>
                            <div className="flex flex-col gap-1 rounded-md bg-secondary/40 px-3 py-2">
                                <p className="text-xs text-text-primary text-wrap break-all">
                                    {bundleFile?.name}
                                </p>
                            </div>
                            {renderPlan()}
                            <div className="flex gap-2 justify-end">
                                <Button
                                  variant="outline"
                                  className="w-1/2 md:w-auto"
                                  disabled={phase === 'burning'}
                                  onClick={() => {
                                        setPhase('select');
                                        setBundleFile(null);
                                        setPlan(null);
                                    }}
                                >
                                    {i18n._('common.cancel')}
                                </Button>
                                <Button
                                  variant="primary"
                                  className="w-1/2 md:w-auto"
                                  disabled={phase === 'burning' || selectedEntries.length === 0}
                                  onClick={() => handleBurn()}
                                >
                                    {i18n._('sys.system_management.confirm_burn')}
                                </Button>
                            </div>
                        </>
                    )}

                    {phase === 'done' && (
                        <div className="flex flex-col items-center gap-3 py-8 text-center">
                            <div className="w-14 h-14 bg-primary/10 text-primary rounded-full flex items-center justify-center">
                                <SvgIcon className="w-8 h-8" icon="check" />
                            </div>
                            <p className="text-sm font-bold text-text-primary">
                                {i18n._('sys.system_management.bundle_done_title')}
                            </p>
                            <p className="text-xs text-text-secondary">
                                {i18n._('sys.system_management.bundle_done_desc')}
                            </p>
                            <Button
                              variant="primary"
                              onClick={() => window.location.reload()}
                            >
                                {i18n._('sys.system_management.bundle_done_reload')}
                            </Button>
                        </div>
                    )}
                </CardContent>
            </Card>
        </div>
    );
}
