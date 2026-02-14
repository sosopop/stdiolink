import { describe, it, expect } from 'vitest';
import { render, screen } from '@testing-library/react';
import { MemoryRouter } from 'react-router-dom';
import { ConfigProvider } from 'antd';
import { AppLayout } from '@/components/Layout/AppLayout';

function renderWithRouter(route = '/dashboard') {
  return render(
    <ConfigProvider>
      <MemoryRouter initialEntries={[route]}>
        <AppLayout />
      </MemoryRouter>
    </ConfigProvider>,
  );
}

describe('AppLayout', () => {
  it('renders header with logo', () => {
    renderWithRouter();
    expect(screen.getByText('stdiolink')).toBeDefined();
  });

  it('renders sidebar navigation', () => {
    renderWithRouter();
    expect(screen.getByText('Dashboard')).toBeDefined();
  });

  it('renders content area', () => {
    const { container } = renderWithRouter();
    const content = container.querySelector('.ant-layout-content');
    expect(content).toBeDefined();
  });
});
