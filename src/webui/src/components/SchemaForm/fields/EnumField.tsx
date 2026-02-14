import React from 'react';
import { Select, Form } from 'antd';
import type { FieldMeta } from '@/types/service';

interface EnumFieldProps {
  field: FieldMeta;
  value: unknown;
  onChange: (value: unknown) => void;
  error?: string;
}

export const EnumField: React.FC<EnumFieldProps> = ({ field, value, onChange, error }) => {
  const options = (field.enum ?? []).map((v) => ({
    label: String(v),
    value: v as string,
  }));

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
      <Select
        value={value as string}
        onChange={onChange}
        options={options}
        placeholder={field.ui?.placeholder ?? 'Select...'}
        disabled={field.ui?.readonly}
        data-testid={`input-${field.name}`}
      />
    </Form.Item>
  );
};
