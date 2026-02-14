import React, { useMemo } from 'react';
import { Empty } from 'antd';
import { SchemaForm } from '@/components/SchemaForm/SchemaForm';
import { useSchemaEditorStore } from '@/stores/useSchemaEditorStore';
import type { FieldMeta } from '@/types/service';
import type { SchemaNode } from '@/utils/schemaPath';

function nodesToFieldMeta(nodes: SchemaNode[]): FieldMeta[] {
  return nodes.map((n) => {
    const meta: FieldMeta = {
      name: n.name,
      type: n.descriptor.type ?? 'any',
      required: n.descriptor.required,
      description: n.descriptor.description,
      default: n.descriptor.default,
      min: n.descriptor.constraints?.min,
      max: n.descriptor.constraints?.max,
      minLength: n.descriptor.constraints?.minLength,
      maxLength: n.descriptor.constraints?.maxLength,
      pattern: n.descriptor.constraints?.pattern,
      enum: n.descriptor.constraints?.enumValues,
      format: n.descriptor.constraints?.format,
      minItems: n.descriptor.constraints?.minItems,
      maxItems: n.descriptor.constraints?.maxItems,
    };
    if (n.children && n.children.length > 0) {
      meta.fields = nodesToFieldMeta(n.children);
    }
    if (n.descriptor.items) {
      meta.items = {
        name: 'items',
        type: n.descriptor.items.type ?? 'any',
        description: n.descriptor.items.description,
      };
    }
    return meta;
  });
}

export const PreviewEditor: React.FC = () => {
  const { nodes } = useSchemaEditorStore();

  const fields = useMemo(() => nodesToFieldMeta(nodes), [nodes]);

  if (fields.length === 0) {
    return <Empty description="No fields to preview" data-testid="preview-empty" />;
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
