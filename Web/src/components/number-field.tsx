import { useEffect, useRef, useState } from 'react';
import { Input } from '@/components/ui/input';

interface NumberFieldProps {
  /** Committed value shown when not editing */
  value: number;
  min: number;
  max: number;
  /** Fired with the parsed + clamped value on blur / Enter, only when it changed */
  onCommit: (v: number) => void;
  className?: string;
  step?: number;
  disabled?: boolean;
}

/**
 * Number input that allows intermediate typing states.
 *
 * A plain controlled <Input> that clamps inside onChange breaks typing: with
 * min=16, the first keystroke of "20" ("2") is already below the floor and
 * gets rewritten to 16, making most targets unreachable. Here the raw text is
 * kept as a local draft while editing; parsing + clamping + reporting happen
 * once on blur (or Enter). Escape reverts to the last committed value.
 */
export function NumberField({
  value,
  min,
  max,
  onCommit,
  className,
  step,
  disabled,
}: NumberFieldProps) {
  const [draft, setDraft] = useState(String(value));
  const focused = useRef(false);
  // Escape sets this right before blurring: setDraft is batched, so when the
  // blur event fires synchronously the commit closure would still see the
  // pre-Escape draft and submit it. Blur consumes the flag and skips commit.
  const revertPending = useRef(false);

  // Follow external value changes only while not editing
  useEffect(() => {
    if (!focused.current) {
      setDraft(String(value));
    }
  }, [value]);

  const commit = () => {
    const n = Math.round(Number(draft));
    const clamped = Math.max(min, Math.min(max, Number.isFinite(n) ? n : min));
    setDraft(String(clamped));
    if (clamped !== value) {
      onCommit(clamped);
    }
  };

  return (
    <Input
      className={className}
      type="number"
      inputMode="numeric"
      min={min}
      max={max}
      step={step}
      disabled={disabled}
      value={draft}
      onChange={(e) => setDraft((e.target as HTMLInputElement).value)}
      onFocus={() => { focused.current = true; }}
      onBlur={() => {
        focused.current = false;
        if (revertPending.current) {
          revertPending.current = false;
          return;
        }
        commit();
      }}
      onKeyDown={(e) => {
        if (e.key === 'Enter') {
          e.preventDefault();
          (e.target as HTMLInputElement).blur();
        } else if (e.key === 'Escape') {
          revertPending.current = true;
          setDraft(String(value));
          (e.target as HTMLInputElement).blur();
        }
      }}
    />
  );
}
