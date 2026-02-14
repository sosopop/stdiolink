export type ServiceConfigSchema = Record<string, SchemaFieldDescriptor>;

export interface SchemaFieldDescriptor {
  type?: 'string' | 'int' | 'int64' | 'double' | 'bool' | 'object' | 'array' | 'enum' | 'any';
  required?: boolean;
  description?: string;
  default?: unknown;
  constraints?: {
    min?: number;
    max?: number;
    minLength?: number;
    maxLength?: number;
    pattern?: string;
    enumValues?: unknown[];
    format?: string;
    minItems?: number;
    maxItems?: number;
  };
  fields?: Record<string, SchemaFieldDescriptor>;
  items?: SchemaFieldDescriptor;
}

export interface SchemaNode {
  name: string;
  descriptor: SchemaFieldDescriptor;
  children?: SchemaNode[];
}

export function schemaToNodes(schema: ServiceConfigSchema): SchemaNode[] {
  return Object.entries(schema).map(([name, desc]) => {
    const node: SchemaNode = { name, descriptor: desc };
    if (desc.fields) {
      node.children = schemaToNodes(desc.fields);
    }
    return node;
  });
}

export function nodesToSchema(nodes: SchemaNode[]): ServiceConfigSchema {
  const schema: ServiceConfigSchema = {};
  for (const node of nodes) {
    const desc = { ...node.descriptor };
    if (node.children && node.children.length > 0) {
      desc.fields = nodesToSchema(node.children);
    }
    schema[node.name] = desc;
  }
  return schema;
}

export function schemaToJson(schema: ServiceConfigSchema): string {
  return JSON.stringify(schema, null, 2);
}

export function jsonToSchema(json: string): ServiceConfigSchema {
  const parsed = JSON.parse(json);
  if (typeof parsed !== 'object' || parsed === null || Array.isArray(parsed)) {
    throw new Error('Schema must be a JSON object');
  }
  return parsed as ServiceConfigSchema;
}

export function getFieldByPath(nodes: SchemaNode[], path: string): SchemaNode | null {
  const parts = path.split('.');
  let current = nodes;
  for (let i = 0; i < parts.length; i++) {
    const found = current.find((n) => n.name === parts[i]);
    if (!found) return null;
    if (i === parts.length - 1) return found;
    current = found.children ?? [];
  }
  return null;
}

export function updateFieldByPath(
  nodes: SchemaNode[],
  path: string,
  updater: (f: SchemaNode) => SchemaNode,
): SchemaNode[] {
  const parts = path.split('.');
  if (parts.length === 1) {
    return nodes.map((n) => (n.name === parts[0] ? updater(n) : n));
  }
  return nodes.map((n) => {
    if (n.name === parts[0]) {
      return {
        ...n,
        children: updateFieldByPath(n.children ?? [], parts.slice(1).join('.'), updater),
      };
    }
    return n;
  });
}

export function removeFieldByPath(nodes: SchemaNode[], path: string): SchemaNode[] {
  const parts = path.split('.');
  if (parts.length === 1) {
    return nodes.filter((n) => n.name !== parts[0]);
  }
  return nodes.map((n) => {
    if (n.name === parts[0]) {
      return {
        ...n,
        children: removeFieldByPath(n.children ?? [], parts.slice(1).join('.')),
      };
    }
    return n;
  });
}

export function addFieldToPath(
  nodes: SchemaNode[],
  parentPath: string,
  field: SchemaNode,
): SchemaNode[] {
  if (!parentPath) {
    return [...nodes, field];
  }
  const parts = parentPath.split('.');
  return nodes.map((n) => {
    if (n.name === parts[0]) {
      if (parts.length === 1) {
        return { ...n, children: [...(n.children ?? []), field] };
      }
      return {
        ...n,
        children: addFieldToPath(n.children ?? [], parts.slice(1).join('.'), field),
      };
    }
    return n;
  });
}

export function moveFieldInPath(
  nodes: SchemaNode[],
  path: string,
  direction: 'up' | 'down',
): SchemaNode[] {
  const parts = path.split('.');
  if (parts.length === 1) {
    const idx = nodes.findIndex((n) => n.name === parts[0]);
    if (idx < 0) return nodes;
    const newIdx = direction === 'up' ? idx - 1 : idx + 1;
    if (newIdx < 0 || newIdx >= nodes.length) return nodes;
    const result = [...nodes];
    const current = result[idx];
    const target = result[newIdx];
    if (!current || !target) return nodes;
    result[idx] = target;
    result[newIdx] = current;
    return result;
  }
  return nodes.map((n) => {
    if (n.name === parts[0]) {
      return {
        ...n,
        children: moveFieldInPath(n.children ?? [], parts.slice(1).join('.'), direction),
      };
    }
    return n;
  });
}
