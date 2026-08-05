import { useState, useEffect, useCallback } from 'preact/hooks';
import { useLingui } from '@lingui/react';
import { Card, CardTitle, CardContent } from '@/components/ui/card';
import { Button } from '@/components/ui/button';
import { Label } from '@/components/ui/label';
import { Input } from '@/components/ui/input';
import { Separator } from '@/components/ui/separator';
import { toast } from 'sonner';
import fileManagement, { type FileEntry, type FsType } from '@/services/api/fileManagement';
import storageManagement from '@/services/api/storageManagement';
import FormatFlashDialog from '@/components/format-flash-dialog';

/* ── helpers ── */
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
  return `${d.toLocaleDateString()} ${d.toLocaleTimeString([], { hour: '2-digit', minute: '2-digit' })}`;
}

function isTextFile(name: string): boolean {
  const ext = name.split('.').pop()?.toLowerCase();
  return ['txt', 'json', 'csv', 'log', 'xml', 'yml', 'yaml', 'cfg', 'ini', 'html', 'htm', 'css', 'js', 'md', 'sh', 'py', 'c', 'h', 'cpp', 'hpp'].includes(ext || '');
}

function isImageFile(name: string): boolean {
  const ext = name.split('.').pop()?.toLowerCase();
  return ['jpg', 'jpeg', 'png', 'bmp', 'gif', 'svg', 'ico'].includes(ext || '');
}

function isEditableFile(name: string): boolean {
  return isTextFile(name);
}

/* ── breadcrumb builder ── */
function buildBreadcrumbs(path: string): { label: string; fullPath: string }[] {
  const parts = path.split('/').filter(Boolean);
  const crumbs = [{ label: 'Home', fullPath: '/' }];
  let acc = '';
  for (const p of parts) {
    acc += `/${p}`;
    crumbs.push({ label: p, fullPath: acc });
  }
  return crumbs;
}

