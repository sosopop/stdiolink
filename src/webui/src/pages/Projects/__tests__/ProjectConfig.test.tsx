import { describe, it, expect, vi } from 'vitest';
import { render, screen, fireEvent } from '@testing-library/react';
import { ConfigProvider } from 'antd';
import { ProjectConfig } from '../components/ProjectConfig';
import type { FieldMeta } from '@/types/service';

const schema: FieldMeta[] = [
  { name: 'host', type: 'string', description: 'Server host' },
  { name: 'port', type: 'int', description: 'Server port' },
];

const execRunnerSchema: FieldMeta[] = [
  { name: 'program', type: 'string', description: 'Executable path' },
  { name: 'args', type: 'array', description: 'Args', items: { name: 'arg', type: 'string' } },
  { name: 'success_exit_codes', type: 'array', description: 'Success codes', items: { name: 'code', type: 'int' } },
];

function renderComponent(props: Partial<Parameters<typeof ProjectConfig>[0]> = {}) {
  return render(
    <ConfigProvider>
      <ProjectConfig
        projectId="demo-project"
        config={{ host: 'localhost', port: 6200 }}
        schema={schema}
        serviceDir="D:/code/stdiolink/release/pkg/data_root/services/demo"
        dataRoot="D:/code/stdiolink/release/pkg/data_root"
        onSave={vi.fn().mockResolvedValue(true)}
        {...props}
      />
    </ConfigProvider>,
  );
}

describe('ProjectConfig', () => {
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

  it('renders test commands panel with the saved param file path', () => {
    renderComponent();
    expect(screen.getByTestId('project-config-test-commands')).toBeDefined();
    expect(screen.getByText('Expanded config arguments')).toBeDefined();
    expect(screen.getByText('Config file mode')).toBeDefined();
    expect(screen.queryByTestId('export-config-btn')).toBeNull();
    expect(screen.getByTestId('project-config-test-commands').textContent)
      .toContain('stdiolink_service "data_root/services/demo" --data-root="data_root" --config-file="data_root/projects/demo-project/param.json"');
  });

  it('renders the test commands panel before the editable form', () => {
    renderComponent();
    const panel = screen.getByTestId('project-config-test-commands');
    const form = screen.getByTestId('schema-form');
    expect(panel.compareDocumentPosition(form) & Node.DOCUMENT_POSITION_FOLLOWING).toBeTruthy();
  });

  it('shows placeholder when command inputs are not ready', () => {
    renderComponent({ serviceDir: null, dataRoot: null });
    expect(screen.getByTestId('project-config-test-commands-placeholder')).toBeDefined();
  });

  it('keeps test commands bound to the saved config instead of unsaved edits', () => {
    renderComponent({ config: { host: 'saved-host', port: 6200 } });
    fireEvent.change(screen.getByDisplayValue('saved-host'), { target: { value: 'edited-host' } });

    expect(screen.getByTestId('project-config-test-commands').textContent)
      .toContain('stdiolink_service "data_root/services/demo" --data-root="data_root" --config.host="saved-host" --config.port=6200');
  });

  it('renders scalar arrays as compact rows in project config', () => {
    renderComponent({
      schema: execRunnerSchema,
      config: { program: 'cmd.exe', args: ['/c', 'echo'], success_exit_codes: [0, 2] },
    });

    expect(screen.getByTestId('compact-array-args')).toBeDefined();
    expect(screen.getByTestId('compact-array-success_exit_codes')).toBeDefined();
    expect(screen.getByTestId('compact-array-row-args-0')).toBeDefined();
    expect(screen.getByTestId('compact-array-row-success_exit_codes-1')).toBeDefined();
  });
});

