import { describe, it, expect } from 'vitest';
import { render, screen } from '@testing-library/react';
import { MemoryRouter } from 'react-router-dom';
import { ConfigProvider } from 'antd';
import { AppHeader } from '@/components/Layout/AppHeader';

function renderWithProviders(ui: React.ReactElement) {
  return render(
    <ConfigProvider>
      <MemoryRouter>{ui}</MemoryRouter>
    </ConfigProvider>,
  );
}

describe('AppHeader', () => {
  it('renders logo text wrapped in link', () => {
    renderWithProviders(<AppHeader />);
    const link = screen.getByRole('link', { name: /STDIOLINK/i });
    expect(link).toBeDefined();
    expect(link.getAttribute('href')).toBe('/');
    expect(screen.getByText('Unified Service Orchestration Scheduler')).toBeDefined();
  });

  it('renders sse status indicator', () => {
    renderWithProviders(<AppHeader />);
    expect(screen.getByTestId('sse-indicator')).toBeDefined();
  });

  it('renders theme toggle', () => {
    renderWithProviders(<AppHeader />);
    expect(screen.getByLabelText('Toggle theme')).toBeDefined();
  });
});
