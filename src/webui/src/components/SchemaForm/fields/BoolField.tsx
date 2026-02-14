import React from 'react';
import { Switch, Form } from 'antd';
import type { FieldMeta } from '@/types/service';

interface BoolFieldProps {
  field: FieldMeta;
  value: boolean;
  onChange: (value: boolean) => void;
  error?: string;
}

export const BoolField: React.FC<BoolFieldProps> = ({ field, value, onChange, error }) => {
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
      <Switch
        checked={value ?? false}
        onChange={onChange}
        disabled={field.ui?.readonly}
        data-testid={`input-${field.name}`}
      />
    </Form.Item>
  );
};
