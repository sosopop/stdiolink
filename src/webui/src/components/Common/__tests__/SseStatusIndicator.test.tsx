import { describe, it, expect } from 'vitest';
import { render, screen } from '@testing-library/react';
import { ConfigProvider } from 'antd';
import { SseStatusIndicator } from '../SseStatusIndicator';

describe('SseStatusIndicator', () => {
  it('shows Live when connected', () => {
    render(<ConfigProvider><SseStatusIndicator status="connected" lastEventTime={Date.now()} /></ConfigProvider>);
    expect(screen.getByTestId('sse-label').textContent).toBe('Live');
  });

  it('shows Offline when disconnected', () => {
    render(<ConfigProvider><SseStatusIndicator status="disconnected" lastEventTime={null} /></ConfigProvider>);
    expect(screen.getByTestId('sse-label').textContent).toBe('Offline');
  });

  it('shows Reconnecting when reconnecting', () => {
    render(<ConfigProvider><SseStatusIndicator status="reconnecting" lastEventTime={null} /></ConfigProvider>);
    expect(screen.getByTestId('sse-label').textContent).toBe('Reconnecting');
  });

  it('shows Error when error', () => {
    render(<ConfigProvider><SseStatusIndicator status="error" lastEventTime={null} error="Connection failed" /></ConfigProvider>);
    expect(screen.getByTestId('sse-label').textContent).toBe('Error');
  });

  it('renders indicator dot', () => {
    render(<ConfigProvider><SseStatusIndicator status="connected" lastEventTime={null} /></ConfigProvider>);
    expect(screen.getByTestId('sse-dot')).toBeDefined();
  });

  it('renders indicator container', () => {
    render(<ConfigProvider><SseStatusIndicator status="connected" lastEventTime={null} /></ConfigProvider>);
    expect(screen.getByTestId('sse-indicator')).toBeDefined();
  });
});
