import { describe, it, expect, vi } from 'vitest';
import { render, screen } from '@testing-library/react';
import { ConfigProvider } from 'antd';
import { MemoryRouter } from 'react-router-dom';
import { DriversTable } from '../DriversTable';

const mockDrivers = [
  { id: 'drv1', program: '/bin/drv1', metaHash: 'a', name: 'Driver One', version: '1.0' },
  { id: 'drv2', program: '/bin/drv2', metaHash: 'b', name: 'Driver Two', version: '2.0' },
];

function renderTable(drivers = mockDrivers) {
  return render(
    <ConfigProvider>
      <MemoryRouter>
        <DriversTable drivers={drivers} />
      </MemoryRouter>
    </ConfigProvider>,
  );
}

describe('DriversTable', () => {
  it('renders drivers', () => {
    renderTable();
    expect(screen.getByTestId('drivers-table')).toBeDefined();
    expect(screen.getByText('drv1')).toBeDefined();
    expect(screen.getByText('Driver One')).toBeDefined();
  });

  it('shows empty state', () => {
    renderTable([]);
    expect(screen.getByTestId('empty-drivers')).toBeDefined();
  });

  it('renders detail and test buttons', () => {
    renderTable();
    expect(screen.getByTestId('detail-drv1')).toBeDefined();
    expect(screen.getByTestId('test-drv1')).toBeDefined();
  });
});
