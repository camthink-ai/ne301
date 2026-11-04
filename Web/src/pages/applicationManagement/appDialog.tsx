import { useEffect, useState } from 'preact/hooks'
import { useLingui } from '@lingui/react'
import { Button } from '@/components/ui/button'
import {
  Dialog,
  DialogContent,
  DialogDescription,
  DialogFooter,
  DialogHeader,
  DialogTitle,
} from '@/components/dialog'
import { useAppManagementStore, type AppLastError, type AppRecord } from '@/store/appManagement'
import { businessUiFor, runOnceEligible, sidecarProjectionServable } from './appSlot'
import { appEnvelopeKey, appFileErrorKey, appReasonKey } from './messages'

export type AppDialogState =
  | 'deploy'
  | 'manage'
  | 'update'
  | 'uninstall'
  | 'uninstall_success'
  | 'uninstall_abnormal'
  | 'status'

interface OpTargetSnapshot {
  base: 'installed' | 'unavailable'
  publisherId: string
  appId: string
  version: string
  tbsSha256: string
  packageSha256: string
}

function snapshotMatches(snapshot: OpTargetSnapshot, record: AppRecord): boolean {
  return (
    snapshot.publisherId === record.publisherId
    && snapshot.appId === record.appId
    && snapshot.version === record.version
    && snapshot.tbsSha256 === record.tbsSha256
    && snapshot.packageSha256 === record.packageSha256
  )
}

interface AppDialogProps {
  state: AppDialogState | null
  onStateChange: (state: AppDialogState) => void
  onClose: () => void
  onBusinessActivated: (appId: string) => void
}

function formatSize(bytes: number): string {
  if (bytes >= 1024 * 1024) {
    return `${(bytes / (1024 * 1024)).toFixed(1)} MB`
  }
  if (bytes >= 1024) {
    return `${(bytes / 1024).toFixed(1)} KB`
  }
  return `${bytes} B`
}

function IdentityRow({ label, value, testId }: { label: string; value: string; testId?: string }) {
  return (
    <div className="flex justify-between gap-4 border-b border-[#e6eaee] py-1.5 text-sm last:border-b-0">
      <span className="shrink-0 text-[#8b97a4]">{label}</span>
      <span className="break-all text-right text-[#2c3848]" data-testid={testId}>
        {value}
      </span>
    </div>
  )
}

function ErrorNotice({ error }: { error: AppLastError }) {
  const { i18n } = useLingui()
  if (error.kind === 'auth') {
    if (error.reason === 'unauthorized') {
      return (
        <div className="rounded border border-amber-300 bg-amber-50 p-3 text-sm text-amber-800">
          {i18n._('sys.app_management.auth.unauthorized')}
          <a className="ml-2 underline" href="/login" data-testid="relogin-link">
            {i18n._('sys.app_management.auth.relogin')}
          </a>
        </div>
      )
    }
    return (
      <div className="rounded border border-amber-300 bg-amber-50 p-3 text-sm text-amber-800">
        {i18n._('sys.app_management.auth.forbidden')}
      </div>
    )
  }
  if (error.kind === 'refused') {
    return (
      <div className="rounded border border-red-300 bg-red-50 p-3 text-sm text-red-700">
        {i18n._(appReasonKey(error.reason || ''))}
      </div>
    )
  }
  if (error.kind === 'envelope') {
    return (
      <div className="rounded border border-red-300 bg-red-50 p-3 text-sm text-red-700">
        {i18n._(appEnvelopeKey(error.reason || ''))}
      </div>
    )
  }
  if (error.kind === 'indeterminate') {
    return (
      <div className="rounded border border-amber-300 bg-amber-50 p-3 text-sm text-amber-800">
        {i18n._('sys.app_management.errors.indeterminate')}
      </div>
    )
  }
  return (
    <div className="rounded border border-red-300 bg-red-50 p-3 text-sm text-red-700">
      {i18n._(appFileErrorKey(error.reason))}
    </div>
  )
}

