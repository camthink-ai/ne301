import { useState, useEffect, useCallback, useRef } from 'preact/hooks';
import { useLingui } from '@lingui/react';
import { Card, CardTitle, CardContent } from '@/components/ui/card';
import { Label } from '@/components/ui/label';
import { Separator } from '@/components/ui/separator';
import { Progress } from '@/components/ui/progress';
import { Button } from '@/components/ui/button';
import { Input } from '@/components/ui/input';
import { toast } from 'sonner';
import storageManagement from '@/services/api/storageManagement';
import fileManagement, { type FileEntry, type FsType } from '@/services/api/fileManagement';
import loginApis from '@/services/api/login';
import FormatFlashDialog from '@/components/format-flash-dialog';
import StorageManagementSkeleton from './skeleton';

/* ── helpers ── */

function formatCapacity(mb: number): string {
  if (mb < 100) return `${mb.toFixed(1)} MB`;
  return `${(mb / 1024).toFixed(2)} GB`;
}

function formatSize(bytes: number): string {
  if (bytes === 0) return '—';
  if (bytes < 1024) return `${bytes} B`;
  if (bytes < 1024 * 1024) return `${(bytes / 1024).toFixed(1)} KB`;
  if (bytes < 1024 * 1024 * 1024) return `${(bytes / (1024 * 1024)).toFixed(1)} MB`;
  return `${(bytes / (1024 * 1024 * 1024)).toFixed(2)} GB`;
}

function formatTime(ts: number): string {
  if (!ts) return '—';
  const d = new Date(ts * 1000);
  return `${d.getUTCFullYear()}-${ 
    String(d.getUTCMonth() + 1).padStart(2, '0')}-${ 
    String(d.getUTCDate()).padStart(2, '0')} ${ 
    String(d.getUTCHours()).padStart(2, '0')}:${ 
    String(d.getUTCMinutes()).padStart(2, '0')}`;
}

function isTextFile(name: string): boolean {
  const ext = name.split('.').pop()?.toLowerCase();
  return ['txt', 'json', 'csv', 'log', 'xml', 'yml', 'yaml', 'cfg', 'ini',
    'html', 'htm', 'css', 'js', 'md', 'sh', 'py', 'c', 'h', 'cpp', 'hpp'].includes(ext || '');
}

function isImageFile(name: string): boolean {
  const ext = name.split('.').pop()?.toLowerCase();
  return ['jpg', 'jpeg', 'png', 'bmp', 'gif', 'svg', 'ico'].includes(ext || '');
}

function isEditableFile(name: string): boolean { return isTextFile(name); }

function getFileIcon(name: string): string {
  const ext = name.split('.').pop()?.toLowerCase();
  if (['jpg', 'jpeg', 'png', 'bmp', 'gif', 'svg', 'ico'].includes(ext || '')) return '🖼️';
  if (['mp4', 'h264', 'avi', 'mkv', 'mov', 'webm', 'flv'].includes(ext || '')) return '🎬';
  if (['mp3', 'wav', 'aac', 'ogg', 'flac', 'wma'].includes(ext || '')) return '🎵';
  if (['zip', 'rar', '7z', 'tar', 'gz', 'bz2', 'xz'].includes(ext || '')) return '📦';
  if (['pdf'].includes(ext || '')) return '📕';
  if (['bin', 'elf', 'hex', 'img'].includes(ext || '')) return '💿';
  return '📄';
}

/* ── types ── */

type StorageInfo = {
  sd_card_connected: boolean;
  total_capacity_mb: number; used_capacity_mb: number; available_capacity_mb: number;
  total_capacity_gb: number; used_capacity_gb: number; available_capacity_gb: number;
  usage_percent: number;
  flash_fs_mounted: boolean;
  flash_fs_error: boolean;            // Flash FS error — needs format
  flash_error: string;                // "not_mounted" | "corrupt" | ""
  flash_total_capacity_mb: number; flash_used_capacity_mb: number; flash_available_capacity_mb: number;
  flash_total_capacity_gb: number; flash_used_capacity_gb: number; flash_available_capacity_gb: number;
  flash_usage_percent: number; flash_fs_type: string;
  cyclic_overwrite_enabled: boolean; overwrite_threshold_percent: number;
  status: string; primary_storage: string;
};

type StorageProgress = { used: number; total: number; color: string };

/* ── File Browser Modal ── */

