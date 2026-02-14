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
      label={field.name}
      required={field.required}
      help={error || field.description}
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