function RunOutcomePanel() {
  const { i18n } = useLingui()
  const runOutcome = useAppManagementStore((s) => s.runOutcome)
  if (!runOutcome) return null
  return (
    <div
      className="mt-3 rounded border border-gray-200 bg-gray-50 p-3 text-sm"
      data-testid="run-outcome"
    >
      <p className="font-medium">{i18n._('sys.app_management.run.session_result')}</p>
      {runOutcome.status === 'completed' ? (
        <div className="mt-1 space-y-1">
          <p>
            {i18n._('sys.app_management.run.entry_returned')}: {String(runOutcome.entryReturned)}
          </p>
          <p>
            {i18n._('sys.app_management.run.entry_result')}: {runOutcome.entryResult ?? 'n/a'}
          </p>
          {runOutcome.log && (
            <pre className="max-h-40 overflow-auto whitespace-pre-wrap break-all rounded bg-white p-2 font-mono text-xs">
              {runOutcome.log}
            </pre>
          )}
          {runOutcome.logTruncated && (
            <p className="text-xs text-text-secondary">
              {i18n._('sys.app_management.run.log_truncated')}
            </p>
          )}
        </div>
      ) : (
        <div className="mt-1 space-y-1">
          <p>
            {i18n._('sys.app_management.run.host_refused')}:{' '}
            {i18n._(appReasonKey(runOutcome.reason || ''))}
          </p>
          {runOutcome.detail && <p className="font-mono text-xs">{runOutcome.detail}</p>}
        </div>
      )}
      <p className="mt-2 text-xs text-text-secondary">
        {i18n._('sys.app_management.run.session_note')}
      </p>
    </div>
  )
}

function PackagePicker({ onFile }: { onFile: (e: Event) => void }) {
  const { i18n } = useLingui()
  return (
    <div className="mt-3 rounded-lg border border-dashed border-[#bcc9d5] bg-[#fbfcfe] px-4 py-6 text-center">
      <div className="text-3xl font-light text-[#8797a7]">⇧</div>
      <p className="mt-1 text-sm font-semibold text-[#273746]">
        {i18n._('sys.app_management.dialog.dropzone_title')}
      </p>
      <p className="mt-0.5 text-xs text-[#8b97a4]">
        {i18n._('sys.app_management.dialog.dropzone_single')}
      </p>
      <label className="mt-3 inline-flex cursor-pointer items-center justify-center rounded-md border border-[#dce3e9] bg-white px-4 py-1.5 text-sm text-[#4a5a69] transition-colors hover:bg-[#f5f6f8]">
        <input
          data-testid="neapp-input"
          type="file"
          accept=".neapp"
          className="hidden"
          onChange={onFile}
        />
        {i18n._('sys.app_management.dialog.browse')}
      </label>
    </div>
  )
}

function PackageFileCard({ file }: { file: File | null }) {
  const { i18n } = useLingui()
  if (!file) return null
  return (
    <div
      className="mt-3 flex items-center gap-2 rounded-lg border border-[#e9edf0] bg-[#f7f9fb] px-3 py-2"
      data-testid="package-file"
    >
      <span className="text-[#8896a5]">▤</span>
      <div className="min-w-0 text-left">
        <p className="truncate text-sm font-semibold text-[#334354]">{file.name}</p>
        <p className="text-xs text-[#8896a5]">
          {formatSize(file.size)} · {i18n._('sys.app_management.dialog.file_unverified')}
        </p>
      </div>
    </div>
  )
}

function ManageRow({
  icon,
  label,
  danger,
  disabled,
  onClick,
  testId,
}: {
  icon: string
  label: string
  danger?: boolean
  disabled?: boolean
  onClick: () => void
  testId: string
}) {
  return (
    <button
      type="button"
      data-testid={testId}
      onClick={onClick}
      disabled={disabled}
      className={`flex w-full items-center justify-between rounded-lg border px-3 py-2.5 text-sm font-semibold transition-colors disabled:pointer-events-none disabled:opacity-50 ${
        danger
          ? 'border-[#efcfce] bg-white text-[#c44a48] hover:bg-[#fdf3f2]'
          : 'border-[#dce3e9] bg-white text-[#354455] hover:bg-[#f5f6f8]'
      }`}
    >
      <span>
        {icon} {label}
      </span>
      <span className="text-[#8c9baa]">›</span>
    </button>
  )
}

export { ErrorNotice, RunOutcomePanel }

