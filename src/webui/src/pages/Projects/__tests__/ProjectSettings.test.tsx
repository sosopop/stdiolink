import { describe, it, expect, vi } from 'vitest';
import { render, screen, fireEvent } from '@testing-library/react';
import { ConfigProvider } from 'antd';
import { ProjectSettings } from '../components/ProjectSettings';

function renderComponent(props: Partial<Parameters<typeof ProjectSettings>[0]> = {}) {
  return render(
    <ConfigProvider>
      <ProjectSettings
        projectName="Test Project"
        schedule={{ type: 'manual' }}
        onSave={vi.fn().mockResolvedValue(true)}
        {...props}
      />
    </ConfigProvider>,
  );
}

describe('ProjectSettings', () => {
  it('renders name input and schedule form', () => {
    renderComponent();
    expect(screen.getByTestId('project-settings')).toBeDefined();
    expect(screen.getByTestId('project-name-input')).toBeDefined();
    expect(screen.getByTestId('schedule-form')).toBeDefined();
  });

  it('renders save button', () => {
    renderComponent();
    expect(screen.getByTestId('save-settings-btn')).toBeDefined();
  });

  it('calls onSave with trimmed name and schedule when save clicked', async () => {
    const onSave = vi.fn().mockResolvedValue(true);
    renderComponent({ onSave, projectName: '  Updated Project  ', schedule: { type: 'manual', runTimeoutMs: 0 } });

    const input = screen.getByTestId('project-name-input');
    fireEvent.change(input, { target: { value: '  Updated Project  ' } });
    fireEvent.click(screen.getByTestId('save-settings-btn'));

    await vi.waitFor(() => {
      expect(onSave).toHaveBeenCalledWith({
        name: 'Updated Project',
        schedule: expect.objectContaining({ type: 'manual', runTimeoutMs: 0 }),
      });
    });
  });

  it('rejects empty project name', async () => {
    const onSave = vi.fn().mockResolvedValue(true);
    renderComponent({ onSave });

    const input = screen.getByTestId('project-name-input');
    fireEvent.change(input, { target: { value: '   ' } });
    fireEvent.click(screen.getByTestId('save-settings-btn'));

    await vi.waitFor(() => {
      expect(onSave).not.toHaveBeenCalled();
    });
    expect(screen.getAllByText('Project name cannot be empty').length).toBeGreaterThan(0);
  });

  it('renders runTimeoutMs input for manual schedule', () => {
    renderComponent();
    expect(screen.getByTestId('run-timeout-ms')).toBeDefined();
  });

  it('saves runTimeoutMs in schedule payload', async () => {
    const onSave = vi.fn().mockResolvedValue(true);
    renderComponent({ onSave, schedule: { type: 'manual', runTimeoutMs: 0 } });

    const input = screen.getByRole('spinbutton');
    fireEvent.change(input, { target: { value: '3000' } });
    fireEvent.blur(input);
    fireEvent.click(screen.getByTestId('save-settings-btn'));

    await vi.waitFor(() => {
      expect(onSave).toHaveBeenCalledWith({
        name: 'Test Project',
        schedule: expect.objectContaining({ runTimeoutMs: 3000 }),
      });
    });
  });

  it('rejects fractional runTimeoutMs instead of normalizing it', async () => {
    const onSave = vi.fn().mockResolvedValue(true);
    renderComponent({ onSave, schedule: { type: 'manual', runTimeoutMs: 0 } });

    const input = screen.getByRole('spinbutton');
    fireEvent.input(input, { target: { value: '3000.5' } });
    fireEvent.blur(input, { target: { value: '3000.5' } });
    fireEvent.click(screen.getByTestId('save-settings-btn'));

    await vi.waitFor(() => {
      expect(onSave).not.toHaveBeenCalled();
    });
    expect(screen.getAllByText('Run timeout must be a non-negative integer').length).toBeGreaterThan(0);
  });

  it('rejects runTimeoutMs above int32 max instead of submitting it', async () => {
    const onSave = vi.fn().mockResolvedValue(true);
    renderComponent({ onSave, schedule: { type: 'manual', runTimeoutMs: 0 } });

    const input = screen.getByRole('spinbutton');
    fireEvent.input(input, { target: { value: '5000000000' } });
    fireEvent.blur(input, { target: { value: '5000000000' } });
    fireEvent.click(screen.getByTestId('save-settings-btn'));

    await vi.waitFor(() => {
      expect(onSave).not.toHaveBeenCalled();
    });
    expect(screen.getAllByText('Run timeout must be a non-negative integer').length).toBeGreaterThan(0);
  });

  it('renders localized timeout label and hint', () => {
    renderComponent();
    expect(screen.getByText('Run Timeout (ms)')).toBeDefined();
    expect(screen.getByText('0 = disabled')).toBeDefined();
  });
});