function FileBrowserModal({ fsType, onClose, availableMB, onStorageChange }: { fsType: FsType; onClose: () => void; availableMB: number; onStorageChange: () => void }) {
  const { i18n } = useLingui();
  const [currentPath, setCurrentPath] = useState('/');
  const [entries, setEntries] = useState<FileEntry[]>([]);
  const [loading, setLoading] = useState(false);
  const [uploading, setUploading] = useState(false);
  const [uploadProgress, setUploadProgress] = useState(0);
  const [downloadProgress, setDownloadProgress] = useState<{ name: string; pct: number } | null>(null);
  const fileInputRef = useRef<HTMLInputElement>(null);

  // create dialog
  const [createDialog, setCreateDialog] = useState<{ type: 'file' | 'dir' } | null>(null);
  const [createName, setCreateName] = useState('');
  const [creating, setCreating] = useState(false);

  // preview
  const [previewFile, setPreviewFile] = useState<string | null>(null);
  const [previewContent, setPreviewContent] = useState('');
  const [previewImageUrl, setPreviewImageUrl] = useState('');
  const [previewLoading, setPreviewLoading] = useState(false);

  // edit
  const [editFile, setEditFile] = useState<string | null>(null);
  const [editContent, setEditContent] = useState('');
  const [editSaving, setEditSaving] = useState(false);

  // delete confirm
  const [deleteTarget, setDeleteTarget] = useState<{ name: string; path: string; isDir: boolean } | null>(null);
  const [deleteInput, setDeleteInput] = useState('');

  // rename
  const [renameTarget, setRenameTarget] = useState<{ name: string; path: string } | null>(null);
  const [renameInput, setRenameInput] = useState('');

  // multi-select
  const [selected, setSelected] = useState<Set<string>>(new Set());
  const [batchDeleting, setBatchDeleting] = useState(false);
  const [batchDeleteConfirm, setBatchDeleteConfirm] = useState<string | null>(null);

  // kebab menu
  const [menuOpen, setMenuOpen] = useState<string | null>(null);

  // format flash (from within file browser)
  const [formatDialogOpen, setFormatDialogOpen] = useState(false);
  const [formatPassword, setFormatPassword] = useState('');
  const [formatPasswordVisible, setFormatPasswordVisible] = useState(false);
  const [formatVerifying, setFormatVerifying] = useState(false);
  const [formatPasswordError, setFormatPasswordError] = useState('');
  const [formatting, setFormatting] = useState(false);

  const t = (key: string) => i18n._(`sys.file_browsing.${key}`);

  const loadDir = useCallback(async (path: string) => {
    setLoading(true);
    setEntries([]);  // clear old list immediately to prevent misclicks
    try {
      const res = await fileManagement.listDir(fsType, path);
      const d = (res && 'data' in res) ? (res as any).data : res;
      if (d?.error) {
        toast.error(d.error);
        setEntries([]);
      } else {
        const sorted = [...(d?.entries || [])].sort((a: FileEntry, b: FileEntry) => {
          if (a.type !== b.type) return a.type === 'dir' ? -1 : 1;
          return a.name.localeCompare(b.name);
        });
        setEntries(sorted);
      }
    } catch { toast.error(t('load_failed')); setEntries([]); } finally { setLoading(false); }
  }, [fsType]);

  useEffect(() => { loadDir(currentPath); }, [currentPath, loadDir]);

  // close floating action bar on outside click
  useEffect(() => {
    if (!menuOpen) return;
    const handler = (e: MouseEvent) => {
      // Don't close if clicking the ⋮ button (it toggles)
      const target = e.target as HTMLElement;
      if (target.closest('[data-action-bar]')) return;
      setMenuOpen(null);
    };
    document.addEventListener('mousedown', handler);
    return () => document.removeEventListener('mousedown', handler);
  }, [menuOpen]);

  const navigateTo = (path: string) => {
    setMenuOpen(null);
    setPreviewFile(null); setPreviewImageUrl(''); setEditFile(null);
    setCurrentPath(path);
  };

  const breadcrumbs = (() => {
    const parts = currentPath.split('/').filter(Boolean);
    const crumbs = [{ label: 'Home', fullPath: '/' }];
    let acc = '';
    for (const p of parts) { acc += `/${p}`; crumbs.push({ label: p, fullPath: acc }); }
    return crumbs;
  })();

  const fullPath = (name: string) => (currentPath === '/' ? `/${name}` : `${currentPath}/${name}`);

  // preview
  const handlePreview = async (entry: FileEntry) => {
    const fp = fullPath(entry.name);
    setPreviewFile(entry.name); setEditFile(null); setPreviewLoading(true);
    if (isImageFile(entry.name)) {
      try {
        setPreviewContent(''); setPreviewImageUrl('');
        const res = await fileManagement.preview(fsType, fp, true);
        const blob = res instanceof Blob ? res : (res as any)?.data;
        if (blob) setPreviewImageUrl(URL.createObjectURL(blob));
      } catch { toast.error(t('preview_failed')); }
    } else {
      try {
        setPreviewImageUrl('');
        const res = await fileManagement.preview(fsType, fp, false);
        const d = (res && 'data' in res) ? (res as any).data : res;
        if (d?.size === 0) {
          setPreviewContent(t('empty_file'));
        } else if (d?.too_large) {
          setPreviewContent(t('file_too_large'));
        } else {
          setPreviewContent(d?.content || '');
        }
      } catch { toast.error(t('preview_failed')); }
    }
    setPreviewLoading(false);
  };

  const closePreview = () => {
    if (previewImageUrl) URL.revokeObjectURL(previewImageUrl);
    setPreviewFile(null); setPreviewContent(''); setPreviewImageUrl('');
  };

  // edit
  const handleEdit = async (entry: FileEntry) => {
    const fp = fullPath(entry.name);
    try {
      const res = await fileManagement.preview(fsType, fp, false);
      const d = (res && 'data' in res) ? (res as any).data : res;
      if (d?.too_large) { toast.error(t('file_too_large')); return; }
      setEditFile(entry.name); setEditContent(d?.content || '');
      setPreviewFile(null); setPreviewImageUrl('');
    } catch { toast.error(t('edit_load_failed')); }
  };

  const handleSaveEdit = async () => {
    if (!editFile) return;
    setEditSaving(true);
    try {
      await fileManagement.edit(fsType, fullPath(editFile), editContent);
      toast.success(t('edit_saved'));
      onStorageChange();
      setEditFile(null);
      loadDir(currentPath);
    } catch { toast.error(t('edit_save_failed')); } finally { setEditSaving(false); }
  };

  // download
  const handleDownload = async (entry: FileEntry) => {
    const fp = fullPath(entry.name);
    setDownloadProgress({ name: entry.name, pct: 0 });
    try {
      const res = await fileManagement.download(
fsType, 
fp,
(pct) => setDownloadProgress({ name: entry.name, pct })
);
      setDownloadProgress(null);
      // res might be the raw axios response or a blob — check carefully
      const blob = res instanceof Blob ? res : (res as any)?.data;
      if (!blob || !(blob instanceof Blob)) {
        toast.error(t('download_failed'));
        return;
      }
      // Check it's not a JSON error response masquerading as a blob
      if (blob.type === 'application/json' || blob.size < 100) {
        const text = await blob.text();
        try { const j = JSON.parse(text); if (j && j.success === false) { toast.error(j.message || t('download_failed')); return; } } catch { /* not JSON error, proceed */ }
      }
      const url = URL.createObjectURL(blob);
      const a = document.createElement('a');
      a.href = url; a.download = entry.name; document.body.appendChild(a);
      a.click(); document.body.removeChild(a);
      URL.revokeObjectURL(url);
    } catch { toast.error(t('download_failed')); setDownloadProgress(null); }
  };

  // delete
  const handleDeleteClick = (entry: FileEntry) => {
    setDeleteTarget({ name: entry.name, path: fullPath(entry.name), isDir: entry.type === 'dir' });
    setDeleteInput('');
  };

  const handleDeleteConfirm = async () => {
    if (!deleteTarget || deleteInput !== deleteTarget.name) return;
    const target = deleteTarget;
    setDeleteTarget(null);  // close dialog immediately to prevent double-click
    try {
      await fileManagement.deleteFile(fsType, target.path, target.isDir);
      toast.success(t('delete_success'));
      onStorageChange();
      loadDir(currentPath);
    } catch { toast.error(t('delete_failed')); }
  };

  const handleBatchDelete = async () => {
    if (selected.size === 0) return;
    const toDelete = Array.from(selected);
    setBatchDeleting(true);
    setBatchDeleteConfirm(null);  // close dialog immediately
    setSelected(new Set());
    let ok = 0;
    for (const name of toDelete) {
      const entry = entries.find(e => e.name === name);
      const fp = fullPath(name);
      const isDir = entry?.type === 'dir';
      // eslint-disable-next-line no-await-in-loop
      try { await fileManagement.deleteFile(fsType, fp, isDir); ok++; } catch { toast.error(`${t('delete_failed')}: ${name}`); }
    }
    if (ok > 0) toast.success(t('delete_success'));
    onStorageChange();
    setBatchDeleting(false);
    setSelected(new Set());
    setBatchDeleteConfirm(null);
    loadDir(currentPath);
  };

  // rename
  const handleRenameClick = (entry: FileEntry) => {
    const fp = fullPath(entry.name);
    setRenameTarget({ name: entry.name, path: fp });
    setRenameInput(entry.name);
  };

  const handleRenameConfirm = async () => {
    if (!renameTarget || !renameInput.trim() || renameInput.trim() === renameTarget.name) return;
    setRenameTarget(null);  // close dialog immediately to prevent double-click
    const oldPath = renameTarget.path;
    const newName = renameInput.trim();
    const newFullPath = currentPath === '/' ? `/${newName}` : `${currentPath}/${newName}`;
    try {
      await fileManagement.rename(fsType, oldPath, newFullPath);
      toast.success(t('rename_success'));
      loadDir(currentPath);
    } catch { toast.error(t('rename_failed')); }
  };

  // upload
  const handleUpload = async (e: Event) => {
    const input = e.target as HTMLInputElement;
    const files = input?.files;
    if (!files?.length) return;

    const MAX_MB = 16;  // matches backend FILE_UPLOAD_FLASH_MAX_SIZE / SD_MAX_SIZE
    const MAX_BYTES = MAX_MB * 1024 * 1024;
    const availableBytes = availableMB * 1024 * 1024;

    // Check size limits before uploading
    for (const file of Array.from(files)) {
      if (file.size > MAX_BYTES) {
        toast.error(`${t('file_too_large_limit')}: ${file.name} (max ${MAX_MB}MB)`);
        input.value = '';
        return;
      }
      if (file.size > availableBytes) {
        toast.error(`${t('no_space')}: ${file.name}`);
        input.value = '';
        return;
      }
    }
    setUploading(true);
    setUploadProgress(0);
    let ok = 0;
    for (const file of Array.from(files)) {
      try {
        // eslint-disable-next-line no-await-in-loop
        await fileManagement.upload(
fsType, 
currentPath, 
file,
(pct) => setUploadProgress(pct)
);
        ok++;
      } catch { toast.error(`${t('upload_failed')}: ${file.name}`); }
    }
    if (ok > 0) toast.success(t('upload_success'));
    onStorageChange();
    setUploading(false);
    setUploadProgress(0);
    if (fileInputRef.current) fileInputRef.current.value = '';
    // Refresh immediately — server is ready since response already sent
    loadDir(currentPath);
  };

  // create
  const handleCreate = async () => {
    if (!createDialog || !createName.trim()) return;
    setCreating(true);
    try {
      await fileManagement.create(fsType, currentPath, createName.trim(), createDialog.type);
      toast.success(t(createDialog.type === 'dir' ? 'mkdir_success' : 'touch_success'));
      onStorageChange();
      setCreateDialog(null);
      setCreateName('');
      loadDir(currentPath);
    } catch { toast.error(t('create_failed')); } finally { setCreating(false); }
  };

  // format flash with password verification
  const handleFormatClick = () => {
    setFormatPassword('');
    setFormatPasswordError('');
    setFormatDialogOpen(true);
  };

  const handleFormatVerify = async () => {
    if (!formatPassword.trim()) return;
    setFormatVerifying(true);
    setFormatPasswordError('');
    try {
      await loginApis.verifyPassword(formatPassword.trim());
      // password correct — close dialog and start formatting
      setFormatDialogOpen(false);
      setFormatPassword('');
      setFormatting(true);
      try {
        await storageManagement.formatFlash();
        toast.success(t('format_success'));
        onStorageChange();
        onClose(); // close file browser after format
      } catch {
        toast.error(t('format_failed'));
      } finally {
        setFormatting(false);
      }
    } catch {
      setFormatPasswordError(t('format_wrong_password'));
    } finally {
      setFormatVerifying(false);
    }
  };

  const handleFormatDialogClose = () => {
    if (formatVerifying || formatting) return;
    setFormatDialogOpen(false);
    setFormatPassword('');
    setFormatPasswordError('');
  };

  return (
    <div className="fixed inset-0 z-50 flex items-center justify-center bg-black/50">
      <div className="bg-white rounded-lg shadow-2xl w-[95vw] max-w-4xl max-h-[90vh] flex flex-col mx-4">
        {/* Header */}
        <div className="flex items-center justify-between px-4 py-3 border-b shrink-0">
          <div className="flex items-center gap-2 text-sm font-medium">
            <span>{fsType === 'flash' ? '💾 Flash' : '💳 SD'}</span>
            <span className="text-gray-300">|</span>
            <div className="flex items-center gap-0.5">
              {breadcrumbs.map((crumb, i) => (
                <span key={crumb.fullPath} className="flex items-center gap-0.5">
                  {i > 0 && <span className="text-gray-300 mx-0.5">/</span>}
                  {i < breadcrumbs.length - 1 ? (
                    <button
                      className="text-blue-600 hover:underline cursor-pointer text-sm"
                      onClick={() => navigateTo(crumb.fullPath)}
                    >
                      {crumb.label}
                    </button>
                  ) : (
                    <span className="text-gray-700 text-sm">{crumb.label}</span>
                  )}
                </span>
              ))}
            </div>
          </div>
          <button
            className="text-gray-400 hover:text-gray-600 text-xl leading-none px-2"
            onClick={onClose}
          >✕
          </button>
        </div>

        {/* Toolbar */}
        <div className="flex items-center gap-2 px-4 py-2 border-b shrink-0 flex-wrap">
          <input
            type="file"
            multiple
            className="hidden"
            ref={fileInputRef}
            onChange={handleUpload}
            disabled={uploading}
          />
          <Button
            size="sm"
            variant="outline"
            disabled={uploading}
            onClick={() => fileInputRef.current?.click()}
          >
            📤 {t('upload')}
          </Button>
          <Button
            size="sm"
            variant="outline"
            onClick={() => { setCreateDialog({ type: 'dir' }); setCreateName(''); }}
          >
            📁 {t('new_folder')}
          </Button>
          <Button
            size="sm"
            variant="outline"
            onClick={() => { setCreateDialog({ type: 'file' }); setCreateName(''); }}
          >
            📄 {t('new_file')}
          </Button>
          {selected.size > 0 && (
            <Button
              size="sm"
              variant="destructive"
              onClick={() => setBatchDeleteConfirm(`delete_${Date.now()}`)}
            >
              🗑 {t('delete_selected')} ({selected.size})
            </Button>
          )}
          <div className="flex-1" />
          {fsType === 'flash' && (
            <Button
              size="sm"
              variant="outline"
              onClick={handleFormatClick}
              title={t('format_flash')}
            >
              ⚡ {t('format_flash')}
            </Button>
          )}
          {(loading || uploading || downloadProgress) && (
            <div className="flex items-center gap-2">
              {(uploading || downloadProgress) ? (
                <div className="flex items-center gap-2">
                  <span className="text-xs text-gray-400">
                    {uploading ? t('uploading') : t('downloading')}
                    {downloadProgress ? ` ${downloadProgress.name}` : ''}
                  </span>
                  <div className="w-24 h-2 bg-gray-200 rounded-full overflow-hidden">
                    <div className="h-full bg-blue-500 transition-all" style={{ width: `${uploading ? uploadProgress : (downloadProgress?.pct || 0)}%` }} />
                  </div>
                  <span className="text-xs text-gray-400">{uploading ? uploadProgress : (downloadProgress?.pct || 0)}%</span>
                </div>
              ) : (
                <span className="text-xs text-gray-400 animate-pulse">⏳ {t('loading')}...</span>
              )}
            </div>
          )}
        </div>

        {/* File list */}
        <div className="flex-1 overflow-auto min-h-0 pb-4">
          {loading && entries.length === 0 ? (
            <div className="text-center py-12 text-gray-400">⏳ {t('loading')}...</div>
          ) : entries.length === 0 ? (
            <div className="text-center py-12 text-gray-400">{t('empty_dir')}</div>
          ) : (
            <table className="w-full text-sm">
              <thead className="sticky top-0 bg-gray-50 z-10">
                <tr className="border-b text-left text-gray-500">
                  <th className="py-2 pl-3 w-8">
                    <input
                      type="checkbox"
                      className="w-3.5 h-3.5 accent-blue-600 cursor-pointer"
                      aria-label={t('select_all')}
                      checked={selected.size === entries.length && entries.length > 0}
                      onChange={() => {
                             if (selected.size === entries.length) setSelected(new Set());
                             else setSelected(new Set(entries.map(e => e.name)));
                           }}
                    />
                  </th>
                  <th className="py-2 w-8" aria-hidden="true" />
                  <th className="py-2">{t('name')}</th>
                  <th className="py-2 hidden sm:table-cell w-20">{t('size')}</th>
                  <th className="py-2 hidden md:table-cell w-36">{t('modified')}</th>
                  <th className="py-2 pr-3 w-10" aria-hidden="true" />
                </tr>
              </thead>
              <tbody>
                {entries.map((entry) => {
                  const isSelected = selected.has(entry.name);
                  return (
                  <tr
                    key={entry.name}
                    className={`border-b border-gray-100 hover:bg-blue-50/50 ${isSelected ? 'bg-blue-50' : ''}`}
                    role="row"
                  >
                    <td className="py-2 pl-3">
                      <input
                        type="checkbox"
                        className="w-3.5 h-3.5 accent-blue-600 cursor-pointer"
                        aria-label={entry.name}
                        checked={isSelected}
                        onChange={() => {
                               const next = new Set(selected);
                               if (isSelected) next.delete(entry.name);
                               else next.add(entry.name);
                               setSelected(next);
                             }}
                      />
                    </td>
                    <td className="py-2">
                      {entry.type === 'dir' ? '📁' : getFileIcon(entry.name)}
                    </td>
                    <td className="py-2 max-w-[200px] truncate">
                      {entry.type === 'dir' ? (
                        <button
                          className="text-blue-600 hover:underline cursor-pointer font-medium text-left"
                          onClick={() => navigateTo(fullPath(entry.name))}
                        >
                          {entry.name}
                        </button>
                      ) : (isImageFile(entry.name) || isTextFile(entry.name)) ? (
                        <button
                          className="text-gray-800 hover:text-blue-600 hover:underline cursor-pointer text-left"
                          onClick={() => handlePreview(entry)}
                          title={t('preview')}
                        >
                          {entry.name}
                        </button>
                      ) : (
                        <span className="text-gray-800">{entry.name}</span>
                      )}
                    </td>
                    <td className="py-2 hidden sm:table-cell text-gray-500">
                      {entry.type === 'dir' ? '—' : formatSize(entry.size)}
                    </td>
                    <td className="py-2 hidden md:table-cell text-gray-500 text-xs">
                      {formatTime(entry.mtime)}
                    </td>
                    <td className="py-2 pr-3 relative">
                      <button
                        className="w-7 h-7 flex items-center justify-center rounded hover:bg-gray-200 text-gray-500 ml-auto"
                        onClick={() => setMenuOpen(menuOpen === entry.name ? null : entry.name)}
                        title={t('actions')}
                      >⋮
                      </button>
                      {menuOpen === entry.name && (
                        <div
                          className="absolute right-0 top-1/2 -translate-y-1/2 z-30 flex gap-1 items-center bg-white border rounded-lg shadow-lg px-2 py-1.5 whitespace-nowrap"
                          data-action-bar
                          onClick={(e) => e.stopPropagation()}
                        >
                          {entry.type === 'file' && (
                            <>
                              {(isImageFile(entry.name) || isTextFile(entry.name)) && (
                                <Button
                                  size="sm"
                                  variant="outline"
                                  onClick={() => { setMenuOpen(null); handlePreview(entry); }}
                                >
                                  👁 {t('preview')}
                                </Button>
                              )}
                              {isEditableFile(entry.name) && (
                                <Button
                                  size="sm"
                                  variant="outline"
                                  onClick={() => { setMenuOpen(null); handleEdit(entry); }}
                                >
                                  ✏️ {t('edit')}
                                </Button>
                              )}
                              <Button
                                size="sm"
                                variant="outline"
                                onClick={() => { setMenuOpen(null); handleDownload(entry); }}
                              >
                                ⬇ {t('download')}
                              </Button>
                            </>
                          )}
                          <Button
                            size="sm"
                            variant="outline"
                            onClick={() => { setMenuOpen(null); handleRenameClick(entry); }}
                          >
                            📝 {t('rename')}
                          </Button>
                          <Button
                            size="sm"
                            variant="destructive"
                            onClick={() => { setMenuOpen(null); handleDeleteClick(entry); }}
                          >
                            🗑 {t('delete')}
                          </Button>
                          <button
                            className="text-gray-400 hover:text-gray-600 ml-1 px-1 text-lg"
                            onClick={() => setMenuOpen(null)}
                          >✕
                          </button>
                        </div>
                      )}
                    </td>
                  </tr>
                  );
                })}
              </tbody>
            </table>
          )}
        </div>

        {/* ── Preview Modal ── */}
        {previewFile && !editFile && (
          <div className="fixed inset-0 z-[60] flex items-center justify-center bg-black/50 p-4">
            <div className="bg-white rounded-lg shadow-2xl w-full max-w-4xl max-h-[85vh] flex flex-col">
              {/* header */}
              <div className="flex items-center justify-between px-5 py-3 border-b shrink-0">
                <span className="text-sm font-medium">👁 {t('previewing')}: {previewFile}</span>
                <div className="flex gap-2">
                  {isEditableFile(previewFile) && (
                    <Button
                      size="sm"
                      variant="outline"
                      onClick={() => {
                      const entry = entries.find(e => e.name === previewFile);
                      if (entry) handleEdit(entry);
                    }}
                    >✏️ {t('edit')}
                    </Button>
                  )}
                  <button className="text-gray-400 hover:text-gray-600 text-xl px-2" onClick={closePreview}>✕</button>
                </div>
              </div>
              {/* body */}
              <div className="flex-1 overflow-auto min-h-0">
                {previewLoading ? (
                  <div className="text-center py-12 text-gray-400">{t('loading')}...</div>
                ) : previewImageUrl ? (
                  <div className="flex items-center justify-center p-4 h-full">
                    <img
                      src={previewImageUrl}
                      alt={previewFile || ''}
                      className="max-w-full max-h-full object-contain rounded"
                    />
                  </div>
                ) : (
                  <pre className="p-4 text-sm font-mono whitespace-pre-wrap break-all min-h-[200px]">
                    {previewContent || t('no_preview')}
                  </pre>
                )}
              </div>
            </div>
          </div>
        )}

        {/* ── Edit Modal ── */}
        {editFile && (
          <div className="fixed inset-0 z-[60] flex items-center justify-center bg-black/50 p-4">
            <div className="bg-white rounded-lg shadow-2xl w-full max-w-4xl max-h-[85vh] flex flex-col">
              {/* header */}
              <div className="flex items-center justify-between px-5 py-3 border-b shrink-0">
                <span className="text-sm font-medium">✏️ {t('editing')}: {editFile}</span>
                <button
                  className="text-gray-400 hover:text-gray-600 text-xl px-2"
                  onClick={() => setEditFile(null)}
                >✕
                </button>
              </div>
              {/* body */}
              <div className="flex-1 overflow-hidden p-4">
                <textarea
                  className="w-full h-full min-h-[300px] p-3 border rounded font-mono text-sm resize-none focus:outline-none focus:ring-2 focus:ring-blue-400"
                  value={editContent}
                  onInput={(e) => setEditContent((e.target as HTMLTextAreaElement).value)}
                />
              </div>
              {/* footer */}
              <div className="flex gap-2 justify-end px-5 py-3 border-t shrink-0 bg-gray-50">
                <Button variant="outline" onClick={() => setEditFile(null)}>
                  {t('cancel')}
                </Button>
                <Button
                  className="bg-green-600 text-white hover:bg-green-700"
                  onClick={handleSaveEdit}
                  disabled={editSaving}
                >
                  {editSaving ? '⏳' : ''} {t('save')}
                </Button>
              </div>
            </div>
          </div>
        )}

        {/* ── Create Dialog ── */}
        {createDialog && (
          <div className="fixed inset-0 z-[70] flex items-center justify-center bg-black/40">
            <div className="bg-white rounded-lg shadow-xl p-6 w-full max-w-sm mx-4 space-y-4">
              <h3 className="text-lg font-bold">
                {createDialog.type === 'dir' ? '📁' : '📄'} {t(createDialog.type === 'dir' ? 'new_folder' : 'new_file')}
              </h3>
              <div>
                <Label className="text-xs text-gray-500 mb-1.5">
                  {t(createDialog.type === 'dir' ? 'folder_name' : 'file_name')}
                </Label>
                <Input
                  className="mt-1.5"
                  placeholder={createDialog.type === 'dir' ? 'new_folder' : 'new_file.txt'}
                  value={createName}
                  onInput={(e) => setCreateName((e.target as HTMLInputElement).value)}
                  onKeyDown={(e) => { if (e.key === 'Enter') handleCreate(); }}
                />
              </div>
              <div className="flex gap-2 justify-end pt-1">
                <Button variant="outline" onClick={() => setCreateDialog(null)}>
                  {t('cancel')}
                </Button>
                <Button
                  className="bg-green-600 text-white hover:bg-green-700"
                  disabled={!createName.trim() || creating}
                  onClick={handleCreate}
                >
                  {t('create')}
                </Button>
              </div>
            </div>
          </div>
        )}

        {/* Delete confirm dialog */}
        {deleteTarget && (
          <div className="fixed inset-0 z-[60] flex items-center justify-center bg-black/40">
            <div className="bg-white rounded-lg shadow-xl p-6 w-full max-w-sm mx-4 space-y-4">
              <h3 className="text-lg font-bold">⚠ {t('delete_confirm_title')}</h3>

              <p className="text-sm text-gray-600 leading-relaxed">
                {t('delete_confirm_msg')}{' '}
                <strong className="text-red-600 break-all">{deleteTarget.name}</strong>
                {deleteTarget.isDir
                  ? t('delete_confirm_dir_suffix')
                  : t('delete_confirm_suffix')}
              </p>

              {deleteTarget.isDir && (
                <p className="text-xs text-amber-600 bg-amber-50 rounded px-2 py-1.5">
                  ⚠ {t('delete_dir_warning')}
                </p>
              )}

              <div>
                <Label className="text-xs text-gray-500 mb-1.5">
                  {t('delete_confirm_type')} <code className="bg-gray-200 px-1 rounded text-xs">{deleteTarget.name}</code>
                </Label>
                <Input
                  className="mt-1.5"
                  placeholder={deleteTarget.name}
                  value={deleteInput}
                  onInput={(e) => setDeleteInput((e.target as HTMLInputElement).value)}
                />
              </div>

              <div className="flex gap-2 justify-end pt-1">
                <Button variant="outline" onClick={() => setDeleteTarget(null)}>
                  {t('cancel')}
                </Button>
                <Button
                  variant="destructive"
                  disabled={deleteInput !== deleteTarget.name}
                  onClick={handleDeleteConfirm}
                >🗑 {t('delete_confirm_btn')}
                </Button>
              </div>
            </div>
          </div>
        )}

        {/* Rename dialog */}
        {renameTarget && (
          <div className="fixed inset-0 z-[60] flex items-center justify-center bg-black/40">
            <div className="bg-white rounded-lg shadow-xl p-6 w-full max-w-sm mx-4 space-y-4">
              <h3 className="text-lg font-bold">✏️ {t('rename')}</h3>
              <p className="text-sm text-gray-600">
                {t('rename_msg')} <strong className="break-all">{renameTarget.name}</strong>
              </p>
              <Input
                className="mt-1"
                value={renameInput}
                onInput={(e) => setRenameInput((e.target as HTMLInputElement).value)}
                onKeyDown={(e) => { if (e.key === 'Enter') handleRenameConfirm(); }}
              />
              <div className="flex gap-2 justify-end pt-1">
                <Button variant="outline" onClick={() => setRenameTarget(null)}>{t('cancel')}</Button>
                <Button
                  className="bg-green-600 text-white hover:bg-green-700"
                  onClick={handleRenameConfirm}
                  disabled={!renameInput.trim() || renameInput.trim() === renameTarget.name}
                >
                  {t('save')}
                </Button>
              </div>
            </div>
          </div>
        )}

        {/* Batch delete confirm dialog */}
        {batchDeleteConfirm && (
          <div className="fixed inset-0 z-[70] flex items-center justify-center bg-black/40">
            <div className="bg-white rounded-lg shadow-xl p-6 w-full max-w-sm mx-4 space-y-4">
              <h3 className="text-lg font-bold">⚠ {t('delete_confirm_title')}</h3>
              <p className="text-sm text-gray-600 leading-relaxed">
                {t('batch_delete_confirm_msg')}
              </p>
              {entries.some(e => selected.has(e.name) && e.type === 'dir') && (
                <p className="text-xs text-amber-600 bg-amber-50 rounded px-2 py-1.5">
                  ⚠ {t('delete_dir_warning')}
                </p>
              )}
              <ul className="text-xs text-gray-500 max-h-24 overflow-y-auto border rounded p-2">
                {entries.filter(e => selected.has(e.name)).map(e => (
                  <li key={e.name} className="truncate">{e.type === 'dir' ? '📁' : getFileIcon(e.name)} {e.name}</li>
                ))}
              </ul>
              <div className="flex gap-2 justify-end pt-1">
                <Button variant="outline" onClick={() => setBatchDeleteConfirm(null)}>
                  {t('cancel')}
                </Button>
                <Button
                  variant="destructive"
                  disabled={batchDeleting}
                  onClick={handleBatchDelete}
                >
                  {batchDeleting ? '⏳' : '🗑'} {t('delete_confirm_btn')}
                </Button>
              </div>
            </div>
          </div>
        )}

        {/* ── Format Password Confirmation Dialog ── */}
        {formatDialogOpen && (
          <div className="fixed inset-0 z-[80] flex items-center justify-center bg-black/40">
            <div className="bg-white rounded-lg shadow-xl p-6 w-full max-w-sm mx-4 space-y-4">
              <h3 className="text-lg font-bold">⚠ {t('format_confirm_title')}</h3>
              <p className="text-sm text-gray-600 leading-relaxed">
                {t('format_confirm_msg')}
              </p>
              <div>
                <Label className="text-xs text-gray-500 mb-1.5">
                  {t('format_password_placeholder')}
                </Label>
                <div className="relative mt-1.5">
                  <Input
                    type={formatPasswordVisible ? 'text' : 'password'}
                    placeholder={t('format_password_placeholder')}
                    value={formatPassword}
                    onInput={(e) => { setFormatPassword((e.target as HTMLInputElement).value); setFormatPasswordError(''); }}
                    onKeyDown={(e) => { if (e.key === 'Enter') handleFormatVerify(); }}
                    disabled={formatVerifying}
                    className={formatPasswordError ? 'border-red-500' : ''}
                  />
                  <button
                    type="button"
                    className="absolute right-2 top-1/2 -translate-y-1/2 text-gray-400 hover:text-gray-600 text-sm"
                    onClick={() => setFormatPasswordVisible(!formatPasswordVisible)}
                  >{formatPasswordVisible ? '🙈' : '👁'}
                  </button>
                </div>
                {formatPasswordError && (
                  <p className="text-sm text-red-500 mt-1">{formatPasswordError}</p>
                )}
              </div>
              <div className="flex gap-2 justify-end pt-1">
                <Button variant="outline" onClick={handleFormatDialogClose} disabled={formatVerifying}>
                  {t('cancel')}
                </Button>
                <Button
                  variant="destructive"
                  disabled={!formatPassword.trim() || formatVerifying}
                  onClick={handleFormatVerify}
                >
                  {formatVerifying ? '⏳' : '⚡'} {t('format_confirm_btn')}
                </Button>
              </div>
            </div>
          </div>
        )}

        {/* ── Full-screen Formatting Wait Overlay ── */}
        {formatting && (
          <div className="fixed inset-0 z-[90] flex items-center justify-center bg-black/60">
            <div className="bg-white rounded-lg shadow-2xl p-8 mx-4 text-center space-y-4 max-w-sm w-full">
              <div className="flex justify-center">
                <div className="w-12 h-12 border-4 border-blue-500 border-t-transparent rounded-full animate-spin" />
              </div>
              <h3 className="text-lg font-bold text-gray-800">⏳ {t('formatting')}</h3>
              <p className="text-sm text-gray-500 leading-relaxed">
                {t('formatting_msg')}
              </p>
            </div>
          </div>
        )}
      </div>
    </div>
  );
}