/* ── component ── */
export default function BrowseFiles() {
  const { i18n } = useLingui();

  /* state */
  const [fsType, setFsType] = useState<FsType>('flash');
  const [currentPath, setCurrentPath] = useState('/');
  const [entries, setEntries] = useState<FileEntry[]>([]);
  const [loading, setLoading] = useState(false);
  const [sdAvailable, setSdAvailable] = useState(false);
  const [flashMounted, setFlashMounted] = useState(false);
  const [flashFsError, setFlashFsError] = useState(false);
  const [formatOpen, setFormatOpen] = useState(false);

  /* preview / edit */
  const [previewFile, setPreviewFile] = useState<string | null>(null);
  const [previewContent, setPreviewContent] = useState<string>('');
  const [previewImageUrl, setPreviewImageUrl] = useState<string>('');
  const [previewLoading, setPreviewLoading] = useState(false);

  /* edit mode */
  const [editFile, setEditFile] = useState<string | null>(null);
  const [editContent, setEditContent] = useState('');
  const [editSaving, setEditSaving] = useState(false);

  /* delete dialog */
  const [deleteTarget, setDeleteTarget] = useState<{ name: string; path: string } | null>(null);
  const [deleteConfirmName, setDeleteConfirmName] = useState('');

  /* upload */
  const [uploading, setUploading] = useState(false);

  /* load storage availability */
  const checkAvailability = useCallback(async () => {
    try {
      const res = await storageManagement.getStorage();
      const d = (res && 'data' in res) ? (res as any).data : res;
      setSdAvailable(!!d?.sd_card_connected);
      setFlashMounted(!!d?.flash_fs_mounted);
      setFlashFsError(!!d?.flash_fs_error);
      // auto-select available FS
      if (!d?.flash_fs_mounted && d?.sd_card_connected) setFsType('sd');
      else if (d?.flash_fs_mounted && !d?.sd_card_connected) setFsType('flash');
    } catch { /* ignore */ }
  }, []);

  useEffect(() => {
    checkAvailability();
  }, [checkAvailability]);

  /* load directory */
  const loadDir = useCallback(async (fs: FsType, path: string) => {
    setLoading(true);
    try {
      const res = await fileManagement.listDir(fs, path);
      const d = (res && 'data' in res) ? (res as any).data : res;
      // sort: dirs first, then alphabetical
      const sorted = [...(d?.entries || [])].sort((a: FileEntry, b: FileEntry) => {
        if (a.type !== b.type) return a.type === 'dir' ? -1 : 1;
        return a.name.localeCompare(b.name);
      });
      setEntries(sorted);
    } catch {
      toast.error(i18n._('sys.file_browsing.load_failed'));
      setEntries([]);
    } finally {
      setLoading(false);
    }
  }, [i18n]);

  useEffect(() => {
    loadDir(fsType, currentPath);
  }, [fsType, currentPath, loadDir]);

  /* navigation */
  const navigateTo = (path: string) => {
    setPreviewFile(null);
    setEditFile(null);
    setCurrentPath(path);
  };

  const handleFsSwitch = (fs: FsType) => {
    if (fs === fsType) return;
    if (fs === 'sd' && !sdAvailable) return;
    if (fs === 'flash' && !flashMounted) return;
    setFsType(fs);
    setCurrentPath('/');
    setPreviewFile(null);
    setEditFile(null);
  };

  /* preview */
  const handlePreview = async (entry: FileEntry) => {
    const fullPath = currentPath === '/' ? `/${entry.name}` : `${currentPath}/${entry.name}`;
    setPreviewFile(entry.name);
    setEditFile(null);
    setPreviewLoading(true);

    if (isImageFile(entry.name)) {
      try {
        setPreviewContent('');
        const res = await fileManagement.preview(fsType, fullPath, true);
        const blob = res instanceof Blob ? res : (res as any)?.data;
        if (blob) {
          setPreviewImageUrl(URL.createObjectURL(blob));
        }
      } catch {
        toast.error(i18n._('sys.file_browsing.preview_failed'));
      }
    } else {
      try {
        setPreviewImageUrl('');
        const res = await fileManagement.preview(fsType, fullPath, false);
        const d = (res && 'data' in res) ? (res as any).data : res;
        if (d?.too_large) {
          setPreviewContent(i18n._('sys.file_browsing.file_too_large'));
        } else {
          setPreviewContent(d?.content || '');
        }
      } catch {
        toast.error(i18n._('sys.file_browsing.preview_failed'));
      }
    }
    setPreviewLoading(false);
  };

  const closePreview = () => {
    if (previewImageUrl) URL.revokeObjectURL(previewImageUrl);
    setPreviewFile(null);
    setPreviewContent('');
    setPreviewImageUrl('');
  };

  /* edit */
  const handleEdit = async (entry: FileEntry) => {
    const fullPath = currentPath === '/' ? `/${entry.name}` : `${currentPath}/${entry.name}`;
    setLoading(true);
    try {
      const res = await fileManagement.preview(fsType, fullPath, false);
      const d = (res && 'data' in res) ? (res as any).data : res;
      if (d?.too_large) {
        toast.error(i18n._('sys.file_browsing.file_too_large'));
        return;
      }
      setEditFile(entry.name);
      setEditContent(d?.content || '');
      setPreviewFile(null);
    } catch {
      toast.error(i18n._('sys.file_browsing.edit_load_failed'));
    } finally {
      setLoading(false);
    }
  };

  const handleSaveEdit = async () => {
    if (!editFile) return;
    const fullPath = currentPath === '/' ? `/${editFile}` : `${currentPath}/${editFile}`;
    setEditSaving(true);
    try {
      await fileManagement.edit(fsType, fullPath, editContent);
      toast.success(i18n._('sys.file_browsing.edit_saved'));
      setEditFile(null);
      loadDir(fsType, currentPath);
    } catch {
      toast.error(i18n._('sys.file_browsing.edit_save_failed'));
    } finally {
      setEditSaving(false);
    }
  };

  /* download */
  const handleDownload = async (entry: FileEntry) => {
    const fullPath = currentPath === '/' ? `/${entry.name}` : `${currentPath}/${entry.name}`;
    try {
      const res = await fileManagement.download(fsType, fullPath);
      const blob = res instanceof Blob ? res : (res as any)?.data;
      if (!blob) throw new Error('no data');
      const url = URL.createObjectURL(blob);
      const a = document.createElement('a');
      a.href = url;
      a.download = entry.name;
      a.click();
      URL.revokeObjectURL(url);
    } catch {
      toast.error(i18n._('sys.file_browsing.download_failed'));
    }
  };

  /* delete */
  const handleDeleteClick = (entry: FileEntry) => {
    const fullPath = currentPath === '/' ? `/${entry.name}` : `${currentPath}/${entry.name}`;
    setDeleteTarget({ name: entry.name, path: fullPath });
    setDeleteConfirmName('');
  };

  const handleDeleteConfirm = async () => {
    if (!deleteTarget || deleteConfirmName !== deleteTarget.name) return;
    try {
      await fileManagement.deleteFile(fsType, deleteTarget.path);
      toast.success(i18n._('sys.file_browsing.delete_success'));
      setDeleteTarget(null);
      loadDir(fsType, currentPath);
    } catch {
      toast.error(i18n._('sys.file_browsing.delete_failed'));
    }
  };

  /* upload */
  const handleUpload = async (e: Event) => {
    const input = e.target as HTMLInputElement;
    const files = input?.files;
    if (!files?.length) return;
    setUploading(true);
    let success = 0;
    for (const file of Array.from(files)) {
      try {
        // eslint-disable-next-line no-await-in-loop
        await fileManagement.upload(fsType, currentPath, file);
        success++;
      } catch {
        toast.error(`${i18n._('sys.file_browsing.upload_failed')}: ${file.name}`);
      }
    }
    if (success > 0) toast.success(i18n._('sys.file_browsing.upload_success'));
    setUploading(false);
    input.value = '';
    loadDir(fsType, currentPath);
  };

  /* ── render ── */
  const breadcrumbs = buildBreadcrumbs(currentPath);

  const actionLabel = (key: string) => i18n._(`sys.file_browsing.${key}`);

  return (
    <div className="flex justify-center pt-4">
      <Card className="w-full max-w-4xl mx-4">
        <CardTitle className="pl-6">{i18n._('sys.file_browsing.title')}</CardTitle>
        <CardContent>
          {/* ── Top bar: FS selector + breadcrumbs ── */}
          <div className="flex items-center gap-3 mb-3 flex-wrap">
            {/* FS selector */}
            <div className="flex gap-1">
              <Button
                size="sm"
                variant={fsType === 'flash' ? 'default' : 'outline'}
                disabled={!flashMounted}
                onClick={() => handleFsSwitch('flash')}
              >
                💾 Flash {flashMounted ? '' : '(—)'}
              </Button>
              <Button
                size="sm"
                variant={fsType === 'sd' ? 'default' : 'outline'}
                disabled={!sdAvailable}
                onClick={() => handleFsSwitch('sd')}
              >
                💳 SD {sdAvailable ? '' : '(—)'}
              </Button>
            </div>

            <Separator orientation="vertical" className="h-6" />

            {/* Breadcrumbs */}
            <div className="flex items-center gap-1 text-sm flex-wrap">
              {breadcrumbs.map((crumb, i) => (
                <span key={crumb.fullPath} className="flex items-center gap-1">
                  {i > 0 && <span className="text-gray-300">/</span>}
                  {i < breadcrumbs.length - 1 ? (
                    <button
                      className="text-primary hover:underline cursor-pointer"
                      onClick={() => navigateTo(crumb.fullPath)}
                    >
                      {crumb.label}
                    </button>
                  ) : (
                    <span className="font-medium text-gray-700">{crumb.label}</span>
                  )}
                </span>
              ))}
            </div>

            <div className="flex-1" />

            {/* Upload button */}
            <label className="cursor-pointer">
              <input
                type="file"
                multiple
                className="hidden"
                onChange={handleUpload}
                disabled={uploading}
              />
              <Button size="sm" variant="outline" disabled={uploading}>
                {uploading ? '⏳' : '📤'} {actionLabel('upload')}
              </Button>
            </label>
          </div>

          <Separator className="mb-2" />

          {/* ── Flash FS error banner ── */}
          {flashFsError && (
            <div className="mb-2 border bg-red-50 rounded-md p-3 flex items-center justify-between gap-3">
              <div className="text-xs text-red-600">
                {i18n._('sys.storage_management.fs_error_msg')}
              </div>
              <Button
                size="sm"
                variant="destructive"
                onClick={() => setFormatOpen(true)}
              >
                🗑 {i18n._('sys.storage_management.format')}
              </Button>
            </div>
          )}

          {/* ── File list table ── */}
          {loading ? (
            <div className="text-center py-8 text-gray-400">{actionLabel('loading')}...</div>
          ) : entries.length === 0 ? (
            <div className="text-center py-8 text-gray-400">{actionLabel('empty_dir')}</div>
          ) : (
            <div className="overflow-x-auto">
              <table className="w-full text-sm">
                <thead>
                  <tr className="border-b text-left text-gray-500">
                    <th className="py-2 pr-2 w-8" aria-hidden="true" />
                    <th className="py-2">{actionLabel('name')}</th>
                    <th className="py-2 hidden sm:table-cell w-24">{actionLabel('size')}</th>
                    <th className="py-2 hidden md:table-cell w-40">{actionLabel('modified')}</th>
                    <th className="py-2 w-32 text-right">{actionLabel('actions')}</th>
                  </tr>
                </thead>
                <tbody>
                  {entries.map((entry) => (
                    <tr key={entry.name} className="border-b border-gray-100 hover:bg-gray-50">
                      <td className="py-2 pr-2">
                        {entry.type === 'dir' ? '📁' : isImageFile(entry.name) ? '🖼' : '📄'}
                      </td>
                      <td className="py-2">
                        {entry.type === 'dir' ? (
                          <button
                            className="text-primary hover:underline cursor-pointer font-medium"
                            onClick={() => navigateTo(
                              currentPath === '/' ? `/${entry.name}` : `${currentPath}/${entry.name}`
                            )}
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
                      <td className="py-2 hidden md:table-cell text-gray-500">
                        {formatTime(entry.mtime)}
                      </td>
                      <td className="py-2 text-right">
                        <div className="flex gap-1 justify-end">
                          {entry.type === 'file' && (
                            <>
                              <Button
                                size="sm"
                                variant="ghost"
                                title={actionLabel('preview')}
                                onClick={() => handlePreview(entry)}
                              >
                                👁
                              </Button>
                              {isEditableFile(entry.name) && (
                                <Button
                                  size="sm"
                                  variant="ghost"
                                  title={actionLabel('edit')}
                                  onClick={() => handleEdit(entry)}
                                >
                                  ✏️
                                </Button>
                              )}
                              <Button
                                size="sm"
                                variant="ghost"
                                title={actionLabel('download')}
                                onClick={() => handleDownload(entry)}
                              >
                                ⬇
                              </Button>
                            </>
                          )}
                          <Button
                            size="sm"
                            variant="ghost"
                            title={actionLabel('delete')}
                            onClick={() => handleDeleteClick(entry)}
                            className="text-red-500 hover:text-red-700"
                          >
                            🗑
                          </Button>
                        </div>
                      </td>
                    </tr>
                  ))}
                </tbody>
              </table>
            </div>
          )}

          {/* ── Preview Panel ── */}
          {(previewFile || editFile) && (
            <>
              <Separator className="my-3" />
              <div className="border rounded-md bg-gray-50 p-3">
                <div className="flex items-center justify-between mb-2">
                  <Label className="text-sm font-medium">
                    {editFile ? `✏️ ${actionLabel('editing')}: ${editFile}` : `👁 ${actionLabel('previewing')}: ${previewFile}`}
                  </Label>
                  <div className="flex gap-2">
                    {editFile && (
                      <>
                        <Button size="sm" onClick={handleSaveEdit} disabled={editSaving}>
                          {editSaving ? '...' : actionLabel('save')}
                        </Button>
                        <Button size="sm" variant="outline" onClick={() => setEditFile(null)}>
                          {actionLabel('cancel')}
                        </Button>
                      </>
                    )}
                    {previewFile && !editFile && (
                      <>
                        {isEditableFile(previewFile) && (
                          <Button
                            size="sm"
                            variant="outline"
                            onClick={() => {
                            const entry = entries.find(e => e.name === previewFile);
                            if (entry) handleEdit(entry);
                          }}
                          >
                            ✏️ {actionLabel('edit')}
                          </Button>
                        )}
                        <Button size="sm" variant="outline" onClick={closePreview}>
                          {actionLabel('close')}
                        </Button>
                      </>
                    )}
                  </div>
                </div>

                {previewLoading ? (
                  <div className="text-center py-4 text-gray-400">{actionLabel('loading')}...</div>
                ) : previewImageUrl ? (
                  <div className="flex justify-center">
                    <img
                      src={previewImageUrl}
                      alt={previewFile || ''}
                      className="max-w-full max-h-80 object-contain rounded"
                    />
                  </div>
                ) : editFile !== null ? (
                  <textarea
                    className="w-full h-64 p-2 border rounded font-mono text-sm bg-white"
                    value={editContent}
                    onInput={(e) => setEditContent((e.target as HTMLTextAreaElement).value)}
                  />
                ) : (
                  <pre className="max-h-64 overflow-auto p-2 bg-white rounded text-sm font-mono whitespace-pre-wrap break-all">
                    {previewContent || actionLabel('no_preview')}
                  </pre>
                )}
              </div>
            </>
          )}

          {/* ── Delete Confirmation Dialog ── */}
          {deleteTarget && (
            <div className="fixed inset-0 z-50 flex items-center justify-center bg-black/40">
              <div className="bg-white rounded-lg shadow-xl p-6 w-full max-w-sm mx-4">
                <h3 className="text-lg font-bold mb-2">⚠ {actionLabel('delete_confirm_title')}</h3>
                <p className="text-sm text-gray-600 mb-4">
                  {actionLabel('delete_confirm_msg')}{' '}
                  <strong className="text-red-600">{deleteTarget.name}</strong>
                  {actionLabel('delete_confirm_suffix')}
                </p>
                <Label className="text-xs text-gray-500 mb-1 block">
                  {actionLabel('delete_confirm_type')} <code>{deleteTarget.name}</code>
                </Label>
                <Input
                  className="mb-4"
                  placeholder={deleteTarget.name}
                  value={deleteConfirmName}
                  onInput={(e) => setDeleteConfirmName((e.target as HTMLInputElement).value)}
                />
                <div className="flex gap-2 justify-end">
                  <Button variant="outline" onClick={() => setDeleteTarget(null)}>
                    {actionLabel('cancel')}
                  </Button>
                  <Button
                    variant="destructive"
                    disabled={deleteConfirmName !== deleteTarget.name}
                    onClick={handleDeleteConfirm}
                  >
                    🗑 {actionLabel('delete_confirm_btn')}
                  </Button>
                </div>
              </div>
            </div>
          )}
        </CardContent>
      </Card>

      {/* Format Flash Dialog (two-step confirmation) */}
      {formatOpen && (
        <FormatFlashDialog
          onClose={() => setFormatOpen(false)}
          onConfirmed={() => { setFormatOpen(false); checkAvailability(); }}
        />
      )}
    </div>
  );
}
