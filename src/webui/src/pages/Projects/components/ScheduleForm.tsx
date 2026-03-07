import React from 'react';
import { useTranslation } from 'react-i18next';
import { Form, Select, InputNumber } from 'antd';
import type { InputNumberRef } from '@rc-component/input-number';
import type { Schedule, ScheduleType } from '@/types/project';

const MAX_RUN_TIMEOUT_MS = 2147483647;

interface ScheduleFormProps {
  value: Schedule;
  onChange: (value: Schedule) => void;
  runTimeoutError?: string | null;
  onRunTimeoutValidationChange?: (error: string | null) => void;
  runTimeoutInputRef?: React.Ref<InputNumberRef>;
}

export const ScheduleForm: React.FC<ScheduleFormProps> = ({
  value,
  onChange,
  runTimeoutError,
  onRunTimeoutValidationChange,
  runTimeoutInputRef,
}) => {
  const { t } = useTranslation();

  const scheduleTypes: { label: string; value: ScheduleType }[] = [
    { label: t('projects.schedule.manual'), value: 'manual' },
    { label: t('projects.schedule.daemon'), value: 'daemon' },
    { label: t('projects.schedule.fixed_rate'), value: 'fixed_rate' },
  ];

  const handleTypeChange = (type: ScheduleType) => {
    onChange({ ...value, type });
  };

  const handleRunTimeoutChange = (v: string | number | null) => {
    if (v == null || v === '') {
      onRunTimeoutValidationChange?.(null);
      onChange({ ...value, runTimeoutMs: 0 });
      return;
    }

    const raw = typeof v === 'number' ? String(v) : v.trim();
    if (!/^\d+$/.test(raw)) {
      onRunTimeoutValidationChange?.(t('projects.schedule.run_timeout_invalid'));
      return;
    }

    const parsed = Number(raw);
    if (!Number.isSafeInteger(parsed) || parsed > MAX_RUN_TIMEOUT_MS) {
      onRunTimeoutValidationChange?.(t('projects.schedule.run_timeout_invalid'));
      return;
    }

    onRunTimeoutValidationChange?.(null);
    onChange({ ...value, runTimeoutMs: parsed });
  };

  const handleRunTimeoutBlur = (event: React.FocusEvent<HTMLInputElement>) => {
    const raw = event.target.value.trim();
    if (raw === '') {
      onRunTimeoutValidationChange?.(null);
      onChange({ ...value, runTimeoutMs: 0 });
      return;
    }
    if (!/^\d+$/.test(raw)) {
      onRunTimeoutValidationChange?.(t('projects.schedule.run_timeout_invalid'));
      return;
    }

    const parsed = Number(raw);
    if (!Number.isSafeInteger(parsed) || parsed > MAX_RUN_TIMEOUT_MS) {
      onRunTimeoutValidationChange?.(t('projects.schedule.run_timeout_invalid'));
      return;
    }

    onRunTimeoutValidationChange?.(null);
    if (parsed !== (value.runTimeoutMs ?? 0)) {
      onChange({ ...value, runTimeoutMs: parsed });
    }
  };

  const handleRunTimeoutInput = (text: string) => {
    const raw = text.trim();
    if (raw === '') {
      onRunTimeoutValidationChange?.(null);
      return;
    }
    if (!/^\d+$/.test(raw)) {
      onRunTimeoutValidationChange?.(t('projects.schedule.run_timeout_invalid'));
      return;
    }

    const parsed = Number(raw);
    onRunTimeoutValidationChange?.(
      Number.isSafeInteger(parsed) && parsed <= MAX_RUN_TIMEOUT_MS
        ? null
        : t('projects.schedule.run_timeout_invalid'),
    );
  };

  return (
    <div data-testid="schedule-form">
      <Form layout="vertical">
        <Form.Item label={t('projects.schedule.type')}>
          <Select
            value={value.type}
            onChange={handleTypeChange}
            options={scheduleTypes}
            data-testid="schedule-type"
          />
        </Form.Item>

        <Form.Item
          label={t('projects.schedule.run_timeout')}
          extra={t('projects.schedule.run_timeout_hint')}
          validateStatus={runTimeoutError ? 'error' : undefined}
          help={runTimeoutError ?? undefined}
        >
          <InputNumber
            ref={runTimeoutInputRef}
            value={String(value.runTimeoutMs ?? 0)}
            onChange={handleRunTimeoutChange}
            onBlur={handleRunTimeoutBlur}
            onInput={handleRunTimeoutInput}
            stringMode
            min={0}
            precision={0}
            step={100}
            style={{ width: '100%' }}
            data-testid="run-timeout-ms"
          />
        </Form.Item>

        {value.type === 'daemon' && (
          <>
            <Form.Item label={t('projects.schedule.restart_delay')}>
              <InputNumber
                value={value.restartDelayMs ?? 3000}
                onChange={(v) => onChange({ ...value, restartDelayMs: v ?? 3000 })}
                min={0}
                style={{ width: '100%' }}
                data-testid="restart-delay"
              />
            </Form.Item>
            <Form.Item label={t('projects.schedule.max_failures')}>
              <InputNumber
                value={value.maxConsecutiveFailures ?? 5}
                onChange={(v) => onChange({ ...value, maxConsecutiveFailures: v ?? 5 })}
                min={0}
                style={{ width: '100%' }}
                data-testid="max-failures"
              />
            </Form.Item>
          </>
        )}

        {value.type === 'fixed_rate' && (
          <>
            <Form.Item label={t('projects.schedule.interval')}>
              <InputNumber
                value={value.intervalMs ?? 60000}
                onChange={(v) => onChange({ ...value, intervalMs: v ?? 60000 })}
                min={1000}
                style={{ width: '100%' }}
                data-testid="interval-ms"
              />
            </Form.Item>
            <Form.Item label={t('projects.schedule.max_concurrent')}>
              <InputNumber
                value={value.maxConcurrent ?? 1}
                onChange={(v) => onChange({ ...value, maxConcurrent: v ?? 1 })}
                min={1}
                style={{ width: '100%' }}
                data-testid="max-concurrent"
              />
            </Form.Item>
          </>
        )}
      </Form>
    </div>
  );
};
