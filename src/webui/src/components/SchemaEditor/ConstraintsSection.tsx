import React from 'react';
import { useTranslation } from 'react-i18next';
import { InputNumber, Input, Typography } from 'antd';
import { EnumValuesEditor } from './EnumValuesEditor';

const { Text } = Typography;

interface ConstraintsSectionProps {
  type: string;
  constraints: Record<string, unknown>;
  onChange: (constraints: Record<string, unknown>) => void;
}

export const ConstraintsSection: React.FC<ConstraintsSectionProps> = ({ type, constraints, onChange }) => {
  const { t } = useTranslation();
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
          {numField(t('schema.min_length'), 'minLength')}
          {numField(t('schema.max_length'), 'maxLength')}
          <div style={{ marginBottom: 8 }}>
            <Text type="secondary" style={{ display: 'block', marginBottom: 4 }}>{t('schema.pattern')}</Text>
            <Input
              value={(constraints.pattern as string) ?? ''}
              onChange={(e) => update('pattern', e.target.value || undefined)}
              placeholder={t('schema.pattern_placeholder')}
              data-testid="constraint-pattern"
            />
          </div>
        </div>
      );

    case 'int':
    case 'int64':
      return (
        <div data-testid="constraints-int">
          {numField(t('schema.min'), 'min')}
          {numField(t('schema.max'), 'max')}
        </div>
      );

    case 'double':
      return (
        <div data-testid="constraints-double">
          {numField(t('schema.min'), 'min', 0.1)}
          {numField(t('schema.max'), 'max', 0.1)}
        </div>
      );

    case 'array':
      return (
        <div data-testid="constraints-array">
          {numField(t('schema.min_items'), 'minItems')}
          {numField(t('schema.max_items'), 'maxItems')}
        </div>
      );

    case 'enum':
      return (
        <div data-testid="constraints-enum">
          <Text type="secondary" style={{ display: 'block', marginBottom: 4 }}>{t('schema.enum_values')}</Text>
          <EnumValuesEditor
            values={(constraints.enumValues as string[]) ?? []}
            onChange={(vals) => update('enumValues', vals)}
          />
        </div>
      );

    default:
      return (
        <div data-testid="constraints-none">
          <Text type="secondary">{t('schema.no_constraints')}</Text>
        </div>
      );
  }
};
