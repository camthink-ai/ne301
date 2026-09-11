import { useState, useEffect } from 'preact/hooks';
import { useLingui } from '@lingui/react';
import { Button } from '@/components/ui/button';
import { toast } from 'sonner';
import storageManagement from '@/services/api/storageManagement';

/**
 * Two-step confirmation dialog for formatting the internal Flash LittleFS
 * volume. Step 1 warns; step 2 requires a second explicit click (button is
 * disabled for 3s to prevent accidental double-click). Destructive — erases
 * all flash files (logs, captures). NVS config is unaffected.
 */
export default function FormatFlashDialog({
  onClose,
  onConfirmed,
}: {
  onClose: () => void;
  onConfirmed: () => void;
}) {
  const { i18n } = useLingui();
  const [step, setStep] = useState<1 | 2>(1);
  const [busy, setBusy] = useState(false);
  const [canConfirm, setCanConfirm] = useState(false);

  useEffect(() => {
    if (step !== 2) return;
    setCanConfirm(false);
    const id = setTimeout(() => setCanConfirm(true), 3000);
    return () => clearTimeout(id);
  }, [step]);

  const doFormat = async () => {
    setBusy(true);
    try {
      await storageManagement.formatFlash();
      toast.success(i18n._('sys.storage_management.format_success'));
      onConfirmed();
    } catch {
      toast.error(i18n._('sys.storage_management.format_failed'));
    } finally {
      setBusy(false);
    }
  };

  return (
    <div className="fixed inset-0 z-[80] flex items-center justify-center bg-black/50 p-4">
      <div className="bg-white rounded-lg shadow-2xl w-full max-w-sm mx-4 space-y-4">
        <h3 className="text-lg font-bold pt-5 px-5">
          ⚠ {i18n._('sys.storage_management.format_confirm_title')}
        </h3>
        {step === 1 ? (
          <>
            <p className="text-sm text-gray-600 leading-relaxed px-5">
              {i18n._('sys.storage_management.format_confirm_step1')}
            </p>
            <div className="flex gap-2 justify-end px-5 pb-5">
              <Button variant="outline" onClick={onClose} disabled={busy}>
                {i18n._('sys.file_browsing.cancel')}
              </Button>
              <Button variant="destructive" onClick={() => setStep(2)}>
                {i18n._('sys.storage_management.format_next')}
              </Button>
            </div>
          </>
        ) : (
          <>
            <p className="text-sm text-red-600 leading-relaxed px-5 font-medium">
              {i18n._('sys.storage_management.format_confirm_step2')}
            </p>
            <div className="flex gap-2 justify-end px-5 pb-5">
              <Button variant="outline" onClick={onClose} disabled={busy}>
                {i18n._('sys.file_browsing.cancel')}
              </Button>
              <Button variant="destructive" disabled={!canConfirm || busy} onClick={doFormat}>
                {busy ? '⏳' : '🗑'} {i18n._('sys.storage_management.format_confirm_btn')}
              </Button>
            </div>
          </>
        )}
      </div>
    </div>
  );
}
