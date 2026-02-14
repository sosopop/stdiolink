import { describe, it, expect, vi, beforeEach } from 'vitest';

vi.mock('@/api/services', () => ({
  servicesApi: {
    detail: vi.fn(),
    fileWrite: vi.fn(),
    validateSchema: vi.fn(),
  },
}));

import { useSchemaEditorStore } from '../useSchemaEditorStore';
import { servicesApi } from '@/api/services';

describe('useSchemaEditorStore', () => {
  beforeEach(() => {
    vi.clearAllMocks();
    useSchemaEditorStore.setState({
      nodes: [],
      originalNodes: [],
      activeMode: 'visual',
      jsonText: '{}',
      jsonError: null,
      dirty: false,
      validationErrors: [],
      validating: false,
      saving: false,
    });
  });

  it('loadSchema fetches and populates nodes', async () => {
    vi.mocked(servicesApi.detail).mockResolvedValue({
      configSchema: {
        host: { type: 'string', required: true },
        port: { type: 'int', default: 3306 },
      },
    } as any);

    await useSchemaEditorStore.getState().loadSchema('svc1');
    const state = useSchemaEditorStore.getState();
    expect(state.nodes).toHaveLength(2);
    expect(state.nodes[0].name).toBe('host');
    expect(state.dirty).toBe(false);
  });

  it('addField adds top-level field', () => {
    useSchemaEditorStore.getState().addField({ name: 'host', descriptor: { type: 'string' } });
    const state = useSchemaEditorStore.getState();
    expect(state.nodes).toHaveLength(1);
    expect(state.dirty).toBe(true);
  });

  it('addField adds nested field', () => {
    useSchemaEditorStore.setState({
      nodes: [{ name: 'opts', descriptor: { type: 'object' }, children: [] }],
    });
    useSchemaEditorStore.getState().addField(
      { name: 'timeout', descriptor: { type: 'int' } },
      'opts',
    );
    const state = useSchemaEditorStore.getState();
    expect(state.nodes[0].children).toHaveLength(1);
  });

  it('updateField updates field at path', () => {
    useSchemaEditorStore.setState({
      nodes: [{ name: 'host', descriptor: { type: 'string', description: 'old' } }],
    });
    useSchemaEditorStore.getState().updateField('host', {
      name: 'host',
      descriptor: { type: 'string', description: 'new' },
    });
    expect(useSchemaEditorStore.getState().nodes[0].descriptor.description).toBe('new');
  });

  it('removeField removes field at path', () => {
    useSchemaEditorStore.setState({
      nodes: [
        { name: 'a', descriptor: { type: 'string' } },
        { name: 'b', descriptor: { type: 'int' } },
      ],
    });
    useSchemaEditorStore.getState().removeField('a');
    expect(useSchemaEditorStore.getState().nodes).toHaveLength(1);
    expect(useSchemaEditorStore.getState().nodes[0].name).toBe('b');
  });

  it('moveField up', () => {
    useSchemaEditorStore.setState({
      nodes: [
        { name: 'a', descriptor: { type: 'string' } },
        { name: 'b', descriptor: { type: 'int' } },
      ],
    });
    useSchemaEditorStore.getState().moveField('b', 'up');
    const names = useSchemaEditorStore.getState().nodes.map((n) => n.name);
    expect(names).toEqual(['b', 'a']);
  });

  it('moveField down', () => {
    useSchemaEditorStore.setState({
      nodes: [
        { name: 'a', descriptor: { type: 'string' } },
        { name: 'b', descriptor: { type: 'int' } },
      ],
    });
    useSchemaEditorStore.getState().moveField('a', 'down');
    const names = useSchemaEditorStore.getState().nodes.map((n) => n.name);
    expect(names).toEqual(['b', 'a']);
  });

  it('syncFromJson succeeds with valid JSON', () => {
    useSchemaEditorStore.setState({ jsonText: '{"host":{"type":"string"}}' });
    const ok = useSchemaEditorStore.getState().syncFromJson();
    expect(ok).toBe(true);
    expect(useSchemaEditorStore.getState().nodes).toHaveLength(1);
    expect(useSchemaEditorStore.getState().jsonError).toBeNull();
  });

  it('syncFromJson fails with invalid JSON', () => {
    useSchemaEditorStore.setState({ jsonText: 'not json' });
    const ok = useSchemaEditorStore.getState().syncFromJson();
    expect(ok).toBe(false);
    expect(useSchemaEditorStore.getState().jsonError).toBeTruthy();
  });

  it('syncToJson serializes nodes to JSON', () => {
    useSchemaEditorStore.setState({
      nodes: [{ name: 'host', descriptor: { type: 'string' } }],
    });
    useSchemaEditorStore.getState().syncToJson();
    const parsed = JSON.parse(useSchemaEditorStore.getState().jsonText);
    expect(parsed.host.type).toBe('string');
  });

  it('validate calls API and sets errors', async () => {
    vi.mocked(servicesApi.validateSchema).mockResolvedValue({ valid: false, error: 'bad schema' });
    useSchemaEditorStore.setState({
      nodes: [{ name: 'x', descriptor: { type: 'string' } }],
    });
    await useSchemaEditorStore.getState().validate('svc1');
    expect(useSchemaEditorStore.getState().validationErrors).toEqual(['bad schema']);
  });

  it('save calls fileWrite and clears dirty', async () => {
    vi.mocked(servicesApi.fileWrite).mockResolvedValue({});
    useSchemaEditorStore.setState({
      nodes: [{ name: 'x', descriptor: { type: 'string' } }],
      dirty: true,
    });
    await useSchemaEditorStore.getState().save('svc1');
    expect(servicesApi.fileWrite).toHaveBeenCalledWith('svc1', 'config.schema.json', expect.any(String));
    expect(useSchemaEditorStore.getState().dirty).toBe(false);
  });

  it('reset restores originalNodes and clears dirty', () => {
    const original = [{ name: 'orig', descriptor: { type: 'string' as const } }];
    useSchemaEditorStore.setState({
      nodes: [{ name: 'changed', descriptor: { type: 'int' } }],
      originalNodes: original,
      dirty: true,
    });
    useSchemaEditorStore.getState().reset();
    const state = useSchemaEditorStore.getState();
    expect(state.nodes[0].name).toBe('orig');
    expect(state.dirty).toBe(false);
  });

  it('dirty flag is set on modifications', () => {
    expect(useSchemaEditorStore.getState().dirty).toBe(false);
    useSchemaEditorStore.getState().addField({ name: 'x', descriptor: { type: 'string' } });
    expect(useSchemaEditorStore.getState().dirty).toBe(true);
  });
});
