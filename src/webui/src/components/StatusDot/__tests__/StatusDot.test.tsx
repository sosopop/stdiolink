import { describe, it, expect } from 'vitest';
import { render, screen } from '@testing-library/react';
import { StatusDot } from '@/components/StatusDot/StatusDot';

describe('StatusDot', () => {
  it('renders with running status', () => {
    render(<StatusDot status="running" />);
    const dot = screen.getByTestId('status-dot');
    expect(dot.dataset.status).toBe('running');
  });

  it('renders with stopped status', () => {
    render(<StatusDot status="stopped" />);
    const dot = screen.getByTestId('status-dot');
    expect(dot.dataset.status).toBe('stopped');
  });

  it('renders with error status', () => {
    render(<StatusDot status="error" />);
    const dot = screen.getByTestId('status-dot');
    expect(dot.dataset.status).toBe('error');
  });
});
