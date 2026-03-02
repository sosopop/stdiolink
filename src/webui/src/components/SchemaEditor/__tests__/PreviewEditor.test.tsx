import { describe, it, expect } from 'vitest';
import type { SchemaNode } from '@/utils/schemaPath';
import { nodesToFieldMeta } from '../PreviewEditor';

describe('PreviewEditor nodesToFieldMeta (M90)', () => {
  it('R_FE_03 converts array<object> items.fields recursively', () => {
    const nodes: SchemaNode[] = [
      {
        name: 'radars',
        descriptor: {
          type: 'array',
          items: {
            type: 'object',
            fields: {
              id: { type: 'string', required: true },
              port: { type: 'int', required: true, constraints: { min: 1, max: 65535 } },
            },
          },
        },
      },
    ];

    const result = nodesToFieldMeta(nodes);
    expect(result).toHaveLength(1);
    expect(result[0].items?.fields).toHaveLength(2);
    expect(result[0].items?.fields?.[0]?.name).toBe('id');
    expect(result[0].items?.fields?.[1]?.name).toBe('port');
  });

  it('R_FE_04 keeps items.fields undefined when descriptor.items.fields is absent', () => {
    const nodes: SchemaNode[] = [
      {
        name: 'tags',
        descriptor: {
          type: 'array',
          items: { type: 'string' },
        },
      },
    ];

    const result = nodesToFieldMeta(nodes);
    expect(result).toHaveLength(1);
    expect(result[0].items?.fields).toBeUndefined();
  });
});
