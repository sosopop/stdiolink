import type { FieldMeta, UIHint } from './service';

export interface DriverListItem {
  id: string;
  program: string;
  metaHash: string;
  name?: string;
  version?: string;
}

export interface DriverDetail {
  id: string;
  program: string;
  metaHash: string;
  meta: DriverMeta | null;
}

export type DriverInfo = DriverListItem;

export interface DriverMetaInfo {
  id?: string;
  name: string;
  version: string;
  description?: string;
  vendor?: string;
  entry?: Record<string, unknown>;
  capabilities?: string[];
  profiles?: string[];
}

export interface DriverMeta {
  schemaVersion: string;
  info: DriverMetaInfo;
  config?: ConfigSchema;
  commands: CommandMeta[];
  types?: Record<string, FieldMeta>;
  errors?: Record<string, unknown>[];
  examples?: Record<string, unknown>[];
}

export interface ConfigSchema {
  fields: FieldMeta[];
  apply?: ConfigApply;
}

export interface ConfigApply {
  method?: 'startupArgs' | 'env' | 'command' | 'file';
  envPrefix?: string;
  command?: string;
  fileName?: string;
}

export interface CommandMeta {
  name: string;
  description?: string;
  title?: string;
  summary?: string;
  params: FieldMeta[];
  returns: ReturnMeta;
  events?: EventMeta[];
  errors?: Record<string, unknown>[];
  examples?: Record<string, unknown>[];
  ui?: UIHint;
}

export interface ReturnMeta {
  type: string;
  description?: string;
  fields?: FieldMeta[];
}

export interface EventMeta {
  name: string;
  description?: string;
  fields?: FieldMeta[];
}
