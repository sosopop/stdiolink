import React from 'react';
import { Button, Form, Space } from 'antd';
import { PlusOutlined, DeleteOutlined } from '@ant-design/icons';
import type { FieldMeta } from '@/types/service';
import { FieldRenderer } from '../FieldRenderer';
import { getDefaultItem } from '../utils/fieldDefaults';

interface ArrayFieldProps {
  field: FieldMeta;
  value: unknown[];
  onChange: (value: unknown[]) => void;
  error?: string;
  errors?: Record<string, string>;
  basePath?: string;
}

export const ArrayField: React.FC<ArrayFieldProps> = ({ field, value, onChange, error, errors, basePath }) => {
  const arr = value ?? [];
  const canAdd = field.maxItems === undefined || arr.length < field.maxItems;
  const canRemove = field.minItems === undefined || arr.length > field.minItems;
  const currentPath = basePath ?? field.name;

  /**
   * array<object> 深度初始化：遍历 items.fields，
   * 对每个子字段调用 getDefaultItem 按类型设置默认值，
   * 使 int 字段初始化为 0 而非 ''，避免后续校验类型错误。
   */
  const handleAdd = () => {
    const subFields = field.items?.type === 'object' ? (field.items.fields ?? []) : [];
    if (subFields.length > 0) {
      const newItem: Record<string, unknown> = {};
      for (const subField of subFields) {
        if (subField.default !== undefined) {
          newItem[subField.name] = subField.default;
        } else if (subField.type === 'enum' && subField.enum?.[0] !== undefined) {
          newItem[subField.name] = subField.enum[0];
        } else {
          newItem[subField.name] = getDefaultItem(subField.type);
        }
      }
      onChange([...arr, newItem]);
      return;
    }
    onChange([...arr, getDefaultItem(field.items?.type)]);
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
          <span data-testid={`array-item-label-${i}`}>
            {`${field.items?.name ?? field.name} ${i + 1}`}
          </span>
          {field.items ? (
            <FieldRenderer
              field={{ ...field.items, name: `${field.name}[${i}]` }}
              value={item}
              onChange={(v) => handleItemChange(i, v)}
              error={errors?.[`${currentPath}[${i}]`]}
              errors={errors}
              basePath={`${currentPath}[${i}]`}
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