/* ── Storage Management Page ── */

export default function StorageManagement() {
  const { i18n } = useLingui();
  const [isLoading, setIsLoading] = useState(true);
  const [storageInfo, setStorageInfo] = useState<StorageInfo | null>(null);
  const colorList = ['#22c55e', '#FE9A00', '#ef4444'];
  const [sdProgress, setSdProgress] = useState<StorageProgress>({ used: 0, total: 100, color: colorList[0] });
  const [flashProgress, setFlashProgress] = useState<StorageProgress>({ used: 0, total: 100, color: colorList[0] });

  // file browser modal
  const [browserFs, setBrowserFs] = useState<FsType | null>(null);
  // format-flash dialog
  const [formatOpen, setFormatOpen] = useState(false);

  const initData = async () => {
    try {
      setIsLoading(true);
      const res = await storageManagement.getStorage();
      const data = (res && 'data' in res) ? (res as any).data as StorageInfo : (res as any as StorageInfo);
      setStorageInfo(data);
      const sdColor = data.usage_percent > 95 ? colorList[2] : data.usage_percent > 80 ? colorList[1] : colorList[0];
      setSdProgress({ used: data.usage_percent || 0, total: 100, color: sdColor });
      const flashColor = data.flash_usage_percent > 95 ? colorList[2] : data.flash_usage_percent > 80 ? colorList[1] : colorList[0];
      setFlashProgress({ used: data.flash_usage_percent || 0, total: 100, color: flashColor });
    } catch (error) { console.error(error); } finally { setIsLoading(false); }
  };

  useEffect(() => { initData(); }, []);

  // Refresh storage info — called after file operations that change capacity
  const refreshStorage = useCallback(() => { initData(); }, []);

  const renderStorageCard = (
    title: string,
    statusLabel: string,
    statusColor: string,
    usedMB: number,
    totalMB: number,
    availableMB: number,
    progress: StorageProgress,
    fsTypeLabel: string,
    onBrowse: () => void,
  ) => (
    <div className="border bg-gray-100 rounded-md p-3 space-y-2">
      <div className="flex items-center justify-between gap-4">
        <Label className="text-sm">{title}</Label>
        <div className="flex items-center text-sm" style={{ color: statusColor }}>
          <div className="rounded-full w-2 h-2 mr-2" style={{ backgroundColor: statusColor }} />
          <span>{statusLabel}</span>
          <span className="text-xs text-gray-400 ml-1">({fsTypeLabel})</span>
        </div>
      </div>
      <Separator />
      <div className="flex items-center justify-between gap-4">
        <Label className="text-sm">{i18n._('sys.storage_management.capacity_info')}</Label>
        <p className="text-sm">
          <span>{i18n._('sys.storage_management.used')}</span> {formatCapacity(usedMB)}
          {' / '}{formatCapacity(totalMB)}
        </p>
      </div>
      <div className="mb-1">
        <Progress className="h-2 bg-gray-200" value={progress.used} indicatorColor={progress.color} />
      </div>
      <div className="flex items-center justify-between mt-3">
        <p className="text-xs text-gray-400">{i18n._('sys.storage_management.free')}: {formatCapacity(availableMB)}</p>
        <Button size="sm" variant="outline" onClick={onBrowse}>
          📂 {i18n._('sys.file_browsing.title')}
        </Button>
      </div>
    </div>
  );

  return (
    <div className="flex justify-center pt-4">
      <Card className="sm:w-xl w-full mx-4">
        <CardTitle className="pl-6">{i18n._('sys.storage_management.storage_management_title')}</CardTitle>
        <CardContent>
          {isLoading ? <StorageManagementSkeleton /> : (
            <div className="space-y-3">
              {/* Internal Flash — error state takes precedence over capacity view */}
              {storageInfo?.flash_fs_error ? (
                <div className="border bg-red-50 rounded-md p-3 space-y-2">
                  <div className="flex items-center justify-between gap-4">
                    <Label className="text-sm">{i18n._('sys.storage_management.internal_flash')}</Label>
                    <div className="flex items-center text-sm text-red-600">
                      <div className="rounded-full w-2 h-2 mr-2 bg-red-600" />
                      <span>{i18n._('sys.storage_management.fs_error')}</span>
                    </div>
                  </div>
                  <Separator />
                  <p className="text-xs text-red-600">
                    {i18n._('sys.storage_management.fs_error_msg')}
                  </p>
                  {/* When the FS is mounted (corrupt-but-readable: capacity/write
                      failed but reads still work), keep the browse/download
                      entry so the user can recover existing files before
                      formatting. Only when truly not mounted is browsing
                      unavailable. */}
                  <div className="flex justify-end gap-2">
                    {storageInfo?.flash_fs_mounted && (
                      <Button size="sm" variant="outline" onClick={() => setBrowserFs('flash')}>
                        📂 {i18n._('sys.file_browsing.title')}
                      </Button>
                    )}
                    <Button size="sm" variant="destructive" onClick={() => setFormatOpen(true)}>
                      🗑 {i18n._('sys.storage_management.format')}
                    </Button>
                  </div>
                </div>
              ) : storageInfo?.flash_fs_mounted && renderStorageCard(
                i18n._('sys.storage_management.internal_flash'),
                storageInfo.flash_usage_percent > 95
                  ? i18n._('sys.storage_management.full')
                  : storageInfo.flash_usage_percent > 80
                    ? i18n._('sys.storage_management.warning')
                    : i18n._('sys.storage_management.normal'),
                flashProgress.color,
                storageInfo.flash_used_capacity_mb,
                storageInfo.flash_total_capacity_mb,
                storageInfo.flash_available_capacity_mb,
                flashProgress,
                storageInfo.flash_fs_type,
                () => setBrowserFs('flash'),
              )}

              {/* SD Card */}
              {storageInfo?.sd_card_connected && renderStorageCard(
                i18n._('sys.storage_management.sd_card'),
                storageInfo.usage_percent > 95
                  ? i18n._('sys.storage_management.full')
                  : storageInfo.usage_percent > 80
                    ? i18n._('sys.storage_management.warning')
                    : i18n._('sys.storage_management.normal'),
                sdProgress.color,
                storageInfo.used_capacity_mb,
                storageInfo.total_capacity_mb,
                storageInfo.available_capacity_mb,
                sdProgress,
                'FAT/exFAT',
                () => setBrowserFs('sd'),
              )}

              {/* SD not connected */}
              {!storageInfo?.sd_card_connected && (
                <div className="border bg-gray-100 rounded-md p-3">
                  <div className="flex items-center justify-between gap-4">
                    <Label className="text-sm">{i18n._('sys.storage_management.sd_card_status')}</Label>
                    <div className="flex items-center text-sm" style={{ color: '#ef4444' }}>
                      <div className="rounded-full w-2 h-2 mr-2" style={{ backgroundColor: '#ef4444' }} />
                      <span>{i18n._('sys.storage_management.not_detected')}</span>
                    </div>
                  </div>
                </div>
              )}

              {/* Nothing at all */}
              {!storageInfo?.flash_fs_mounted && !storageInfo?.sd_card_connected && (
                <div className="text-center text-gray-400 py-4 text-sm">
                  {i18n._('sys.storage_management.no_storage')}
                </div>
              )}
            </div>
          )}
        </CardContent>
      </Card>

      {/* File Browser Modal */}
      {browserFs && (
        <FileBrowserModal
          fsType={browserFs}
          onClose={() => setBrowserFs(null)}
          availableMB={browserFs === 'flash'
            ? (storageInfo?.flash_available_capacity_mb ?? 0)
            : (storageInfo?.available_capacity_mb ?? 0)}
          onStorageChange={refreshStorage}
        />
      )}

      {/* Format Flash Dialog (two-step confirmation) */}
      {formatOpen && (
        <FormatFlashDialog
          onClose={() => setFormatOpen(false)}
          onConfirmed={() => { setFormatOpen(false); refreshStorage(); }}
        />
      )}
    </div>
  );
}
