import { describe, it, expect } from 'vitest';
import { render, screen } from '@testing-library/react';
import { ConfigProvider } from 'antd';
import { ResourceMetricCards } from '../ResourceMetricCards';

const mockProcesses = [
  { pid: 100, name: 'node', cpuPercent: 25.5, memoryRssBytes: 1024 * 1024 * 128, threadCount: 4, uptimeSeconds: 3661, ioReadBytes: 1024 * 1024, ioWriteBytes: 512 * 1024 },
];

describe('ResourceMetricCards', () => {
  it('renders metric cards with data', () => {
    render(<ConfigProvider><ResourceMetricCards processes={mockProcesses} /></ConfigProvider>);
    expect(screen.getByTestId('resource-metrics')).toBeDefined();
  });

  it('shows empty state with dashes', () => {
    render(<ConfigProvider><ResourceMetricCards processes={[]} /></ConfigProvider>);
    expect(screen.getByTestId('resource-metrics-empty')).toBeDefined();
  });
});
