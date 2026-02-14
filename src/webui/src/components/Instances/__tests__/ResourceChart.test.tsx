import { describe, it, expect } from 'vitest';
import { render, screen } from '@testing-library/react';
import { ResourceChart } from '../ResourceChart';
import type { ResourceSample } from '@/stores/useInstancesStore';

// Mock recharts to avoid SVG rendering issues in jsdom
vi.mock('recharts', () => ({
  AreaChart: ({ children }: any) => <div data-testid="area-chart">{children}</div>,
  Area: () => <div data-testid="area" />,
  XAxis: () => <div />,
  YAxis: () => <div />,
  Tooltip: () => <div />,
  ResponsiveContainer: ({ children }: any) => <div data-testid="responsive-container">{children}</div>,
  CartesianGrid: () => <div />,
}));

const mockData: ResourceSample[] = [
  { timestamp: 1000, cpuPercent: 25, memoryRssBytes: 1024 * 1024 * 128, threadCount: 4 },
  { timestamp: 6000, cpuPercent: 30, memoryRssBytes: 1024 * 1024 * 140, threadCount: 4 },
  { timestamp: 11000, cpuPercent: 28, memoryRssBytes: 1024 * 1024 * 135, threadCount: 5 },
];

describe('ResourceChart', () => {
  it('renders charts with data', () => {
    render(<ResourceChart data={mockData} />);
    expect(screen.getByTestId('resource-chart')).toBeDefined();
  });

  it('shows empty state when no data', () => {
    render(<ResourceChart data={[]} />);
    expect(screen.getByTestId('empty-chart')).toBeDefined();
  });

  it('renders two chart sections', () => {
    render(<ResourceChart data={mockData} />);
    expect(screen.getByText('CPU Usage (%)')).toBeDefined();
    expect(screen.getByText('Memory Usage (MB)')).toBeDefined();
  });
});
