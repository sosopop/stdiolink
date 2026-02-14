import React from 'react';
import { Collapse, Form } from 'antd';
import type { FieldMeta } from '@/types/service';
import { FieldRenderer } from '../FieldRenderer';

interface ObjectFieldProps {
  field: FieldMeta;
  value: Record<string, unknown>;
  onChange: (value: Record<string, unknown>) => void;
  errors?: Record<string, string>;
}

export const ObjectField: React.FC<ObjectFieldProps> = ({ field, value, onChange, errors }) => {
  const val = value ?? {};

  const handleFieldChange = (name: string, fieldValue: unknown) => {
    onChange({ ...val, [name]: fieldValue });
  };

  const items = [
    {
      key: field.name,
      label: field.name,
      children: (
        <div data-testid={`object-fields-${field.name}`}>
          {(field.fields ?? []).map((child) => (
            <FieldRenderer
              key={child.name}
              field={child}
              value={val[child.name]}
              onChange={(v) => handleFieldChange(child.name, v)}
              error={errors?.[`${field.name}.${child.name}`]}
            />
          ))}
        </div>
      ),
    },
  ];

  return (
    <Form.Item data-testid={`field-${field.name}`}>
      <Collapse items={items} defaultActiveKey={[field.name]} size="small" />
    </Form.Item>
  );
};
