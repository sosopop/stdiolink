import React from 'react';
import { Button, Input, Space } from 'antd';
import { PlusOutlined, DeleteOutlined } from '@ant-design/icons';

interface EnumValuesEditorProps {
  values: string[];
  onChange: (values: string[]) => void;
}

export const EnumValuesEditor: React.FC<EnumValuesEditorProps> = ({ values, onChange }) => {
  const handleAdd = () => onChange([...values, '']);

  const handleChange = (index: number, val: string) => {
    const next = [...values];
    next[index] = val;
    onChange(next);
  };

  const handleRemove = (index: number) => {
    onChange(values.filter((_, i) => i !== index));
  };

  return (
    <div data-testid="enum-values-editor">
      {values.map((v, i) => (
        <Space key={i} style={{ display: 'flex', marginBottom: 8 }} align="center">
          <Input
            value={v}
            onChange={(e) => handleChange(i, e.target.value)}
            placeholder={`Option ${i + 1}`}
            data-testid={`enum-value-${i}`}
            style={{ width: 200 }}
          />
          <Button
            type="text"
            danger
            icon={<DeleteOutlined />}
            onClick={() => handleRemove(i)}
            data-testid={`enum-remove-${i}`}
          />
        </Space>
      ))}
      <Button
        type="dashed"
        icon={<PlusOutlined />}
        onClick={handleAdd}
        data-testid="enum-add-btn"
        block
      >
        Add Option
      </Button>
    </div>
  );
};
