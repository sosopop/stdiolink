import React from 'react';
import { useTranslation } from 'react-i18next';
import { Form, Select, InputNumber } from 'antd';
import type { Schedule, ScheduleType } from '@/types/project';

interface ScheduleFormProps {
  value: Schedule;
  onChange: (value: Schedule) => void;
}

export const ScheduleForm: React.FC<ScheduleFormProps> = ({ value, onChange }) => {
  const { t } = useTranslation();

  const scheduleTypes: { label: string; value: ScheduleType }[] = [
    { label: t('projects.schedule.manual'), value: 'manual' },
    { label: t('projects.schedule.daemon'), value: 'daemon' },
    { label: t('projects.schedule.fixed_rate'), value: 'fixed_rate' },
  ];

  const handleTypeChange = (type: ScheduleType) => {
    onChange({ ...value, type });
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
