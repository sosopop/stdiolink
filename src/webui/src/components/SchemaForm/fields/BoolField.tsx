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
      label={field.name}
      required={field.required}
      help={error || field.description}
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
