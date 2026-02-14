export interface ServiceInfo {
  id: string;
  name: string;
  version: string;
  serviceDir: string;
  hasSchema: boolean;
  projectCount: number;
}

export interface ServiceDetail extends ServiceInfo {
  manifest: ServiceManifest;
  configSchema: Record<string, unknown>;
  configSchemaFields: FieldMeta[];
  projects: string[];
}

export interface ServiceManifest {
  manifestVersion: string;
  id: string;
  name: string;
  version: string;
  description?: string;
  author?: string;
}

export interface ServiceFile {
  name: string;
  path: string;
  size: number;
  type: string;
  modifiedAt: string;
}

export interface CreateServiceRequest {
  id: string;
  name: string;
  version: string;
  description?: string;
  author?: string;
  template?: 'empty' | 'basic' | 'driver_demo';
  indexJs?: string;
  configSchema?: Record<string, unknown>;
}

export interface FieldMeta {
  name: string;
  type: FieldType;
  description?: string;
  required?: boolean;
  default?: unknown;
  min?: number;
  max?: number;
  minLength?: number;
  maxLength?: number;
  pattern?: string;
  enum?: unknown[];
  format?: string;
  minItems?: number;
  maxItems?: number;
  ui?: UIHint;
  fields?: FieldMeta[];
  items?: FieldMeta;
  requiredKeys?: string[];
  additionalProperties?: boolean;
}

export type FieldType =
  | 'string' | 'int' | 'int64' | 'double'
  | 'bool' | 'object' | 'array' | 'enum' | 'any';

export interface UIHint {
  widget?: string;
  group?: string;
  order?: number;
  placeholder?: string;
  advanced?: boolean;
  readonly?: boolean;
  visibleIf?: string;
  unit?: string;
  step?: number;
}
