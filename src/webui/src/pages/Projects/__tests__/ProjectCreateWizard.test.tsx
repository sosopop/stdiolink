import { describe, it, expect, vi, beforeEach } from 'vitest';
import { render, screen, fireEvent } from '@testing-library/react';
import { ConfigProvider } from 'antd';
import { ProjectCreateWizard } from '../components/ProjectCreateWizard';
import type { ServiceInfo } from '@/types/service';

const mockServices: ServiceInfo[] = [
  { id: 's1', name: 'Service One', version: '1.0', serviceDir: '/d', hasSchema: true, projectCount: 0 },
  { id: 's2', name: 'Service Two', version: '2.0', serviceDir: '/d', hasSchema: false, projectCount: 1 },
];

function renderWizard(props: Partial<Parameters<typeof ProjectCreateWizard>[0]> = {}) {
  const defaultProps = {
    open: true,
    onClose: vi.fn(),
    onCreate: vi.fn().mockResolvedValue(true),
    services: mockServices,
    getServiceDetail: vi.fn().mockResolvedValue({ configSchemaFields: [] }),
    ...props,
  };
  return { ...render(<ConfigProvider><ProjectCreateWizard {...defaultProps} /></ConfigProvider>), props: defaultProps };
}

describe('ProjectCreateWizard', () => {
  beforeEach(() => vi.clearAllMocks());

  it('renders service selection on step 0', () => {
    renderWizard();
    expect(screen.getByTestId('step-service')).toBeDefined();
    expect(screen.getByTestId('service-card-s1')).toBeDefined();
    expect(screen.getByTestId('service-card-s2')).toBeDefined();
  });

  it('advances to step 1 after selecting service', async () => {
    renderWizard();
    fireEvent.click(screen.getByTestId('service-card-s1'));
    fireEvent.click(screen.getByTestId('wizard-ok'));
    expect(screen.getByTestId('step-info')).toBeDefined();
  });

  it('shows ID validation error', () => {
    renderWizard();
    fireEvent.click(screen.getByTestId('service-card-s1'));
    fireEvent.click(screen.getByTestId('wizard-ok'));
    fireEvent.change(screen.getByTestId('project-id-input'), { target: { value: 'bad id!' } });
    expect(screen.getByText('Only letters, numbers, _ and - allowed')).toBeDefined();
  });

  it('disables OK when ID is empty on step 1', () => {
    renderWizard();
    fireEvent.click(screen.getByTestId('service-card-s1'));
    fireEvent.click(screen.getByTestId('wizard-ok'));
    const okBtn = screen.getByTestId('wizard-ok');
    expect(okBtn.closest('button')?.disabled).toBe(true);
  });

  it('advances through all steps and creates', async () => {
    const { props } = renderWizard();
    // Step 0: select service
    fireEvent.click(screen.getByTestId('service-card-s1'));
    fireEvent.click(screen.getByTestId('wizard-ok'));
    // Step 1: fill info
    fireEvent.change(screen.getByTestId('project-id-input'), { target: { value: 'my_proj' } });
    fireEvent.change(screen.getByTestId('project-name-input'), { target: { value: 'My Project' } });
    fireEvent.click(screen.getByTestId('wizard-ok'));
    // Step 2: config (skip)
    expect(screen.getByTestId('step-config')).toBeDefined();
    fireEvent.click(screen.getByTestId('wizard-ok'));
    // Step 3: schedule
    expect(screen.getByTestId('step-schedule')).toBeDefined();
    fireEvent.click(screen.getByTestId('wizard-ok'));
    await vi.waitFor(() => {
      expect(props.onCreate).toHaveBeenCalled();
    });
  });
});
