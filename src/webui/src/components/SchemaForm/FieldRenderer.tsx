import React from 'react';
import type { FieldMeta } from '@/types/service';
import { StringField } from './fields/StringField';
import { NumberField } from './fields/NumberField';
import { BoolField } from './fields/BoolField';
import { EnumField } from './fields/EnumField';
import { ObjectField } from './fields/ObjectField';
import { ArrayField } from './fields/ArrayField';
import { AnyField } from './fields/AnyField';

interface FieldRendererProps {
  field: FieldMeta;
  value: unknown;
  onChange: (value: unknown) => void;
  error?: string;
  errors?: Record<string, string>;
}

export const FieldRenderer: React.FC<FieldRendererProps> = ({ field, value, onChange, error, errors }) => {
  if (field.ui?.readonly) {
    // Still render the field but in readonly mode
  }

  switch (field.type) {
    case 'string':
      return <StringField field={field} value={value as string} onChange={onChange} error={error} />;
    case 'int':
    case 'int64':
    case 'double':
      return <NumberField field={field} value={value as number | undefined} onChange={onChange} error={error} />;
    case 'bool':
      return <BoolField field={field} value={value as boolean} onChange={onChange} error={error} />;
    case 'enum':
      return <EnumField field={field} value={value} onChange={onChange} error={error} />;
    case 'object':
      return (
        <ObjectField
          field={field}
          value={(value as Record<string, unknown>) ?? {}}
          onChange={onChange as (v: Record<string, unknown>) => void}
          errors={errors}
        />
      );
    case 'array':
      return (
        <ArrayField
          field={field}
          value={(value as unknown[]) ?? []}
          onChange={onChange as (v: unknown[]) => void}
          error={error}
        />
      );
    default:
      return <AnyField field={field} value={value} onChange={onChange} error={error} />;
  }
};
