import { describe, it, expect } from 'vitest';
import {
  schemaToNodes,
  nodesToSchema,
  schemaToJson,
  jsonToSchema,
  getFieldByPath,
  updateFieldByPath,
  removeFieldByPath,
  addFieldToPath,
  moveFieldInPath,
} from '../schemaPath';
import type { SchemaNode, ServiceConfigSchema } from '../schemaPath';

const sampleSchema: ServiceConfigSchema = {
  host: { type: 'string', required: true, description: 'Host address' },
  port: { type: 'int', default: 3306, constraints: { min: 1, max: 65535 } },
  options: {
    type: 'object',
    fields: {
      timeout: { type: 'int', default: 30 },
      retries: { type: 'int', default: 3 },
    },
  },
};

describe('schemaPath', () => {
  describe('schemaToNodes / nodesToSchema', () => {
    it('converts schema to nodes and back', () => {
      const nodes = schemaToNodes(sampleSchema);
      expect(nodes).toHaveLength(3);
      expect(nodes[0].name).toBe('host');
      expect(nodes[2].name).toBe('options');
      expect(nodes[2].children).toHaveLength(2);

      const back = nodesToSchema(nodes);
      expect(back).toEqual(sampleSchema);
    });
  });

  describe('schemaToJson / jsonToSchema', () => {
    it('serializes schema to JSON', () => {
      const json = schemaToJson(sampleSchema);
      expect(JSON.parse(json)).toEqual(sampleSchema);
    });

    it('deserializes valid JSON', () => {
      const json = JSON.stringify(sampleSchema);
      const schema = jsonToSchema(json);
      expect(schema).toEqual(sampleSchema);
    });

    it('throws on invalid JSON', () => {
      expect(() => jsonToSchema('not json')).toThrow();
    });

    it('throws on non-object JSON', () => {
      expect(() => jsonToSchema('[1,2,3]')).toThrow('Schema must be a JSON object');
    });
  });

  describe('getFieldByPath', () => {
    const nodes = schemaToNodes(sampleSchema);

    it('returns top-level field', () => {
      const field = getFieldByPath(nodes, 'host');
      expect(field).not.toBeNull();
      expect(field!.name).toBe('host');
    });

    it('returns nested field', () => {
      const field = getFieldByPath(nodes, 'options.timeout');
      expect(field).not.toBeNull();
      expect(field!.name).toBe('timeout');
    });

    it('returns null for non-existent path', () => {
      expect(getFieldByPath(nodes, 'nonexistent')).toBeNull();
    });

    it('returns null for non-existent nested path', () => {
      expect(getFieldByPath(nodes, 'options.missing')).toBeNull();
    });
  });

  describe('updateFieldByPath', () => {
    it('updates top-level field', () => {
      const nodes = schemaToNodes(sampleSchema);
      const updated = updateFieldByPath(nodes, 'host', (f) => ({
        ...f,
        descriptor: { ...f.descriptor, description: 'Updated' },
      }));
      expect(updated[0].descriptor.description).toBe('Updated');
      expect(updated[1].name).toBe('port'); // others unchanged
    });

    it('updates nested field', () => {
      const nodes = schemaToNodes(sampleSchema);
      const updated = updateFieldByPath(nodes, 'options.timeout', (f) => ({
        ...f,
        descriptor: { ...f.descriptor, default: 60 },
      }));
      expect(updated[2].children![0].descriptor.default).toBe(60);
    });
  });

  describe('removeFieldByPath', () => {
    it('removes top-level field', () => {
      const nodes = schemaToNodes(sampleSchema);
      const result = removeFieldByPath(nodes, 'port');
      expect(result).toHaveLength(2);
      expect(result.find((n) => n.name === 'port')).toBeUndefined();
    });

    it('removes nested field', () => {
      const nodes = schemaToNodes(sampleSchema);
      const result = removeFieldByPath(nodes, 'options.retries');
      expect(result[2].children).toHaveLength(1);
      expect(result[2].children![0].name).toBe('timeout');
    });
  });

  describe('addFieldToPath', () => {
    it('adds to root when parentPath is empty', () => {
      const nodes = schemaToNodes(sampleSchema);
      const newField: SchemaNode = { name: 'newField', descriptor: { type: 'bool' } };
      const result = addFieldToPath(nodes, '', newField);
      expect(result).toHaveLength(4);
      expect(result[3].name).toBe('newField');
    });

    it('adds child to object field', () => {
      const nodes = schemaToNodes(sampleSchema);
      const newField: SchemaNode = { name: 'debug', descriptor: { type: 'bool' } };
      const result = addFieldToPath(nodes, 'options', newField);
      expect(result[2].children).toHaveLength(3);
      expect(result[2].children![2].name).toBe('debug');
    });
  });

  describe('moveFieldInPath', () => {
    it('moves field up', () => {
      const nodes = schemaToNodes(sampleSchema);
      const result = moveFieldInPath(nodes, 'port', 'up');
      expect(result[0].name).toBe('port');
      expect(result[1].name).toBe('host');
    });

    it('moves field down', () => {
      const nodes = schemaToNodes(sampleSchema);
      const result = moveFieldInPath(nodes, 'host', 'down');
      expect(result[0].name).toBe('port');
      expect(result[1].name).toBe('host');
    });

    it('does not move first field up', () => {
      const nodes = schemaToNodes(sampleSchema);
      const result = moveFieldInPath(nodes, 'host', 'up');
      expect(result[0].name).toBe('host');
    });

    it('does not move last field down', () => {
      const nodes = schemaToNodes(sampleSchema);
      const result = moveFieldInPath(nodes, 'options', 'down');
      expect(result[2].name).toBe('options');
    });

    it('moves nested field', () => {
      const nodes = schemaToNodes(sampleSchema);
      const result = moveFieldInPath(nodes, 'options.retries', 'up');
      expect(result[2].children![0].name).toBe('retries');
      expect(result[2].children![1].name).toBe('timeout');
    });
  });
});
