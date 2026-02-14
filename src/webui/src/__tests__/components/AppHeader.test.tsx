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
  it('renders logo text', () => {
    renderWithProviders(<AppHeader />);
    expect(screen.getByText('stdiolink')).toBeDefined();
  });

  it('renders toggle sidebar button', () => {
    renderWithProviders(<AppHeader />);
    expect(screen.getByLabelText('Toggle sidebar')).toBeDefined();
  });

  it('renders theme toggle', () => {
    renderWithProviders(<AppHeader />);
    expect(screen.getByLabelText('Toggle theme')).toBeDefined();
  });
});
