import React from 'react';
import { InputNumber, Input, Typography } from 'antd';
import { EnumValuesEditor } from './EnumValuesEditor';

const { Text } = Typography;

interface ConstraintsSectionProps {
  type: string;
  constraints: Record<string, unknown>;
  onChange: (constraints: Record<string, unknown>) => void;
}

export const ConstraintsSection: React.FC<ConstraintsSectionProps> = ({ type, constraints, onChange }) => {
  const update = (key: string, value: unknown) => {
    onChange({ ...constraints, [key]: value });
  };

  const numField = (label: string, key: string, step?: number) => (
    <div style={{ marginBottom: 8 }} key={key}>
      <Text type="secondary" style={{ display: 'block', marginBottom: 4 }}>{label}</Text>
      <InputNumber
        value={constraints[key] as number | undefined}
        onChange={(v) => update(key, v)}
        data-testid={`constraint-${key}`}
        step={step}
        style={{ width: '100%' }}
      />
    </div>
  );

  switch (type) {
    case 'string':
      return (
        <div data-testid="constraints-string">
          {numField('Min Length', 'minLength')}
          {numField('Max Length', 'maxLength')}
          <div style={{ marginBottom: 8 }}>
            <Text type="secondary" style={{ display: 'block', marginBottom: 4 }}>Pattern</Text>
            <Input
              value={(constraints.pattern as string) ?? ''}
              onChange={(e) => update('pattern', e.target.value || undefined)}
              placeholder="Regular expression"
              data-testid="constraint-pattern"
            />
          </div>
        </div>
      );

    case 'int':
    case 'int64':
      return (
        <div data-testid="constraints-int">
          {numField('Min', 'min')}
          {numField('Max', 'max')}
        </div>
      );

    case 'double':
      return (
        <div data-testid="constraints-double">
          {numField('Min', 'min', 0.1)}
          {numField('Max', 'max', 0.1)}
        </div>
      );

    case 'array':
      return (
        <div data-testid="constraints-array">
          {numField('Min Items', 'minItems')}
          {numField('Max Items', 'maxItems')}
        </div>
      );

    case 'enum':
      return (
        <div data-testid="constraints-enum">
          <Text type="secondary" style={{ display: 'block', marginBottom: 4 }}>Enum Values</Text>
          <EnumValuesEditor
            values={(constraints.enumValues as string[]) ?? []}
            onChange={(vals) => update('enumValues', vals)}
          />
        </div>
      );

    default:
      return (
        <div data-testid="constraints-none">
          <Text type="secondary">No constraints available for this type.</Text>
        </div>
      );
  }
};
