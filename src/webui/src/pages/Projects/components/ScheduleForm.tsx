import React from 'react';
import { Form, Select, InputNumber } from 'antd';
import type { Schedule, ScheduleType } from '@/types/project';

interface ScheduleFormProps {
  value: Schedule;
  onChange: (value: Schedule) => void;
}

const SCHEDULE_TYPES: { label: string; value: ScheduleType }[] = [
  { label: 'Manual', value: 'manual' },
  { label: 'Daemon', value: 'daemon' },
  { label: 'Fixed Rate', value: 'fixed_rate' },
];

export const ScheduleForm: React.FC<ScheduleFormProps> = ({ value, onChange }) => {
  const handleTypeChange = (type: ScheduleType) => {
    onChange({ ...value, type });
  };

  return (
    <div data-testid="schedule-form">
      <Form layout="vertical">
        <Form.Item label="Schedule Type">
          <Select
            value={value.type}
            onChange={handleTypeChange}
            options={SCHEDULE_TYPES}
            data-testid="schedule-type"
          />
        </Form.Item>

        {value.type === 'daemon' && (
          <>
            <Form.Item label="Restart Delay (ms)">
              <InputNumber
                value={value.restartDelayMs ?? 3000}
                onChange={(v) => onChange({ ...value, restartDelayMs: v ?? 3000 })}
                min={0}
                style={{ width: '100%' }}
                data-testid="restart-delay"
              />
            </Form.Item>
            <Form.Item label="Max Consecutive Failures">
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
            <Form.Item label="Interval (ms)">
              <InputNumber
                value={value.intervalMs ?? 60000}
                onChange={(v) => onChange({ ...value, intervalMs: v ?? 60000 })}
                min={1000}
                style={{ width: '100%' }}
                data-testid="interval-ms"
              />
            </Form.Item>
            <Form.Item label="Max Concurrent">
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