export default function AppDialog({ state, onStateChange, onClose, onBusinessActivated }: AppDialogProps) {
  const { i18n } = useLingui()
  const view = useAppManagementStore((s) => s.view)
  const authBlock = useAppManagementStore((s) => s.authBlock)
  const querying = useAppManagementStore((s) => s.querying)
  const pendingVerification = useAppManagementStore((s) => s.pendingVerification)
  const installing = useAppManagementStore((s) => s.installing)
  const uninstalling = useAppManagementStore((s) => s.uninstalling)
  const running = useAppManagementStore((s) => s.running)
  const stopping = useAppManagementStore((s) => s.stopping)
  const starting = useAppManagementStore((s) => s.starting)
  const installOutcome = useAppManagementStore((s) => s.installOutcome)
  const stopOutcome = useAppManagementStore((s) => s.stopOutcome)
  const startOutcome = useAppManagementStore((s) => s.startOutcome)
  const lastError = useAppManagementStore((s) => s.lastError)
  const refresh = useAppManagementStore((s) => s.refresh)
  const installPackage = useAppManagementStore((s) => s.installPackage)
  const uninstallApp = useAppManagementStore((s) => s.uninstallApp)
  const runOnce = useAppManagementStore((s) => s.runOnce)
  const stopApp = useAppManagementStore((s) => s.stopApp)
  const startApp = useAppManagementStore((s) => s.startApp)
  const [selectedFile, setSelectedFile] = useState<File | null>(null)
  const [opSnapshot, setOpSnapshot] = useState<OpTargetSnapshot | null>(null)

  useEffect(() => {
    setSelectedFile(null)
    if (state !== 'update' && state !== 'uninstall') setOpSnapshot(null)
  }, [state])

  const record: AppRecord | null = view && (view.kind === 'installed' || view.kind === 'unavailable') ? view.record : null

  const appRunning = view?.kind === 'installed' || view?.kind === 'unavailable' ? view.appRunning === true : false
  const opBlocked = authBlock !== null || pendingVerification || view?.kind === 'undetermined'
  const canInstall = !opBlocked && !installing
  const canUpdate = record !== null && !appRunning && !opBlocked && !installing
  const canUninstall = record !== null && !appRunning && !opBlocked && !uninstalling
  const opBaseMatches = opSnapshot !== null && record !== null && view?.kind === opSnapshot.base
  const opTargetUnchanged = opBaseMatches && snapshotMatches(opSnapshot, record)
  const opTargetChanged = opBaseMatches && !opTargetUnchanged
  const canConfirmUpdate = opTargetUnchanged && canUpdate
  const canConfirmUninstall = opTargetUnchanged && canUninstall
  const runOnceOffered = view?.kind === 'installed'
    && runOnceEligible(view.record.appId, view.record.packageSha256)
  const canRun = runOnceOffered && !appRunning && !running && !opBlocked

  const handleFile = (e: Event) => {
    const target = e.target as HTMLInputElement
    setSelectedFile(target.files && target.files.length > 0 ? target.files[0] : null)
  }

  const openCommittedOp = (next: 'update' | 'uninstall') => {
    if (record !== null && (view?.kind === 'installed' || view?.kind === 'unavailable')) {
      setOpSnapshot({
        base: view.kind,
        publisherId: record.publisherId,
        appId: record.appId,
        version: record.version,
        tbsSha256: record.tbsSha256,
        packageSha256: record.packageSha256,
      })
    } else {
      setOpSnapshot(null)
    }
    onStateChange(next)
  }

  const handleDeployConfirm = async () => {
    if (!selectedFile) return
    await installPackage(selectedFile)
    const s = useAppManagementStore.getState()
    if (s.installOutcome !== null && s.view?.kind === 'installed') {
      const entry = businessUiFor(s.view.record.appId)
      const sidecarServable = s.sidecar.kind === 'active'
        && sidecarProjectionServable(s.sidecar.projection, s.view.record.appId, window.location.origin)
      if (entry || sidecarServable) {
        onBusinessActivated(s.view.record.appId)
      } else {
        onStateChange('manage')
      }
    }
  }

  const handleUpdateConfirm = async () => {
    if (!canConfirmUpdate || !selectedFile) return
    await installPackage(selectedFile)
    const s = useAppManagementStore.getState()
    if (s.installOutcome !== null && s.view?.kind === 'installed') {
      onStateChange('manage')
    }
  }

  const handleUninstallConfirm = async () => {
    if (!canConfirmUninstall) return
    await uninstallApp()
    const s = useAppManagementStore.getState()
    if (s.view?.kind === 'not_installed') {
      onStateChange('uninstall_success')
      return
    }
    if (s.view?.kind === 'cleanup_pending' || s.pendingVerification) {
      onStateChange('uninstall_abnormal')
    }
  }

  const renderError = () => (lastError ? (
      <div className="mt-3" data-testid="last-error">
        <ErrorNotice error={lastError} />
      </div>
    ) : null)

  const renderPendingVerification = () => (pendingVerification ? (
      <div
        className="mt-3 rounded border border-amber-300 bg-amber-50 p-3 text-sm text-amber-800"
        data-testid="pending-verification"
      >
        {i18n._('sys.app_management.errors.pending_verification')}
      </div>
    ) : null)

  const renderDeploy = () => (
    <div>
      <DialogHeader>
        <DialogTitle>{i18n._('sys.app_management.slot.deploy')}</DialogTitle>
        <DialogDescription>{i18n._('sys.app_management.dialog.deploy_desc')}</DialogDescription>
      </DialogHeader>
      <PackagePicker onFile={handleFile} />
      <PackageFileCard file={selectedFile} />
      <p className="mt-3 text-xs text-[#7f8f9e]">
        ⓘ {i18n._('sys.app_management.dialog.file_note')}
      </p>
      {installing && (
        <p className="mt-2 text-xs text-text-secondary" data-testid="install-progress">
          {i18n._('sys.app_management.install.progress')}
        </p>
      )}
      {installOutcome && !installing && (
        <p className="mt-2 text-sm text-green-700" data-testid="install-outcome">
          {installOutcome.status === 'already_installed'
            ? i18n._('sys.app_management.install.already_installed')
            : i18n._('sys.app_management.install.installed')}
        </p>
      )}
      {renderError()}
      {renderPendingVerification()}
      <DialogFooter className="mt-4">
        <Button variant="outline" size="sm" onClick={onClose}>
          {i18n._('sys.app_management.uninstall.cancel')}
        </Button>
        <Button
          size="sm"
          className="text-primary-foreground"
          onClick={handleDeployConfirm}
          disabled={!canInstall || !selectedFile}
          data-testid="deploy-confirm"
        >
          {installing
            ? i18n._('sys.app_management.install.submitting')
            : i18n._('sys.app_management.dialog.deploy_confirm')}
        </Button>
      </DialogFooter>
    </div>
  )

  const renderManage = () => {
    if (!record) {
      return (
        <div>
          <DialogHeader>
            <DialogTitle>{i18n._('sys.app_management.slot.manage')}</DialogTitle>
          </DialogHeader>
          <p className="mt-4 text-sm text-text-secondary">
            {view?.kind === 'undetermined'
              ? i18n._('sys.app_management.state.undetermined_warning')
              : i18n._('sys.app_management.state.querying')}
          </p>
          <DialogFooter className="mt-4">
            <Button variant="outline" size="sm" onClick={onClose}>
              {i18n._('sys.app_management.dialog.close')}
            </Button>
          </DialogFooter>
        </div>
      )
    }
    return (
      <div>
        <DialogHeader>
          <DialogTitle>{i18n._('sys.app_management.slot.manage')}</DialogTitle>
        </DialogHeader>
        <div className="mt-3 flex items-center gap-3 rounded-lg bg-[#f1f4f7] p-3">
          <div className="flex h-11 w-11 shrink-0 items-center justify-center rounded-lg bg-white text-xl text-[#72869a]">
            ▣
          </div>
          <div className="min-w-0">
            <p className="text-sm font-semibold text-[#2b3d4d]">
              {i18n._('sys.app_management.dialog.manage_current')}
            </p>
            <p className="text-xs text-[#8b97a4]">
              {i18n._('sys.app_management.dialog.manage_chip')}
            </p>
          </div>
        </div>
        <div className="mt-2" data-testid="manage-identity">
          <IdentityRow label={i18n._('sys.app_management.labels.version')} value={record.version} />
          <IdentityRow
            label={i18n._('sys.app_management.labels.app_id')}
            value={record.appId}
            testId="manage-app-id"
          />
          <IdentityRow
            label={i18n._('sys.app_management.labels.status')}
            value={
              view?.kind === 'installed'
                ? i18n._('sys.app_management.state.installed')
                : i18n._('sys.app_management.state.unavailable')
            }
            testId="manage-status"
          />
        </div>
        <div className="mt-3 rounded-lg border border-[#e6eaee] p-3" data-testid="runtime-section">
          <div className="flex items-center justify-between gap-2">
            <span
              data-testid="runtime-state"
              className={appRunning ? 'text-sm font-medium text-green-700' : 'text-sm font-medium text-gray-600'}
            >
              {appRunning
                ? i18n._('sys.app_management.run.running')
                : i18n._('sys.app_management.run.idle')}
            </span>
            {appRunning && (
              <Button
                variant="destructive"
                size="sm"
                onClick={stopApp}
                disabled={stopping || opBlocked}
                data-testid="stop-button"
              >
                {stopping
                  ? i18n._('sys.app_management.stop.submitting')
                  : i18n._('sys.app_management.stop.button')}
              </Button>
            )}
            {!appRunning && !runOnceOffered && (
              <Button
                size="sm"
                className="text-primary-foreground"
                onClick={startApp}
                disabled={starting || opBlocked || appRunning}
                data-testid="start-button"
              >
                {starting
                  ? i18n._('sys.app_management.start.submitting')
                  : i18n._('sys.app_management.start.button')}
              </Button>
            )}
          </div>
            {stopOutcome?.status === 'stopping' && (
              <p className="mt-1 text-xs text-text-secondary" data-testid="stop-busy-note">
                {i18n._('sys.app_management.stop.busy_note')}
              </p>
            )}
            {stopOutcome?.status === 'refused' && (
              <p
                className="mt-1 rounded border border-red-300 bg-red-50 p-2 text-xs text-red-700"
                data-testid="stop-refused-note"
              >
                {i18n._('sys.app_management.stop.host_refused')}:{' '}
                {i18n._(appReasonKey(stopOutcome.reason || ''))}
              </p>
            )}
            {startOutcome?.status === 'refused' && (
              <p
                className="mt-1 rounded border border-red-300 bg-red-50 p-2 text-xs text-red-700"
                data-testid="start-refused-note"
              >
                {i18n._('sys.app_management.start.host_refused')}:{' '}
                {i18n._(appReasonKey(startOutcome.reason || ''))}
                {startOutcome.detail ? ` · ${startOutcome.detail}` : ''}
              </p>
            )}
        </div>
        {runOnceOffered && (
          <div className="mt-3 rounded-lg border border-[#e6eaee] p-3" data-testid="run-section">
            <div className="flex items-center justify-between gap-2">
              <p className="text-sm font-semibold">
                {i18n._('sys.app_management.run.title')}
              </p>
            </div>
            <p className="mt-1 text-xs text-text-secondary">
              {i18n._('sys.app_management.run.note')}
            </p>
            <div className="mt-2">
              <Button
                size="sm"
                className="text-primary-foreground"
                onClick={runOnce}
                disabled={!canRun}
                data-testid="run-button"
              >
                {running
                  ? i18n._('sys.app_management.run.submitting')
                  : i18n._('sys.app_management.run.button')}
              </Button>
            </div>
            {appRunning && (
              <p className="mt-1 text-xs text-text-secondary">
                {i18n._('sys.app_management.run.busy_note')}
              </p>
            )}
            <RunOutcomePanel />
          </div>
        )}
        <div className="mt-3 space-y-2">
          <ManageRow
            icon="↻"
            label={i18n._('sys.app_management.dialog.update_row')}
            disabled={!canUpdate}
            onClick={() => openCommittedOp('update')}
            testId="update-row"
          />
          <ManageRow
            icon="◇"
            label={i18n._('sys.app_management.dialog.uninstall_row')}
            danger
            disabled={!canUninstall}
            onClick={() => openCommittedOp('uninstall')}
            testId="uninstall-row"
          />
        </div>
        {appRunning && (
          <p
            className="mt-2 rounded border border-amber-300 bg-amber-50 p-3 text-sm text-amber-800"
            data-testid="running-block-note"
          >
            {i18n._('sys.app_management.dialog.running_block_note')}
          </p>
        )}
        {renderError()}
        {renderPendingVerification()}
        <DialogFooter className="mt-4">
          <Button variant="outline" size="sm" onClick={onClose}>
            {i18n._('sys.app_management.dialog.close')}
          </Button>
        </DialogFooter>
      </div>
    )
  }

  const renderUpdate = () => (
    <div>
      <DialogHeader>
        <DialogTitle>{i18n._('sys.app_management.slot.manage')}</DialogTitle>
      </DialogHeader>
      <button
        type="button"
        data-testid="back-to-manage"
        onClick={() => onStateChange('manage')}
        className="mt-1 text-xs text-[#7d8b9a] hover:text-[#354455]"
      >
        ← {i18n._('sys.app_management.dialog.back')}
      </button>
      <p className="mt-3 text-base font-semibold text-[#273746]">
        {i18n._('sys.app_management.dialog.update_title')}
      </p>
      {record && (
        <p className="text-xs text-[#8694a3]">
          {i18n._('sys.app_management.dialog.current_version')}: {record.version}
        </p>
      )}
      <PackagePicker onFile={handleFile} />
      <PackageFileCard file={selectedFile} />
      <p className="mt-3 text-xs text-[#7f8f9e]">
        ⓘ {i18n._('sys.app_management.dialog.verify_note')}
      </p>
      {installing && (
        <p className="mt-2 text-xs text-text-secondary" data-testid="install-progress">
          {i18n._('sys.app_management.install.progress')}
        </p>
      )}
      {renderError()}
      {renderPendingVerification()}
      {!installing && appRunning && (
        <p
          className="mt-3 rounded border border-amber-300 bg-amber-50 p-3 text-sm text-amber-800"
          data-testid="running-block-note"
        >
          {i18n._('sys.app_management.dialog.running_block_note')}
        </p>
      )}
      {!installing && !appRunning && opSnapshot !== null && view?.kind !== opSnapshot.base && (
        <p
          className="mt-3 rounded border border-amber-300 bg-amber-50 p-3 text-sm text-amber-800"
          data-testid="state-changed-note"
        >
          {i18n._('sys.app_management.dialog.state_changed_note')}
        </p>
      )}
      {!installing && !appRunning && opTargetChanged && (
        <p
          className="mt-3 rounded border border-amber-300 bg-amber-50 p-3 text-sm text-amber-800"
          data-testid="app-changed-note"
        >
          {i18n._('sys.app_management.dialog.app_changed_note')}
        </p>
      )}
      <DialogFooter className="mt-4">
        <Button variant="outline" size="sm" onClick={() => onStateChange('manage')}>
          {i18n._('sys.app_management.uninstall.cancel')}
        </Button>
        <Button
          size="sm"
          className="text-primary-foreground"
          onClick={handleUpdateConfirm}
          disabled={!canConfirmUpdate || !selectedFile}
          data-testid="update-confirm"
        >
          {installing
            ? i18n._('sys.app_management.install.submitting')
            : i18n._('sys.app_management.dialog.update_confirm')}
        </Button>
      </DialogFooter>
    </div>
  )

  const renderUninstall = () => (
    <div>
      <DialogHeader>
        <DialogTitle>{i18n._('sys.app_management.slot.manage')}</DialogTitle>
      </DialogHeader>
      <button
        type="button"
        data-testid="back-to-manage"
        onClick={() => onStateChange('manage')}
        className="mt-1 text-xs text-[#7d8b9a] hover:text-[#354455]"
      >
        ← {i18n._('sys.app_management.dialog.back')}
      </button>
      <div className="mt-3 flex items-center gap-2">
        <span className="flex h-8 w-8 shrink-0 items-center justify-center rounded-full bg-[#feebea] text-lg font-bold text-[#bf4846]">
          !
        </span>
        <p className="text-base font-semibold text-[#b14643]">
          {i18n._('sys.app_management.dialog.uninstall_question')}
        </p>
      </div>
      <div className="mt-3 rounded-lg border border-[#f0d8be] bg-[#fff8f1] p-3 text-sm leading-6 text-[#745638]">
        {i18n._('sys.app_management.dialog.uninstall_warning')}
      </div>
      <p className="mt-2 text-xs text-[#8290a0]">
        {i18n._('sys.app_management.dialog.uninstall_data_note')}
      </p>
      {uninstalling && (
        <div className="mt-2" data-testid="uninstall-progress">
          <p className="text-sm font-medium text-text-secondary">
            {i18n._('sys.app_management.uninstall.submitting')}
          </p>
          <p className="mt-1 text-xs text-text-secondary">
            {i18n._('sys.app_management.uninstall.processing_detail')}
          </p>
        </div>
      )}
      {renderError()}
      {renderPendingVerification()}
      {!uninstalling && appRunning && (
        <p
          className="mt-3 rounded border border-amber-300 bg-amber-50 p-3 text-sm text-amber-800"
          data-testid="running-block-note"
        >
          {i18n._('sys.app_management.dialog.running_block_note')}
        </p>
      )}
      {!uninstalling && !appRunning && opSnapshot !== null && view?.kind !== opSnapshot.base && (
        <p
          className="mt-3 rounded border border-amber-300 bg-amber-50 p-3 text-sm text-amber-800"
          data-testid="state-changed-note"
        >
          {i18n._('sys.app_management.dialog.state_changed_note')}
        </p>
      )}
      {!uninstalling && !appRunning && opTargetChanged && (
        <p
          className="mt-3 rounded border border-amber-300 bg-amber-50 p-3 text-sm text-amber-800"
          data-testid="app-changed-note"
        >
          {i18n._('sys.app_management.dialog.app_changed_note')}
        </p>
      )}
      <DialogFooter className="mt-4">
        <Button variant="outline" size="sm" onClick={() => onStateChange('manage')}>
          {i18n._('sys.app_management.uninstall.cancel')}
        </Button>
        <Button
          variant="destructive"
          size="sm"
          onClick={handleUninstallConfirm}
          disabled={!canConfirmUninstall}
          data-testid="confirm-uninstall"
        >
          {uninstalling
            ? i18n._('sys.app_management.uninstall.submitting')
            : i18n._('sys.app_management.dialog.uninstall_confirm')}
        </Button>
      </DialogFooter>
    </div>
  )

  const renderUninstallSuccess = () => (
    <div>
      <DialogHeader>
        <DialogTitle>{i18n._('sys.app_management.uninstall_success.title')}</DialogTitle>
      </DialogHeader>
      <div
        className="mt-3 rounded-lg border border-[#cfe8d2] bg-[#f2fbf4] p-3 text-sm text-[#2f6b3a]"
        data-testid="uninstall-success"
      >
        <p className="font-medium">{i18n._('sys.app_management.uninstall_success.heading')}</p>
        <p className="mt-1">{i18n._('sys.app_management.uninstall_success.detail')}</p>
      </div>
      <DialogFooter className="mt-4">
        <Button variant="outline" size="sm" onClick={onClose} data-testid="uninstall-success-close">
          {i18n._('sys.app_management.dialog.close')}
        </Button>
      </DialogFooter>
    </div>
  )

  const renderUninstallAbnormal = () => (
    <div>
      <DialogHeader>
        <DialogTitle>{i18n._('sys.app_management.uninstall_abnormal.title')}</DialogTitle>
      </DialogHeader>
      <div
        className="mt-3 rounded-lg border border-[#f0d8be] bg-[#fff8f1] p-3 text-sm text-[#745638]"
        data-testid="uninstall-abnormal"
      >
        <p className="font-medium">{i18n._('sys.app_management.uninstall_abnormal.heading')}</p>
        <p className="mt-1">{i18n._('sys.app_management.uninstall_abnormal.detail')}</p>
        {view?.kind === 'cleanup_pending' && (
          <p className="mt-2 font-mono text-xs" data-testid="uninstall-abnormal-state">
            cleanup_pending
          </p>
        )}
      </div>
      <DialogFooter className="mt-4">
        <Button variant="outline" size="sm" onClick={onClose} data-testid="uninstall-abnormal-close">
          {i18n._('sys.app_management.dialog.close')}
        </Button>
      </DialogFooter>
    </div>
  )

  const renderStatus = () => (
    <div>
      <DialogHeader>
        <DialogTitle>{i18n._('sys.app_management.slot.status')}</DialogTitle>
        <DialogDescription>{i18n._('sys.app_management.page.abnormal_heading')}</DialogDescription>
      </DialogHeader>
      {authBlock !== null && (
        <div className="mt-3">
          <ErrorNotice
            error={{ scope: 'query', kind: 'auth', reason: authBlock }}
          />
        </div>
      )}
      {authBlock === null && view === null && (
        <p className="mt-3 text-sm text-text-secondary" data-testid="status-querying">
          {i18n._('sys.app_management.state.querying')}
        </p>
      )}
      {authBlock === null && view?.kind === 'undetermined' && (
        <div className="mt-3 rounded border border-red-300 bg-red-50 p-3 text-sm text-red-700">
          <p>{i18n._('sys.app_management.state.undetermined_warning')}</p>
          {view.reason && (
            <p className="mt-1">
              <span className="text-[#8290a0]">
                {i18n._('sys.app_management.page.abnormal_reason')}:
              </span>{' '}
              <span className="font-mono text-xs">{view.reason}</span>
            </p>
          )}
        </div>
      )}
      {authBlock === null && view?.kind === 'cleanup_pending' && (
        <div className="mt-3 rounded border border-[#f0d8be] bg-[#fff8f1] p-3 text-sm text-[#745638]">
          <p className="font-medium">{i18n._('sys.app_management.cleanup.title')}</p>
          <p className="mt-1">{i18n._('sys.app_management.cleanup.detail')}</p>
        </div>
      )}
      {view?.kind === 'unavailable' && record && (
        <div className="mt-3">
          <div className="rounded border border-orange-300 bg-orange-50 p-3 text-sm text-orange-800">
            <p>{i18n._('sys.app_management.state.unavailable_note')}</p>
            <p className="mt-1 font-mono text-xs" data-testid="status-reason">
              {view.reason}
            </p>
          </div>
          <div className="mt-2">
            <IdentityRow
              label={i18n._('sys.app_management.labels.publisher')}
              value={record.publisherId}
            />
            <IdentityRow
              label={i18n._('sys.app_management.labels.app_id')}
              value={record.appId}
            />
            <IdentityRow
              label={i18n._('sys.app_management.labels.version')}
              value={record.version}
            />
            <IdentityRow
              label={i18n._('sys.app_management.labels.tbs_sha256')}
              value={record.tbsSha256}
            />
          </div>
          <div className="mt-3 space-y-2">
            <ManageRow
              icon="↻"
              label={i18n._('sys.app_management.dialog.update_row')}
              disabled={!canUpdate}
              onClick={() => openCommittedOp('update')}
              testId="update-row"
            />
            <ManageRow
              icon="◇"
              label={i18n._('sys.app_management.dialog.uninstall_row')}
              danger
              disabled={!canUninstall}
              onClick={() => openCommittedOp('uninstall')}
              testId="uninstall-row"
            />
          </div>
          {appRunning && (
            <p
              className="mt-2 rounded border border-amber-300 bg-amber-50 p-3 text-sm text-amber-800"
              data-testid="running-block-note"
            >
              {i18n._('sys.app_management.dialog.running_block_note')}
            </p>
          )}
        </div>
      )}
      {renderPendingVerification()}
      <Button
        variant="outline"
        size="sm"
        className="mt-3"
        onClick={refresh}
        disabled={querying}
        data-testid="status-refresh"
      >
        {i18n._('sys.app_management.actions.refresh')}
      </Button>
      <DialogFooter className="mt-4">
        <Button variant="outline" size="sm" onClick={onClose}>
          {i18n._('sys.app_management.dialog.close')}
        </Button>
      </DialogFooter>
    </div>
  )

  return (
    <Dialog
      open={state !== null}
      onOpenChange={(open: boolean) => {
        if (!open) onClose()
      }}
    >
      <DialogContent className="max-w-md">
        {state === 'deploy' && renderDeploy()}
        {state === 'manage' && renderManage()}
        {state === 'update' && renderUpdate()}
        {state === 'uninstall' && renderUninstall()}
        {state === 'uninstall_success' && renderUninstallSuccess()}
        {state === 'uninstall_abnormal' && renderUninstallAbnormal()}
        {state === 'status' && renderStatus()}
      </DialogContent>
    </Dialog>
  )
}
