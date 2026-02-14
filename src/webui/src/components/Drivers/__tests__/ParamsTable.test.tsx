import { describe, it, expect } from 'vitest';
import { render, screen } from '@testing-library/react';
import { ConfigProvider } from 'antd';
import { ParamsTable } from '../ParamsTable';

const mockParams = [
  { name: 'host', type: 'string', required: true, description: 'Server host' },
  { name: 'port', type: 'int', required: false, min: 1, max: 65535, default: 8080 },
  { name: 'db', type: 'object', fields: [{ name: 'name', type: 'string', required: true }] },
];

describe('ParamsTable', () => {
  it('renders params', () => {
    render(<ConfigProvider><ParamsTable params={mockParams} /></ConfigProvider>);
    expect(screen.getByTestId('params-table')).toBeDefined();
    expect(screen.getByTestId('param-host')).toBeDefined();
    expect(screen.getByTestId('param-port')).toBeDefined();
  });

  it('shows required marker', () => {
    render(<ConfigProvider><ParamsTable params={mockParams} /></ConfigProvider>);
    const rows = screen.getAllByRole('row');
    expect(rows.length).toBeGreaterThan(1);
  });

  it('shows nested object fields', () => {
    render(<ConfigProvider><ParamsTable params={mockParams} /></ConfigProvider>);
    expect(screen.getByTestId('param-db.name')).toBeDefined();
  });

  it('shows empty state', () => {
    render(<ConfigProvider><ParamsTable params={[]} /></ConfigProvider>);
    expect(screen.getByTestId('empty-params')).toBeDefined();
  });

  it('shows constraints in description', () => {
    render(<ConfigProvider><ParamsTable params={mockParams} /></ConfigProvider>);
    expect(screen.getByText(/min: 1/)).toBeDefined();
    expect(screen.getByText(/max: 65535/)).toBeDefined();
  });
});
