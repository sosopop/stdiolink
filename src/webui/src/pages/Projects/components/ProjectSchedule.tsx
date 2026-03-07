import React, { useRef, useState } from 'react';
import { useTranslation } from 'react-i18next';
import { Button, message } from 'antd';
import type { InputNumberRef } from '@rc-component/input-number';
import { ScheduleForm } from './ScheduleForm';
import type { Schedule } from '@/types/project';

const MAX_RUN_TIMEOUT_MS = 2147483647;

interface ProjectScheduleProps {
  schedule: Schedule;
  onSave: (schedule: Schedule) => Promise<boolean>;
}

function validateRunTimeoutMs(schedule: Schedule, invalidMessage: string): string | null {
  const value = schedule.runTimeoutMs;
  if (value == null) {
    return null;
  }
  if (!Number.isFinite(value) || !Number.isInteger(value) || value < 0 || value > MAX_RUN_TIMEOUT_MS) {
    return invalidMessage;
  }
  return null;
}

function parseRunTimeoutInput(raw: string): number | null {
  const trimmed = raw.trim();
  if (trimmed === '') {
    return 0;
  }
  if (!/^\d+$/.test(trimmed)) {
    return null;
  }
  const parsed = Number(trimmed);
  return Number.isSafeInteger(parsed) && parsed <= MAX_RUN_TIMEOUT_MS ? parsed : null;
}

export const ProjectSchedule: React.FC<ProjectScheduleProps> = ({ schedule, onSave }) => {
  const { t } = useTranslation();
  const [value, setValue] = useState<Schedule>(schedule);
  const invalidRunTimeoutMessage = t('projects.schedule.run_timeout_invalid');
  const [runTimeoutError, setRunTimeoutError] = useState<string | null>(
    validateRunTimeoutMs(schedule, invalidRunTimeoutMessage),
  );
  const [saving, setSaving] = useState(false);
  const runTimeoutInputRef = useRef<InputNumberRef | null>(null);

  const handleScheduleChange = (next: Schedule) => {
    setValue(next);
    setRunTimeoutError(validateRunTimeoutMs(next, invalidRunTimeoutMessage));
  };

  const handleSave = async () => {
    const rawInput = runTimeoutInputRef.current?.value ?? String(value.runTimeoutMs ?? '');
    const parsedRunTimeout = parseRunTimeoutInput(rawInput);
    if (parsedRunTimeout == null) {
      setRunTimeoutError(invalidRunTimeoutMessage);
      message.error(invalidRunTimeoutMessage);
      return;
    }
    const validationError = runTimeoutError ?? validateRunTimeoutMs(value, invalidRunTimeoutMessage);
    if (validationError) {
      setRunTimeoutError(validationError);
      message.error(validationError);
      return;
    }
    const nextValue =
      parsedRunTimeout === (value.runTimeoutMs ?? 0)
        ? value
        : { ...value, runTimeoutMs: parsedRunTimeout };
    setSaving(true);
    const ok = await onSave(nextValue);
    if (ok) message.success(t('projects.schedule.save_success'));
    else message.error(t('projects.schedule.save_fail'));
    setSaving(false);
  };

  return (
    <div data-testid="project-schedule">
      <ScheduleForm
        value={value}
        onChange={handleScheduleChange}
        runTimeoutError={runTimeoutError}
        onRunTimeoutValidationChange={setRunTimeoutError}
        runTimeoutInputRef={runTimeoutInputRef}
      />
      <Button
        type="primary"
        onClick={handleSave}
        loading={saving}
        disabled={!!runTimeoutError}
        style={{ marginTop: 16 }}
        data-testid="save-schedule-btn"
      >
        {t('projects.schedule.save')}
      </Button>
    </div>
  );
};
