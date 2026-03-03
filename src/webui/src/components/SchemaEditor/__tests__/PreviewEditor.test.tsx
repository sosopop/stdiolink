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

  it('ignores children for non-object array items', () => {
    const nodes: SchemaNode[] = [
      {
        name: 'tags',
        descriptor: {
          type: 'array',
          items: { type: 'string' },
        },
        children: [
          { name: 'fake', descriptor: { type: 'string' } },
        ],
      },
    ];

    const result = nodesToFieldMeta(nodes);
    expect(result).toHaveLength(1);
    expect(result[0].items?.type).toBe('string');
    expect(result[0].items?.fields).toBeUndefined();
  });

  // R09
  it('passes through ui hints from descriptor to field meta', () => {
    const nodes: SchemaNode[] = [{
      name: 'timeout',
      descriptor: {
        type: 'int',
        ui: { widget: 'slider', unit: 'ms', readonly: true },
      },
    }];
    const result = nodesToFieldMeta(nodes);
    expect(result[0].ui?.widget).toBe('slider');
    expect(result[0].ui?.unit).toBe('ms');
    expect(result[0].ui?.readonly).toBe(true);
  });

  // R10
  it('passes through requiredKeys and additionalProperties', () => {
    const nodes: SchemaNode[] = [{
      name: 'server',
      descriptor: {
        type: 'object',
        requiredKeys: ['host'],
        additionalProperties: false,
      },
    }];
    const result = nodesToFieldMeta(nodes);
    expect(result[0].requiredKeys).toEqual(['host']);
    expect(result[0].additionalProperties).toBe(false);
  });

  // R17
  it('keeps ui undefined when descriptor has no ui', () => {
    const nodes: SchemaNode[] = [{
      name: 'host',
      descriptor: { type: 'string' },
    }];
    const result = nodesToFieldMeta(nodes);
    expect(result[0].ui).toBeUndefined();
  });
});
