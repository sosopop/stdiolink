import { describe, it, expect, vi } from 'vitest';
import { render, screen } from '@testing-library/react';
import { ConfigProvider } from 'antd';
import { ExportMetaButton } from '../ExportMetaButton';

const mockMeta = {
  schemaVersion: '1',
  info: { name: 'Test', version: '1.0' },
  commands: [],
};

describe('ExportMetaButton', () => {
  it('renders export button', () => {
    render(<ConfigProvider><ExportMetaButton driverId="drv1" meta={mockMeta} /></ConfigProvider>);
    expect(screen.getByTestId('export-meta-btn')).toBeDefined();
  });

  it('is disabled when no meta', () => {
    render(<ConfigProvider><ExportMetaButton driverId="drv1" meta={null} /></ConfigProvider>);
    const btn = screen.getByTestId('export-meta-btn').closest('button');
    expect(btn?.disabled).toBe(true);
  });

  it('is enabled when meta exists', () => {
    render(<ConfigProvider><ExportMetaButton driverId="drv1" meta={mockMeta} /></ConfigProvider>);
    const btn = screen.getByTestId('export-meta-btn').closest('button');
    expect(btn?.disabled).toBe(false);
  });
});
