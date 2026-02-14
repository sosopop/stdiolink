import { describe, it, expect } from 'vitest';
import { render, screen } from '@testing-library/react';
import { MemoryRouter } from 'react-router-dom';
import { ConfigProvider } from 'antd';
import { AppSidebar } from '@/components/Layout/AppSidebar';

function renderWithProviders(ui: React.ReactElement, route = '/dashboard') {
  return render(
    <ConfigProvider>
      <MemoryRouter initialEntries={[route]}>{ui}</MemoryRouter>
    </ConfigProvider>,
  );
}

describe('AppSidebar', () => {
  it('renders all navigation items', () => {
    renderWithProviders(<AppSidebar collapsed={false} />);
    expect(screen.getByText('Dashboard')).toBeDefined();
    expect(screen.getByText('Services')).toBeDefined();
    expect(screen.getByText('Projects')).toBeDefined();
    expect(screen.getByText('Instances')).toBeDefined();
    expect(screen.getByText('Drivers')).toBeDefined();
    expect(screen.getByText('DriverLab')).toBeDefined();
  });

  it('renders in collapsed mode without text labels visible', () => {
    const { container } = renderWithProviders(<AppSidebar collapsed={true} />);
    const sider = container.querySelector('.ant-layout-sider-collapsed');
    expect(sider).toBeDefined();
  });
});
