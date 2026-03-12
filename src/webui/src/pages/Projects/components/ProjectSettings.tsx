import React, { useEffect, useRef, useState } from 'react';
import { useTranslation } from 'react-i18next';
import { Button, Form, Input, message } from 'antd';
import type { InputNumberRef } from '@rc-component/input-number';
import { ScheduleForm } from './ScheduleForm';
import type { Schedule } from '@/types/project';

const MAX_RUN_TIMEOUT_MS = 2147483647;

interface ProjectSettingsProps {
  projectName: string;
  schedule: Schedule;
  onSave: (data: { name: string; schedule: Schedule }) => Promise<boolean>;
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

function validateProjectName(value: string, requiredMessage: string): string | null {
  return value.trim().length > 0 ? null : requiredMessage;
}

export const ProjectSettings: React.FC<ProjectSettingsProps> = ({
  projectName,
  schedule,
  onSave,
}) => {
  const { t } = useTranslation();
  const [name, setName] = useState(projectName);
  const invalidRunTimeoutMessage = t('projects.schedule.run_timeout_invalid');
  const requiredNameMessage = t('projects.settings.name_required');
  const [nameError, setNameError] = useState<string | null>(
    validateProjectName(projectName, requiredNameMessage),
  );
  const [value, setValue] = useState<Schedule>(schedule);
  const [runTimeoutError, setRunTimeoutError] = useState<string | null>(
    validateRunTimeoutMs(schedule, invalidRunTimeoutMessage),
  );
  const [saving, setSaving] = useState(false);
  const runTimeoutInputRef = useRef<InputNumberRef | null>(null);

  useEffect(() => {
    setName(projectName);
    setNameError(validateProjectName(projectName, requiredNameMessage));
  }, [projectName, requiredNameMessage]);

  useEffect(() => {
    setValue(schedule);
    setRunTimeoutError(validateRunTimeoutMs(schedule, invalidRunTimeoutMessage));
  }, [schedule, invalidRunTimeoutMessage]);

  const handleScheduleChange = (next: Schedule) => {
    setValue(next);
    setRunTimeoutError(validateRunTimeoutMs(next, invalidRunTimeoutMessage));
  };

  const handleNameChange = (event: React.ChangeEvent<HTMLInputElement>) => {
    const nextName = event.target.value;
    setName(nextName);
    setNameError(validateProjectName(nextName, requiredNameMessage));
  };

  const handleSave = async () => {
    const normalizedName = name.trim();
    const nextNameError = validateProjectName(normalizedName, requiredNameMessage);
    if (nextNameError) {
      setNameError(nextNameError);
      message.error(nextNameError);
      return;
    }

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
    const ok = await onSave({ name: normalizedName, schedule: nextValue });
    if (ok) {
      setName(normalizedName);
      message.success(t('projects.settings.save_success'));
    } else {
      message.error(t('projects.settings.save_fail'));
    }
    setSaving(false);
  };

  return (
    <div data-testid="project-settings">
      <Form layout="vertical">
        <Form.Item
          label={t('projects.settings.name')}
          validateStatus={nameError ? 'error' : undefined}
          help={nameError ?? undefined}
        >
          <Input
            value={name}
            onChange={handleNameChange}
            onBlur={() => setNameError(validateProjectName(name, requiredNameMessage))}
            placeholder={t('projects.settings.name_placeholder')}
            data-testid="project-name-input"
          />
        </Form.Item>
      </Form>

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
        data-testid="save-settings-btn"
      >
        {t('projects.settings.save')}
      </Button>
    </div>
  );
};
