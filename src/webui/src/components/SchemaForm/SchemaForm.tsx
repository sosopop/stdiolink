import React, { useState } from 'react';
import { Form, Collapse } from 'antd';
import type { FieldMeta } from '@/types/service';
import { FieldRenderer } from './FieldRenderer';

interface SchemaFormProps {
  schema: FieldMeta[];
  value: Record<string, unknown>;
  onChange: (value: Record<string, unknown>) => void;
  errors?: Record<string, string>;
}

export const SchemaForm: React.FC<SchemaFormProps> = ({ schema, value, onChange, errors }) => {
  const [showAdvanced, setShowAdvanced] = useState(false);

  const sorted = [...schema].sort((a, b) => (a.ui?.order ?? 999) - (b.ui?.order ?? 999));

  const groups = new Map<string, FieldMeta[]>();
  const ungrouped: FieldMeta[] = [];

  for (const field of sorted) {
    if (field.ui?.group) {
      const list = groups.get(field.ui.group) ?? [];
      list.push(field);
      groups.set(field.ui.group, list);
    } else {
      ungrouped.push(field);
    }
  }

  const basicFields = ungrouped.filter((f) => !f.ui?.advanced);
  const advancedFields = ungrouped.filter((f) => f.ui?.advanced);

  const handleFieldChange = (name: string, fieldValue: unknown) => {
    onChange({ ...value, [name]: fieldValue });
  };

  const renderField = (field: FieldMeta) => (
    <FieldRenderer
      key={field.name}
      field={field}
      value={value[field.name]}
      onChange={(v) => handleFieldChange(field.name, v)}
      error={errors?.[field.name]}
      errors={errors}
    />
  );

  const groupItems = Array.from(groups.entries()).map(([groupName, fields]) => ({
    key: groupName,
    label: groupName,
    children: <div data-testid={`group-${groupName}`}>{fields.map(renderField)}</div>,
  }));

  return (
    <Form layout="vertical" data-testid="schema-form">
      {basicFields.map(renderField)}

      {groupItems.length > 0 && (
        <Collapse items={groupItems} defaultActiveKey={groupItems.map((g) => g.key)} size="small" />
      )}

      {advancedFields.length > 0 && (
        <Collapse
          items={[{
            key: 'advanced',
            label: 'Advanced Options',
            children: <div data-testid="advanced-fields">{advancedFields.map(renderField)}</div>,
          }]}
          activeKey={showAdvanced ? ['advanced'] : []}
          onChange={(keys) => setShowAdvanced(keys.length > 0)}
          size="small"
          style={{ marginTop: 16 }}
        />
      )}
    </Form>
  );
};
