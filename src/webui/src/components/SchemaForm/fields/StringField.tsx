import React from 'react';
import { Input, Form } from 'antd';
import type { FieldMeta } from '@/types/service';

interface StringFieldProps {
  field: FieldMeta;
  value: string;
  onChange: (value: string) => void;
  error?: string;
}

export const StringField: React.FC<StringFieldProps> = ({ field, value, onChange, error }) => {
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
      <Input
        value={value ?? ''}
        onChange={(e) => onChange(e.target.value)}
        placeholder={field.ui?.placeholder ?? ''}
        readOnly={field.ui?.readonly}
        maxLength={field.maxLength}
        data-testid={`input-${field.name}`}
      />
    </Form.Item>
  );
};
