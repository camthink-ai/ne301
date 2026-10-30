import { useEffect, useState } from 'preact/hooks'
import { useLingui } from '@lingui/react'
import { Card, CardContent } from '@/components/ui/card'
import { Button } from '@/components/ui/button'
import { Tabs, TabsList, TabsTrigger, TabsContent } from '@/components/ui/tabs'
import MqttModule from './mqtt-module'
import WebhookModule from './webhook-module'
import AppDialog, { type AppDialogState } from './appDialog'
import SidecarAppFrame from './sidecarAppFrame'
import { firstSlotFor } from './appSlot'
import { useAppManagementStore } from '@/store/appManagement'

export default function ApplicationManagement() {
  const { i18n } = useLingui()
  const view = useAppManagementStore((s) => s.view)
  const querying = useAppManagementStore((s) => s.querying)
  const authBlock = useAppManagementStore((s) => s.authBlock)
  const installOutcome = useAppManagementStore((s) => s.installOutcome)
  const sidecar = useAppManagementStore((s) => s.sidecar)
  const refresh = useAppManagementStore((s) => s.refresh)
  const refreshSidecar = useAppManagementStore((s) => s.refreshSidecar)
  const [dialogState, setDialogState] = useState<AppDialogState | null>(null)
  const [activeTab, setActiveTab] = useState('app-status')

  useEffect(() => {
    refresh()
    refreshSidecar()
  }, [refresh, refreshSidecar])

  const slot = firstSlotFor(view, authBlock, sidecar)

  const recheck = () => {
    refresh()
    refreshSidecar()
  }

  const renderInstallOutcome = () => {
    if (!installOutcome || view?.kind !== 'installed') return null
    return (
      <p className="mb-3 text-sm text-green-700" data-testid="install-outcome">
        {installOutcome.status === 'already_installed'
          ? i18n._('sys.app_management.install.already_installed')
          : i18n._('sys.app_management.install.installed')}
      </p>
    )
  }

  const renderStatusPanel = () => {
    if (authBlock !== null) {
      return (
        <div
          className="rounded border border-amber-300 bg-amber-50 p-3 text-sm text-amber-800"
          data-testid="auth-block-notice"
        >
          {authBlock === 'unauthorized'
            ? i18n._('sys.app_management.auth.unauthorized')
            : i18n._('sys.app_management.auth.forbidden')}
          {authBlock === 'unauthorized' && (
            <a className="ml-2 underline" href="/login" data-testid="relogin-link">
              {i18n._('sys.app_management.auth.relogin')}
            </a>
          )}
        </div>
      )
    }
    if (view === null) {
      return (
        <p className="py-6 text-center text-sm text-[#718093]" data-testid="app-status-querying">
          {i18n._('sys.app_management.page.querying')}
        </p>
      )
    }
    if (view.kind === 'undetermined') {
      return (
        <div
          className="rounded border border-amber-300 bg-amber-50 p-3 text-sm text-amber-800"
          data-testid="undetermined-warning"
        >
          <p>{i18n._('sys.app_management.page.undetermined')}</p>
          {view.reason && (
            <p className="mt-1 font-mono text-xs">
              {i18n._('sys.app_management.page.abnormal_reason')}: {view.reason}
            </p>
          )}
          <Button
            variant="outline"
            size="sm"
            className="mt-2"
            onClick={recheck}
            disabled={querying}
            data-testid="status-refresh"
          >
            {i18n._('sys.app_management.page.recheck')}
          </Button>
        </div>
      )
    }
    if (view.kind === 'cleanup_pending') {
      return (
        <div
          className="rounded border border-[#f0d8be] bg-[#fff8f1] p-3 text-sm text-[#745638]"
          data-testid="cleanup-pending-warning"
        >
          <p className="font-medium">{i18n._('sys.app_management.cleanup.title')}</p>
          <p className="mt-1">{i18n._('sys.app_management.cleanup.detail')}</p>
          <Button
            variant="outline"
            size="sm"
            className="mt-2"
            onClick={recheck}
            disabled={querying}
            data-testid="status-refresh"
          >
            {i18n._('sys.app_management.page.recheck')}
          </Button>
        </div>
      )
    }
    if (view.kind === 'unavailable') {
      return (
        <div>
          <div
            className="rounded border border-orange-300 bg-orange-50 p-3 text-sm text-orange-800"
            data-testid="unavailable-warning"
          >
            <p>
              {i18n._('sys.app_management.state.unavailable_note')}{' '}
              <span className="font-mono text-xs">{view.reason}</span>
            </p>
          </div>
          <Button
            variant="outline"
            size="sm"
            className="mt-3"
            onClick={() => setDialogState('status')}
            data-testid="slot-manage"
          >
            {i18n._('sys.app_management.slot.manage')}
          </Button>
        </div>
      )
    }
    if (view.kind === 'not_installed') {
      return (
        <div
          className="flex flex-col items-center gap-2 px-4 py-8 text-center"
          data-testid="app-status-empty"
        >
          <p className="text-sm font-medium text-[#334354]">
            {i18n._('sys.app_management.page.empty_title')}
          </p>
          <p className="text-xs text-[#718093]">
            {i18n._('sys.app_management.page.empty_hint')}
          </p>
          <Button
            size="sm"
            className="mt-2 text-primary-foreground"
            onClick={() => setDialogState('deploy')}
            data-testid="slot-deploy"
          >
            {i18n._('sys.app_management.page.install_app')}
          </Button>
        </div>
      )
    }
    if (view.kind === 'installed') {
      if (slot.kind === 'business' || slot.kind === 'sidecar') {
        return (
          <div>
            <div className="mb-3 flex justify-end">
              <button
                type="button"
                data-testid="business-manage"
                onClick={() => setDialogState('manage')}
                className="inline-flex items-center gap-1 text-sm text-text-secondary transition-colors hover:text-primary"
              >
                ⚙ {i18n._('sys.app_management.slot.manage')}
              </button>
            </div>
            {slot.kind === 'sidecar' ? (
              <SidecarAppFrame
                projection={slot.projection}
                appId={view.record.appId}
              />
            ) : (
              slot.entry.render(view.record)
            )}
          </div>
        )
      }
      return (
        <div>
          <div
            className="rounded border border-[#e6eaee] bg-[#f7f9fb] p-3 text-sm"
            data-testid="no-business-notice"
          >
            <p className="text-[#334354]">
              {i18n._('sys.app_management.page.installed_no_business')}
            </p>
            <p className="mt-1 text-xs text-[#718093]">
              {i18n._('sys.app_management.page.installed_no_business_hint')}
            </p>
          </div>
          <Button
            variant="outline"
            size="sm"
            className="mt-3"
            onClick={() => setDialogState('manage')}
            data-testid="slot-manage"
          >
            {i18n._('sys.app_management.slot.manage')}
          </Button>
        </div>
      )
    }
    return null
  }

  return (
    <div className="flex justify-center">
      <Card className="sm:w-xl w-full mx-4 my-4">
        <CardContent>
          <Tabs value={activeTab} onValueChange={setActiveTab}>
            <TabsList className="w-full">
              <TabsTrigger value="app-status" data-testid="tab-app-status">
                {i18n._('sys.app_management.slot.status')}
              </TabsTrigger>
              <TabsTrigger value="mqtt-module" data-testid="tab-mqtt">MQTT/MQTTS</TabsTrigger>
              <TabsTrigger value="webhook-module" data-testid="tab-webhook">Webhook</TabsTrigger>
            </TabsList>
            <TabsContent value="app-status">
              <div data-testid="app-status-panel">
                {renderInstallOutcome()}
                {renderStatusPanel()}
              </div>
            </TabsContent>
            <TabsContent value="mqtt-module">
              <MqttModule />
            </TabsContent>
            <TabsContent value="webhook-module">
              <WebhookModule />
            </TabsContent>
          </Tabs>
        </CardContent>
      </Card>
      <AppDialog
        state={dialogState}
        onStateChange={setDialogState}
        onClose={() => setDialogState(null)}
        onBusinessActivated={() => {
          setDialogState(null)
          setActiveTab('app-status')
        }}
      />
    </div>
  )
}
