import { useCallback, useEffect, useMemo, useState } from 'react';
import { useLingui } from '@lingui/react';
import { Separator } from '@/components/ui/separator';
import { Button } from '@/components/ui/button';
import { toast } from 'sonner';
import captureSettings, {
  type RecordInfo, type RecordState, type QueueStatus,
} from '@/services/api/captureSettings';
import fileManagement from '@/services/api/fileManagement';
import type { FsType } from '@/services/api/fileManagement';

function formatTimestamp(ts: number): string {
  if (!ts) return '—';
  const d = new Date(ts * 1000);
  if (Number.isNaN(d.getTime())) return '—';
  const pad = (n: number) => String(n).padStart(2, '0');
  return `${d.getFullYear()}-${pad(d.getMonth() + 1)}-${pad(d.getDate())} ${pad(d.getHours())}:${pad(d.getMinutes())}:${pad(d.getSeconds())}`;
}

function formatSize(bytes: number): string {
  if (!bytes) return '—';
  if (bytes < 1024) return `${bytes} B`;
  if (bytes < 1024 * 1024) return `${(bytes / 1024).toFixed(1)} KB`;
  return `${(bytes / (1024 * 1024)).toFixed(2)} MB`;
}

/** Convert local datetime string (YYYY-MM-DDTHH:mm) to Unix timestamp */
function toUnix(ts: string): number {
  if (!ts) return 0;
  return Math.floor(new Date(ts).getTime() / 1000);
}

/** Derive the UTC date dir name "YYYY-MM-DD" from a record's unix timestamp.
 * Must match the backend's date_dir_from_id() which uses gmtime() + "%Y-%m-%d".
 * Using UTC (not local TZ) is critical — a local-time computation would be off
 * by one day for records captured near midnight across a timezone boundary. */
function dateDir(ts: number): string {
  if (!ts) return 'unknown';
  const d = new Date(ts * 1000);
  if (Number.isNaN(d.getTime())) return 'unknown';
  return d.toISOString().slice(0, 10);
}

/** Derive the UTC hour dir "HH" (2-digit) — must match the backend's
 * hour_dir_from_id() which uses gmtime() + "%H". The on-disk layout is
 * /captures/data/<YYYY-MM-DD>/<HH>/<id>_p.jpg, so omitting the hour (as the old
 * code did) makes preview/download miss the file. */
function hourDir(ts: number): string {
  if (!ts) return '00';
  const d = new Date(ts * 1000);
  if (Number.isNaN(d.getTime())) return '00';
  return d.toISOString().slice(11, 13);
}

/** Parse the unix timestamp out of a record id "cap_<ts>_<seq>". The backend's
 * on-disk date/hour dirs are derived from THIS ts (via ts_from_id), so deriving
 * the path from the id (not r.timestamp, which can be 0) always matches. */
function tsFromId(id: string): number {
  const m = id.match(/^cap_(\d+)_\d+$/);
  return m ? parseInt(m[1], 10) : 0;
}

const TABS: { key: RecordState; labelKey: string }[] = [
  { key: 'pending', labelKey: 'sys.capture_settings.state_pending' },
  { key: 'sent',    labelKey: 'sys.capture_settings.state_sent' },
  { key: 'failed',  labelKey: 'sys.capture_settings.state_failed' },
  { key: 'local',   labelKey: 'sys.capture_settings.state_local' },
];

