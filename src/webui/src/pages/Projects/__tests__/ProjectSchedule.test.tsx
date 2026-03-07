import { describe, it, expect, vi } from 'vitest';
import { render, screen, fireEvent } from '@testing-library/react';
import { ConfigProvider } from 'antd';
import { ProjectSchedule } from '../components/ProjectSchedule';

function renderComponent(props: Partial<Parameters<typeof ProjectSchedule>[0]> = {}) {
  return render(
    <ConfigProvider>
      <ProjectSchedule
        schedule={{ type: 'manual' }}
        onSave={vi.fn().mockResolvedValue(true)}
        {...props}
      />
    </ConfigProvider>,
  );
}

describe('ProjectSchedule', () => {
  it('renders schedule form', () => {
    renderComponent();
    expect(screen.getByTestId('project-schedule')).toBeDefined();
    expect(screen.getByTestId('schedule-form')).toBeDefined();
  });

  it('renders save button', () => {
    renderComponent();
    expect(screen.getByTestId('save-schedule-btn')).toBeDefined();
  });

  it('calls onSave when save clicked', async () => {
    const onSave = vi.fn().mockResolvedValue(true);
    renderComponent({ onSave });
    fireEvent.click(screen.getByTestId('save-schedule-btn'));
    await vi.waitFor(() => {
      expect(onSave).toHaveBeenCalled();
    });
  });

  it('T16 renders runTimeoutMs input for manual schedule', () => {
    renderComponent();
    expect(screen.getByTestId('run-timeout-ms')).toBeDefined();
  });

  it('T17 saves runTimeoutMs in schedule payload', async () => {
    const onSave = vi.fn().mockResolvedValue(true);
    renderComponent({ onSave, schedule: { type: 'manual', runTimeoutMs: 0 } });

    const input = screen.getByRole('spinbutton');
    fireEvent.change(input, { target: { value: '3000' } });
    fireEvent.blur(input);
    fireEvent.click(screen.getByTestId('save-schedule-btn'));

    await vi.waitFor(() => {
      expect(onSave).toHaveBeenCalledWith(expect.objectContaining({ runTimeoutMs: 3000 }));
    });
  });

  it('rejects fractional runTimeoutMs instead of normalizing it', async () => {
    const onSave = vi.fn().mockResolvedValue(true);
    renderComponent({ onSave, schedule: { type: 'manual', runTimeoutMs: 0 } });

    const input = screen.getByRole('spinbutton');
    fireEvent.input(input, { target: { value: '3000.5' } });
    fireEvent.blur(input, { target: { value: '3000.5' } });
    const saveButton = screen.getByTestId('save-schedule-btn');
    fireEvent.click(saveButton);

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
    fireEvent.click(screen.getByTestId('save-schedule-btn'));

    await vi.waitFor(() => {
      expect(onSave).not.toHaveBeenCalled();
    });
    expect(screen.getAllByText('Run timeout must be a non-negative integer').length).toBeGreaterThan(0);
  });

  it('T18 renders localized timeout label and hint', () => {
    renderComponent();
    expect(screen.getByText('Run Timeout (ms)')).toBeDefined();
    expect(screen.getByText('0 = disabled')).toBeDefined();
  });
});
