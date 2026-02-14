import React from 'react';
import { InputNumber, Form } from 'antd';
import type { FieldMeta } from '@/types/service';

interface NumberFieldProps {
  field: FieldMeta;
  value: number | undefined;
  onChange: (value: number | null) => void;
  error?: string;
}

export const NumberField: React.FC<NumberFieldProps> = ({ field, value, onChange, error }) => {
  return (
    <Form.Item
      label={
        <span>
          {field.name}
          {field.description && (
            <span style={{ color: 'var(--text-tertiary)', marginLeft: 8, fontWeight: 'normal', fontSize: 12 }}>
              ({field.description})
            </span>
          )}
        </span>
      }
      required={field.required}
      help={error}
      validateStatus={error ? 'error' : undefined}
      data-testid={`field-${field.name}`}
    >
      <InputNumber
        value={value}
        onChange={(v) => onChange(v)}
        min={field.min}
        max={field.max}
        step={field.ui?.step ?? 1}
        placeholder={field.ui?.placeholder}
        readOnly={field.ui?.readonly}
        style={{ width: '100%' }}
        addonAfter={field.ui?.unit}
        data-testid={`input-${field.name}`}
      />
    </Form.Item>
  );
};
