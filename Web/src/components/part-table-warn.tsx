import { useRef, useState } from 'preact/hooks';
import { useLingui } from '@lingui/react';
import {
  Dialog,
  DialogContent,
  DialogHeader,
  DialogTitle,
  DialogFooter,
} from '@/components/dialog';
import { Button } from '@/components/ui/button';
import systemApis, { type FirmwareType } from '@/services/api/system';
import { sliceFile } from '@/utils';

/** Partition-layout drift warning shared by every single-firmware upload
 *  flow. The device stamps each package with a CRC of the partition table
 *  it was built for; precheck flags a mismatch against the running
 *  firmware's table. Burn addresses come from the RUNNING table, so the
 *  user must choose: continue anyway, or switch to the bundle path (which
 *  carries its own table and migrates layouts).
 *
 *  ask() resolves true when the user chose to continue. */
export function usePartTableWarn(onGoBundle: () => void) {
  const { i18n } = useLingui();
  const resolveRef = useRef<((v: boolean) => void) | null>(null);
  const [open, setOpen] = useState(false);

  const ask = () => new Promise<boolean>((resolve) => {
    resolveRef.current = resolve;
    setOpen(true);
  });

  const answer = (proceed: boolean, goBundle = false) => {
    setOpen(false);
    resolveRef.current?.(proceed);
    resolveRef.current = null;
    if (!proceed && goBundle) onGoBundle();
  };

  const dialog = (
    <Dialog
      open={open}
      onOpenChange={(o) => {
        if (!o) answer(false);
      }}
    >
      <DialogContent>
        <DialogHeader>
          <DialogTitle>
            {i18n._('sys.system_management.part_table_warn_title')}
          </DialogTitle>
        </DialogHeader>
        <p className="text-sm text-text-primary my-4">
          {i18n._('sys.system_management.part_table_warn_desc')}
        </p>
        <DialogFooter>
          <Button variant="outline" onClick={() => answer(false, true)}>
            {i18n._('sys.system_management.part_table_warn_bundle')}
          </Button>
          <Button variant="primary" onClick={() => answer(true)}>
            {i18n._('sys.system_management.part_table_warn_continue')}
          </Button>
        </DialogFooter>
      </DialogContent>
    </Dialog>
  );

  return { ask, dialog };
}

/** Single-firmware precheck with the layout-drift gate: slices the package
 *  header, asks the device to validate it, and when the device flags a
 *  partition-table mismatch blocks until the user answers the warning.
 *  @return true when the upload may proceed */
export async function precheckWithLayoutWarn(
  file: File,
  type: FirmwareType,
  ask: () => Promise<boolean>,
): Promise<boolean> {
  const contentPreview = await sliceFile(file, 2048);
  if (!contentPreview.size) return false;
  const res = await systemApis.preCheckReq(contentPreview, type);
  const data = (res as { data?: { part_table_changed?: boolean } } | undefined)?.data;
  return !(data?.part_table_changed && !(await ask()));
}
