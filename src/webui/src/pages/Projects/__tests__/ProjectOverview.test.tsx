import { describe, it, expect, vi, beforeEach } from 'vitest';
import { render, screen, fireEvent } from '@testing-library/react';
import { ConfigProvider } from 'antd';
import { ProjectOverview } from '../components/ProjectOverview';
import type { Project, ProjectRuntime } from '@/types/project';

const mockProject: Project = {
  id: 'p1',
  name: 'Test Project',
  serviceId: 's1',
  enabled: true,
  valid: true,
  config: {},
  schedule: { type: 'manual' },
  instanceCount: 0,
  status: 'stopped',
};

const mockRuntime: ProjectRuntime = {
  id: 'p1',
  enabled: true,
  valid: true,
  status: 'stopped',
  runningInstances: 0,
  instances: [],
  schedule: { type: 'manual', timerActive: false, restartSuppressed: false, consecutiveFailures: 0, shuttingDown: false, autoRestarting: false },
};

function renderComponent(overrides: Partial<{ project: Project; runtime: ProjectRuntime | null }> = {}) {
  return render(
    <ConfigProvider>
      <ProjectOverview
        project={overrides.project ?? mockProject}
        runtime={overrides.runtime !== undefined ? overrides.runtime : mockRuntime}
        onStart={vi.fn()}
        onStop={vi.fn()}
        onReload={vi.fn()}
      />
    </ConfigProvider>,
  );
}

describe('ProjectOverview', () => {
  it('renders project info', () => {
    renderComponent();
    expect(screen.getByTestId('project-overview')).toBeDefined();
    expect(screen.getByText('p1')).toBeDefined();
  });

  it('enables start button when project is valid and stopped', () => {
    renderComponent();
    const btn = screen.getByTestId('start-btn');
    expect(btn.closest('button')?.disabled).toBe(false);
  });

  it('disables start button when running', () => {
    renderComponent({ runtime: { ...mockRuntime, status: 'running' } });
    const btn = screen.getByTestId('start-btn');
    expect(btn.closest('button')?.disabled).toBe(true);
  });

  it('disables stop button when stopped', () => {
    renderComponent();
    const btn = screen.getByTestId('stop-btn');
    expect(btn.closest('button')?.disabled).toBe(true);
  });
});
