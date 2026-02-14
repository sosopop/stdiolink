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
      label={field.name}
      required={field.required}
      help={error || field.description}
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
