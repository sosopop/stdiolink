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
});
