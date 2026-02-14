import React from 'react';
import { Input, Form } from 'antd';
import type { FieldMeta } from '@/types/service';

const { TextArea } = Input;

interface AnyFieldProps {
  field: FieldMeta;
  value: unknown;
  onChange: (value: unknown) => void;
  error?: string;
}

export const AnyField: React.FC<AnyFieldProps> = ({ field, value, onChange, error }) => {
  const strValue = typeof value === 'string' ? value : JSON.stringify(value ?? '', null, 2);

  const handleChange = (text: string) => {
    try {
      onChange(JSON.parse(text));
    } catch {
      onChange(text);
    }
  };

  return (
    <Form.Item
      label={field.name}
      required={field.required}
      help={error || field.description}
      validateStatus={error ? 'error' : undefined}
      data-testid={`field-${field.name}`}
    >
      <TextArea
        value={strValue}
        onChange={(e) => handleChange(e.target.value)}
        rows={4}
        placeholder="JSON value"
        data-testid={`input-${field.name}`}
      />
    </Form.Item>
  );
};
