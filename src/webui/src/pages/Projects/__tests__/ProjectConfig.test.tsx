import { describe, it, expect, vi, beforeEach, afterEach } from 'vitest';
import { render, screen, fireEvent } from '@testing-library/react';
import { ConfigProvider } from 'antd';
import { ProjectConfig } from '../components/ProjectConfig';
import type { FieldMeta } from '@/types/service';

const schema: FieldMeta[] = [
  { name: 'host', type: 'string', description: 'Server host' },
  { name: 'port', type: 'int', description: 'Server port' },
];

function renderComponent(props: Partial<Parameters<typeof ProjectConfig>[0]> = {}) {
  return render(
    <ConfigProvider>
      <ProjectConfig
        projectId="demo-project"
        config={{ host: 'localhost', port: 6200 }}
        schema={schema}
        serviceDir="D:/data/services/demo"
        dataRoot="D:/data"
        onSave={vi.fn().mockResolvedValue(true)}
        {...props}
      />
    </ConfigProvider>,
  );
}

describe('ProjectConfig', () => {
  const originalBlob = Blob;
  const originalCreateObjectURL = URL.createObjectURL;
  const originalRevokeObjectURL = URL.revokeObjectURL;

  beforeEach(() => {
    class FakeBlob {
      public readonly parts: unknown[];

      public readonly type: string;

      constructor(parts: unknown[], options?: { type?: string }) {
        this.parts = parts;
        this.type = options?.type ?? '';
      }
    }

    // JSDOM Blob is awkward to introspect in this test; a fake keeps the export assertion deterministic.
    // eslint-disable-next-line @typescript-eslint/no-explicit-any
    globalThis.Blob = FakeBlob as any;
    URL.createObjectURL = vi.fn(() => 'blob:test-url');
    URL.revokeObjectURL = vi.fn();
  });

  afterEach(() => {
    globalThis.Blob = originalBlob;
    URL.createObjectURL = originalCreateObjectURL;
    URL.revokeObjectURL = originalRevokeObjectURL;
    vi.restoreAllMocks();
  });

  it('renders schema form', () => {
    renderComponent();
    expect(screen.getByTestId('project-config')).toBeDefined();
    expect(screen.getByTestId('schema-form')).toBeDefined();
  });

  it('renders save button', () => {
    renderComponent();
    expect(screen.getByTestId('save-config-btn')).toBeDefined();
  });

  it('calls onSave when save clicked', async () => {
    const onSave = vi.fn().mockResolvedValue(true);
    renderComponent({ onSave });
    fireEvent.click(screen.getByTestId('save-config-btn'));
    await vi.waitFor(() => {
      expect(onSave).toHaveBeenCalled();
    });
  });

  it('renders test commands panel and export action', () => {
    renderComponent();
    expect(screen.getByTestId('project-config-test-commands')).toBeDefined();
    expect(screen.getByText('Expanded config arguments')).toBeDefined();
    expect(screen.getByText('Config file mode')).toBeDefined();
    expect(screen.getByTestId('export-config-btn')).toBeDefined();
    expect(screen.getByText('stdiolink_service "D:/data/services/demo" --data-root="D:/data" --config-file="demo-project.config.json"')).toBeDefined();
  });

  it('shows placeholder when command inputs are not ready', () => {
    renderComponent({ serviceDir: null, dataRoot: null });
    expect(screen.getByTestId('project-config-test-commands-placeholder')).toBeDefined();
  });

  it('exports saved config instead of unsaved edits', async () => {
    let exportedBlob: { parts: unknown[] } | null = null;
    URL.createObjectURL = vi.fn((blob: Blob | MediaSource) => {
      exportedBlob = blob as unknown as { parts: unknown[] };
      return 'blob:test-url';
    });
    const clickSpy = vi.spyOn(HTMLAnchorElement.prototype, 'click').mockImplementation(() => {});

    renderComponent({ config: { host: 'saved-host', port: 6200 } });
    fireEvent.change(screen.getByDisplayValue('saved-host'), { target: { value: 'edited-host' } });
    fireEvent.click(screen.getByTestId('export-config-btn'));

    expect(clickSpy).toHaveBeenCalled();
    expect(exportedBlob).not.toBeNull();
    const exportedText = String(exportedBlob!.parts[0]);
    expect(exportedText).toContain('"host": "saved-host"');
    expect(exportedText).not.toContain('"host": "edited-host"');
  });
});

