import { describe, it, expect, vi } from 'vitest';
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
        config={{ host: 'localhost', port: 8080 }}
        schema={schema}
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
});
