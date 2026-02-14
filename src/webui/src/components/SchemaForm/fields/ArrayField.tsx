import React from 'react';
import { Button, Form, Space } from 'antd';
import { PlusOutlined, DeleteOutlined } from '@ant-design/icons';
import type { FieldMeta } from '@/types/service';
import { FieldRenderer } from '../FieldRenderer';

interface ArrayFieldProps {
  field: FieldMeta;
  value: unknown[];
  onChange: (value: unknown[]) => void;
  error?: string;
}

export const ArrayField: React.FC<ArrayFieldProps> = ({ field, value, onChange, error }) => {
  const arr = value ?? [];
  const canAdd = field.maxItems === undefined || arr.length < field.maxItems;
  const canRemove = field.minItems === undefined || arr.length > field.minItems;

  const handleAdd = () => {
    onChange([...arr, field.items?.type === 'object' ? {} : '']);
  };

  const handleRemove = (index: number) => {
    onChange(arr.filter((_, i) => i !== index));
  };

  const handleItemChange = (index: number, val: unknown) => {
    const next = [...arr];
    next[index] = val;
    onChange(next);
  };

  return (
    <Form.Item
      label={field.name}
      required={field.required}
      help={error || field.description}
      validateStatus={error ? 'error' : undefined}
      data-testid={`field-${field.name}`}
    >
      {arr.map((item, i) => (
        <Space key={i} align="start" style={{ display: 'flex', marginBottom: 8 }} data-testid={`array-item-${i}`}>
          {field.items ? (
            <FieldRenderer
              field={{ ...field.items, name: `${i}` }}
              value={item}
              onChange={(v) => handleItemChange(i, v)}
            />
          ) : (
            <span>{JSON.stringify(item)}</span>
          )}
          <Button
            type="text"
            danger
            icon={<DeleteOutlined />}
            disabled={!canRemove}
            onClick={() => handleRemove(i)}
            data-testid={`remove-item-${i}`}
          />
        </Space>
      ))}
      <Button
        type="dashed"
        icon={<PlusOutlined />}
        onClick={handleAdd}
        disabled={!canAdd}
        data-testid={`add-item-${field.name}`}
        block
      >
        Add Item
      </Button>
    </Form.Item>
  );
};
