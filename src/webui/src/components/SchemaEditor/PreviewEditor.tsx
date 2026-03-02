import React, { useMemo } from 'react';
import { useTranslation } from 'react-i18next';
import { Empty } from 'antd';
import { SchemaForm } from '@/components/SchemaForm/SchemaForm';
import { useSchemaEditorStore } from '@/stores/useSchemaEditorStore';
import type { FieldMeta } from '@/types/service';
import type { SchemaFieldDescriptor, SchemaNode } from '@/utils/schemaPath';

function descriptorToFieldMeta(name: string, descriptor: SchemaFieldDescriptor): FieldMeta {
  const meta: FieldMeta = {
    name,
    type: descriptor.type ?? 'any',
    required: descriptor.required,
    description: descriptor.description,
    default: descriptor.default,
    min: descriptor.constraints?.min,
    max: descriptor.constraints?.max,
    minLength: descriptor.constraints?.minLength,
    maxLength: descriptor.constraints?.maxLength,
    pattern: descriptor.constraints?.pattern,
    enum: descriptor.constraints?.enumValues,
    format: descriptor.constraints?.format,
    minItems: descriptor.constraints?.minItems,
    maxItems: descriptor.constraints?.maxItems,
  };

  if (descriptor.fields) {
    meta.fields = Object.entries(descriptor.fields).map(([fieldName, fieldDesc]) =>
      descriptorToFieldMeta(fieldName, fieldDesc));
  }

  if (descriptor.items) {
    meta.items = descriptorToFieldMeta(name, descriptor.items);
  }

  return meta;
}

export function nodesToFieldMeta(nodes: SchemaNode[]): FieldMeta[] {
  return nodes.map((n) => {
    const meta = descriptorToFieldMeta(n.name, n.descriptor);
    if (n.children && n.children.length > 0) {
      meta.fields = nodesToFieldMeta(n.children);
    }
    return meta;
  });
}

export const PreviewEditor: React.FC = () => {
  const { t } = useTranslation();
  const { nodes } = useSchemaEditorStore();

  const fields = useMemo(() => nodesToFieldMeta(nodes), [nodes]);

  if (fields.length === 0) {
    return <Empty description={t('schema.no_preview')} data-testid="preview-empty" />;
  }

  return (
    <div data-testid="preview-editor">
      <SchemaForm
        schema={fields}
        value={{}}
        onChange={() => {}}
      />
    </div>
  );
};
