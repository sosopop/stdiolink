import { create } from 'zustand';
import { servicesApi } from '@/api/services';
import type { SchemaNode, ServiceConfigSchema } from '@/utils/schemaPath';
import {
  schemaToNodes, nodesToSchema, schemaToJson, jsonToSchema,
  addFieldToPath, updateFieldByPath, removeFieldByPath, moveFieldInPath,
} from '@/utils/schemaPath';

interface SchemaEditorState {
  nodes: SchemaNode[];
  originalNodes: SchemaNode[];
  activeMode: 'visual' | 'json' | 'preview';
  jsonText: string;
  jsonError: string | null;
  dirty: boolean;
  validationErrors: string[];
  validating: boolean;
  saving: boolean;

  setNodes: (nodes: SchemaNode[]) => void;
  addField: (field: SchemaNode, parentPath?: string) => void;
  updateField: (path: string, field: SchemaNode) => void;
  removeField: (path: string) => void;
  moveField: (path: string, direction: 'up' | 'down') => void;
  setJsonText: (text: string) => void;
  syncFromJson: () => boolean;
  syncToJson: () => void;
  setActiveMode: (mode: 'visual' | 'json' | 'preview') => void;
  validate: (serviceId: string) => Promise<void>;
  save: (serviceId: string) => Promise<void>;
  reset: () => void;
  loadSchema: (serviceId: string) => Promise<void>;
}

export const useSchemaEditorStore = create<SchemaEditorState>()((set, get) => ({
  nodes: [],
  originalNodes: [],
  activeMode: 'visual',
  jsonText: '{}',
  jsonError: null,
  dirty: false,
  validationErrors: [],
  validating: false,
  saving: false,

  setNodes: (nodes) => {
    const json = schemaToJson(nodesToSchema(nodes));
    set({ nodes, jsonText: json, dirty: true, jsonError: null });
  },

  addField: (field, parentPath) => {
    const nodes = parentPath
      ? addFieldToPath(get().nodes, parentPath, field)
      : [...get().nodes, field];
    const json = schemaToJson(nodesToSchema(nodes));
    set({ nodes, jsonText: json, dirty: true, jsonError: null });
  },

  updateField: (path, field) => {
    const nodes = updateFieldByPath(get().nodes, path, () => field);
    const json = schemaToJson(nodesToSchema(nodes));
    set({ nodes, jsonText: json, dirty: true, jsonError: null });
  },

  removeField: (path) => {
    const nodes = removeFieldByPath(get().nodes, path);
    const json = schemaToJson(nodesToSchema(nodes));
    set({ nodes, jsonText: json, dirty: true, jsonError: null });
  },

  moveField: (path, direction) => {
    const nodes = moveFieldInPath(get().nodes, path, direction);
    const json = schemaToJson(nodesToSchema(nodes));
    set({ nodes, jsonText: json, dirty: true, jsonError: null });
  },

  setJsonText: (text) => {
    set({ jsonText: text });
  },

  syncFromJson: () => {
    try {
      const schema = jsonToSchema(get().jsonText);
      const nodes = schemaToNodes(schema);
      set({ nodes, jsonError: null, dirty: true });
      return true;
    } catch (e: any) {
      set({ jsonError: e.message || 'Invalid JSON' });
      return false;
    }
  },

  syncToJson: () => {
    const json = schemaToJson(nodesToSchema(get().nodes));
    set({ jsonText: json, jsonError: null });
  },

  setActiveMode: (mode) => {
    const prev = get().activeMode;
    if (prev === 'json' && mode !== 'json') {
      get().syncFromJson();
    }
    if (prev !== 'json' && mode === 'json') {
      get().syncToJson();
    }
    set({ activeMode: mode });
  },

  validate: async (serviceId) => {
    try {
      set({ validating: true, validationErrors: [] });
      const schema = nodesToSchema(get().nodes);
      const result = await servicesApi.validateSchema(serviceId, schema);
      set({
        validating: false,
        validationErrors: result.valid ? [] : [result.error || 'Validation failed'],
      });
    } catch (e: any) {
      set({ validating: false, validationErrors: [e?.error || 'Validation request failed'] });
    }
  },

  save: async (serviceId) => {
    try {
      set({ saving: true });
      const schema = nodesToSchema(get().nodes);
      const content = JSON.stringify(schema, null, 2);
      await servicesApi.fileWrite(serviceId, 'config.schema.json', content);
      set({ saving: false, dirty: false, originalNodes: structuredClone(get().nodes) });
    } catch (e: any) {
      set({ saving: false });
      throw e;
    }
  },

  reset: () => {
    const original = get().originalNodes;
    const json = schemaToJson(nodesToSchema(original));
    set({ nodes: structuredClone(original), jsonText: json, dirty: false, jsonError: null, validationErrors: [] });
  },

  loadSchema: async (serviceId) => {
    try {
      const detail = await servicesApi.detail(serviceId);
      const schemaObj = (detail.configSchema ?? {}) as ServiceConfigSchema;
      const nodes = schemaToNodes(schemaObj);
      const json = schemaToJson(schemaObj);
      set({ nodes, originalNodes: structuredClone(nodes), jsonText: json, dirty: false, jsonError: null });
    } catch {
      set({ nodes: [], originalNodes: [], jsonText: '{}', dirty: false });
    }
  },
}));