export default function CaptureRecords() {
  const { i18n } = useLingui();
  const [tab, setTab] = useState<RecordState>('pending');
  const [records, setRecords] = useState<RecordInfo[]>([]);
  const [total, setTotal] = useState(0);
  const [offset, setOffset] = useState(0);
  const [loading, setLoading] = useState(false);
  const [status, setStatus] = useState<QueueStatus | null>(null);
  const [previewUrl, setPreviewUrl] = useState<string | null>(null);
  const [previewTitle, setPreviewTitle] = useState('');
  const [previewLoading, setPreviewLoading] = useState(false);

  /* Batch selection */
  const [selected, setSelected] = useState<Set<string>>(new Set());

  /* Time filter — input values (not yet applied) */
  const [filterFrom, setFilterFrom] = useState('');
  const [filterTo, setFilterTo] = useState('');
  /* Active filter (applied on button click after validation) */
  const [appliedFrom, setAppliedFrom] = useState<number | undefined>(undefined);
  const [appliedTo, setAppliedTo] = useState<number | undefined>(undefined);
  const [sortNewest, setSortNewest] = useState(true);

  const PAGE = 20;
  const fsType: FsType = (status?.actual_fs === 'sd' ? 'sd' : 'flash');

  const load = useCallback(async (st: RecordState, off: number) => {
    setLoading(true);
    try {
      const [recResp, stResp] = await Promise.all([
        captureSettings.listRecords({
          state: st,
offset: off,
limit: PAGE,
          from: appliedFrom,
to: appliedTo,
          sort: sortNewest ? 'desc' : 'asc',
        }),
        captureSettings.getQueueStatus(),
      ]);
      setRecords(recResp.data.records ?? []);
      setTotal(recResp.data.total ?? 0);
      setStatus(stResp.data);
    } catch (e) {
      console.error(e);
    } finally {
      setLoading(false);
    }
  }, [appliedFrom, appliedTo, sortNewest]);

  useEffect(() => { setSelected(new Set()); load(tab, offset); }, [tab, offset, load]);

  /* Preview helpers */
  const previewImage = async (r: RecordInfo, type: 'p' | 'i') => {
    setPreviewTitle(type === 'p'
      ? i18n._('sys.capture_settings.preview_primary')
      : i18n._('sys.capture_settings.preview_inference'));
    setPreviewLoading(true);
    setPreviewUrl(null);
    try {
      const ts = tsFromId(r.id) || r.timestamp;
      const path = `/captures/data/${dateDir(ts)}/${hourDir(ts)}/${r.id}_${type}.jpg`;
      const res: any = await fileManagement.preview(fsType, path, true);
      const blob = res instanceof Blob ? res : res?.data;
      if (blob instanceof Blob) {
        setPreviewUrl(URL.createObjectURL(blob));
      } else {
        toast.error(i18n._('sys.capture_settings.preview_failed') ?? 'Preview failed');
      }
    } catch (e) {
      console.error(e);
      toast.error(i18n._('sys.capture_settings.preview_failed') ?? 'Preview failed');
    } finally {
      setPreviewLoading(false);
    }
  };

  const closePreview = () => {
    if (previewUrl) URL.revokeObjectURL(previewUrl);
    setPreviewUrl(null);
    setPreviewLoading(false);
  };

  const switchTab = (t: RecordState) => { setTab(t); setOffset(0); };

  /* ── Batch selection helpers ── */
  const allOnPage = useMemo(() => records.filter(r => selected.has(r.id)), [records, selected]);
  const allSelected = records.length > 0 && allOnPage.length === records.length;
  const someSelected = allOnPage.length > 0;

  const toggleSelect = (id: string) => {
    setSelected(prev => {
      const next = new Set(prev);
      if (next.has(id)) next.delete(id); else next.add(id);
      return next;
    });
  };

  const toggleSelectAll = () => {
    if (allSelected) {
      setSelected(prev => {
        const next = new Set(prev);
        records.forEach(r => next.delete(r.id));
        return next;
      });
    } else {
      setSelected(prev => {
        const next = new Set(prev);
        records.forEach(r => next.add(r.id));
        return next;
      });
    }
  };

  /* ── Batch actions ── */
  const batchRetry = async () => {
    if (allOnPage.length === 0) return;
    try {
      const resp = await captureSettings.retryRecords(allOnPage.map(rec => rec.id));
      toast.success(`${i18n._('sys.capture_settings.batch_retry_ok')} (${resp.data?.reset_count ?? 0})`);
      setSelected(new Set());
      load(tab, offset);
    } catch { toast.error('Batch retry failed'); }
  };

  const batchDelete = async () => {
    if (allOnPage.length === 0) return;
    // eslint-disable-next-line no-alert, no-restricted-globals
    if (!confirm(i18n._('sys.capture_settings.confirm_delete_batch')?.replace('{n}', String(allOnPage.length)) ?? 'Confirm delete?')) return;
    try {
      const resp = await captureSettings.deleteRecords(allOnPage.map(rec => rec.id));
      toast.success(`${i18n._('sys.capture_settings.batch_delete_ok')} (${resp.data?.deleted_count ?? 0})`);
      setSelected(new Set());
      load(tab, offset);
    } catch { toast.error('Batch delete failed'); }
  };

  /* ── Single actions ── */
  const retryOne = async (id: string) => {
    try {
      await captureSettings.retryRecord(id);
      toast.success(i18n._('sys.capture_settings.retry_ok'));
      load(tab, offset);
    } catch { toast.error('Retry failed'); }
  };

  const retryAll = async () => {
    try {
      const r = await captureSettings.retryAllFailed();
      toast.success(`${i18n._('sys.capture_settings.retry_all_ok')} (${r.data?.reset_count ?? 0})`);
      load(tab, offset);
    } catch { toast.error('Retry all failed'); }
  };

  const delOne = async (id: string) => {
    try {
      await captureSettings.deleteRecord(id);
      load(tab, offset);
    } catch { toast.error('Delete failed'); }
  };

  /* ── Time filter apply / clear ── */
  const applyFilter = () => {
    const from = filterFrom.trim();
    const to = filterTo.trim();
    if (!from || !to) {
      toast.error(i18n._('sys.capture_settings.filter_both_required') ?? 'Please set both From and To');
      return;
    }
    const fromTs = toUnix(from);
    const toTs = toUnix(to);
    if (Number.isNaN(fromTs) || Number.isNaN(toTs)) {
      toast.error(i18n._('sys.capture_settings.filter_invalid_date') ?? 'Invalid date');
      return;
    }
    if (fromTs >= toTs) {
      toast.error(i18n._('sys.capture_settings.filter_from_before_to') ?? 'From must be before To');
      return;
    }
    setAppliedFrom(fromTs);
    setAppliedTo(toTs);
    setOffset(0);
  };

  const clearFilter = () => {
    setFilterFrom('');
    setFilterTo('');
    setAppliedFrom(undefined);
    setAppliedTo(undefined);
    setOffset(0);
  };

  return (
    <div className="flex flex-col gap-2 mt-4">
      {/* Status bar */}
      {status && (
        <div className="text-xs text-gray-600 flex flex-wrap gap-x-4 gap-y-1 bg-gray-100 p-2 rounded">
          <span>{i18n._('sys.capture_settings.state_pending')}: {status.pending_count}</span>
          <span>{i18n._('sys.capture_settings.state_sent')}: {status.sent_count}</span>
          <span>{i18n._('sys.capture_settings.state_failed')}: {status.failed_count}</span>
          <span>{i18n._('sys.capture_settings.state_local')}: {status.local_count}</span>
          <span>{i18n._('sys.capture_settings.free_space')}: {((status.bytes_available_kb ?? 0) / 1024).toFixed(0)} MB</span>
          {status.storage_full && (
            <span className="text-red-500 font-bold">{i18n._('sys.capture_settings.storage_full')}</span>
          )}
        </div>
      )}

      {/* Time filter bar */}
      <div className="flex flex-wrap gap-2 items-center text-xs">
        <label className="text-gray-500">{i18n._('sys.capture_settings.filter_from')}:</label>
        <input
          type="datetime-local"
          className="inline-block w-48 h-7 text-xs rounded border border-gray-300 bg-gray-100 px-2 py-0.5"
          value={filterFrom}
          onChange={(e: any) => setFilterFrom(e.target.value)}
        />
        <label className="text-gray-500 ml-2">{i18n._('sys.capture_settings.filter_to')}:</label>
        <input
          type="datetime-local"
          className="inline-block w-48 h-7 text-xs rounded border border-gray-300 bg-gray-100 px-2 py-0.5"
          value={filterTo}
          onChange={(e: any) => setFilterTo(e.target.value)}
        />
        <Button variant="outline" size="sm" onClick={applyFilter}>
          {i18n._('sys.capture_settings.filter_apply')}
        </Button>
        {(filterFrom || filterTo) && (
          <Button variant="ghost" size="sm" onClick={clearFilter}>
            {i18n._('sys.capture_settings.filter_clear')}
          </Button>
        )}
        {appliedFrom !== undefined && (
          <span className="text-blue-600 ml-1 text-xs">
            {i18n._('sys.capture_settings.filter_active')}
          </span>
        )}
        <span className="ml-auto text-gray-400">
          {i18n._('sys.capture_settings.sort_by_time')}:
        </span>
        <Button
          variant={sortNewest ? 'primary' : 'outline'}
          size="sm"
          onClick={() => { setSortNewest(true); setOffset(0); }}
        >
          {i18n._('sys.capture_settings.sort_newest')}
        </Button>
        <Button
          variant={!sortNewest ? 'primary' : 'outline'}
          size="sm"
          onClick={() => { setSortNewest(false); setOffset(0); }}
        >
          {i18n._('sys.capture_settings.sort_oldest')}
        </Button>
      </div>

      {/* Batch action toolbar */}
      {someSelected && (
        <div className="flex gap-2 items-center bg-blue-50 border border-blue-200 rounded p-2 text-sm">
          <span className="text-blue-700">
            {i18n._('sys.capture_settings.selected_count')?.replace('{n}', String(allOnPage.length)) ?? `${allOnPage.length} selected`}
          </span>
          <div className="flex-1" />
          {tab === 'failed' && (
            <Button variant="primary" size="sm" onClick={batchRetry}>
              {i18n._('sys.capture_settings.retry_selected')}
            </Button>
          )}
          <Button variant="destructive" size="sm" onClick={batchDelete}>
            {i18n._('sys.capture_settings.delete_selected')}
          </Button>
          <Button
            variant="ghost"
            size="sm"
            onClick={() => setSelected(new Set())}
          >
            {i18n._('sys.capture_settings.deselect_all')}
          </Button>
        </div>
      )}

      {/* Tab switcher */}
      <div className="flex gap-2 flex-wrap items-center">
        {TABS.map((t) => (
          <Button
            key={t.key}
            variant={tab === t.key ? 'primary' : 'outline'}
            size="sm"
            onClick={() => switchTab(t.key)}
          >
            {i18n._(t.labelKey)}
          </Button>
        ))}
        <div className="flex-1" />
        {/* Select-all checkbox (header level) */}
        <label className="flex items-center gap-1 text-xs text-gray-500 cursor-pointer select-none">
          <input
            type="checkbox"
            checked={allSelected}
            onChange={toggleSelectAll}
            className="w-4 h-4"
          />
          {i18n._('sys.capture_settings.select_all')}
        </label>
        {tab === 'failed' && (
          <Button variant="outline" size="sm" onClick={retryAll}>
            {i18n._('sys.capture_settings.retry_all')}
          </Button>
        )}
      </div>

      <Separator />

      {loading && <div className="text-center py-4 text-gray-500">Loading...</div>}

      {!loading && records.length === 0 && (
        <div className="text-center py-4 text-gray-500">{i18n._('sys.capture_settings.no_records')}</div>
      )}

      {!loading && records.map((r) => (
        <div
          key={r.id}
          className={`border rounded p-3 flex flex-col gap-1.5 text-sm ${
          selected.has(r.id) ? 'border-blue-400 bg-blue-50' : 'border-gray-200'
        }`}
        >
          {/* Row 0: checkbox + id + timestamp */}
          <div className="flex items-center gap-2">
            <input
              type="checkbox"
              checked={selected.has(r.id)}
              onChange={() => toggleSelect(r.id)}
              className="w-4 h-4 shrink-0"
            />
            <span className="font-mono text-xs text-gray-700 truncate">{r.id || '—'}</span>
            <span className="text-xs text-gray-500 whitespace-nowrap ml-auto">{formatTimestamp(r.timestamp)}</span>
          </div>

          {/* Row 1: trigger + size */}
          <div className="flex gap-4 text-gray-600 text-xs ml-6">
            <span className="min-w-[60px]">
              <span className="text-gray-400">{i18n._('sys.capture_settings.col_trigger')}: </span>
              {r.trigger || '—'}
            </span>
            <span className="min-w-[60px]">
              <span className="text-gray-400">{i18n._('sys.capture_settings.col_size')}: </span>
              {formatSize(r.size)}
            </span>
          </div>

          {/* Row 2: action buttons */}
          <div className="flex gap-2 mt-1 ml-6">
            <Button variant="outline" size="sm" onClick={() => previewImage(r, 'p')}>
              {i18n._('sys.capture_settings.preview_primary')}
            </Button>
            {r.has_inference && (
              <Button variant="outline" size="sm" onClick={() => previewImage(r, 'i')}>
                {i18n._('sys.capture_settings.preview_inference')}
              </Button>
            )}
            {tab === 'failed' && (
              <Button variant="outline" size="sm" onClick={() => retryOne(r.id)}>
                {i18n._('sys.capture_settings.retry')}
              </Button>
            )}
            <Button
              variant="outline"
              size="sm"
              className="ml-auto"
              // eslint-disable-next-line no-alert, no-restricted-globals
              onClick={() => { if (confirm(i18n._('sys.capture_settings.confirm_delete'))) delOne(r.id); }}
            >
              {i18n._('common.delete')}
            </Button>
          </div>

          {/* Row 3: retry/error info */}
          {(r.retry_count > 0 || (r.last_error && r.last_error.length > 0)) && (
            <div className="flex gap-4 text-xs ml-6">
              {r.retry_count > 0 && (
                <span className="text-orange-600">
                  {i18n._('sys.capture_settings.col_retry')}: {r.retry_count}
                </span>
              )}
              {r.last_error && r.last_error.length > 0 && (
                <span className="text-red-500 truncate">
                  {i18n._('sys.capture_settings.col_error')}: {r.last_error}
                </span>
              )}
            </div>
          )}
        </div>
      ))}

      {/* Pagination — always visible when there are records */}
      {total > 0 && (
        <div className="flex justify-center gap-2 mt-2 items-center">
          <Button
            variant="outline"
            size="sm"
            disabled={offset === 0}
            onClick={() => setOffset(Math.max(0, offset - PAGE))}
          >
            ← {i18n._('sys.capture_settings.page_prev')}
          </Button>
          <span className="text-sm text-gray-600 min-w-[120px] text-center">
            {offset + 1}–{Math.min(offset + PAGE, total)} / {total}
          </span>
          <Button
            variant="outline"
            size="sm"
            disabled={offset + PAGE >= total}
            onClick={() => setOffset(offset + PAGE)}
          >
            {i18n._('sys.capture_settings.page_next')} →
          </Button>
        </div>
      )}

      {/* Image preview modal */}
      {(previewUrl || previewLoading) && (
        <div
          className="fixed inset-0 z-[60] flex items-center justify-center bg-black/50 p-4"
          onClick={closePreview}
        >
          <div
            className="bg-white rounded-lg shadow-2xl w-full max-w-4xl max-h-[85vh] flex flex-col"
            onClick={(e) => e.stopPropagation()}
          >
            <div className="flex items-center justify-between px-5 py-3 border-b shrink-0">
              <span className="text-sm font-medium">👁 {previewTitle}</span>
              <button
                className="text-gray-400 hover:text-gray-600 text-xl px-2"
                onClick={closePreview}
              >✕
              </button>
            </div>
            <div className="flex-1 overflow-auto min-h-0">
              {previewLoading ? (
                <div className="text-center py-12 text-gray-400">Loading...</div>
              ) : previewUrl ? (
                <div className="flex items-center justify-center p-4 h-full">
                  <img
                    src={previewUrl}
                    alt={previewTitle}
                    className="max-w-full max-h-full object-contain rounded"
                  />
                </div>
              ) : null}
            </div>
          </div>
        </div>
      )}
    </div>
  );
}
