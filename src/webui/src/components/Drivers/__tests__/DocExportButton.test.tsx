import { describe, it, expect, vi } from 'vitest';
import { render, screen } from '@testing-library/react';
import { ConfigProvider } from 'antd';

vi.mock('@/stores/useDriversStore', () => ({
  useDriversStore: vi.fn(),
}));

import { DocExportButton } from '../DocExportButton';
import { useDriversStore } from '@/stores/useDriversStore';

describe('DocExportButton', () => {
  it('renders export button', () => {
    const state = { fetchDriverDocs: vi.fn().mockResolvedValue('') };
    vi.mocked(useDriversStore).mockImplementation((sel?: any) => sel ? sel(state) : state);
    render(<ConfigProvider><DocExportButton driverId="drv1" /></ConfigProvider>);
    expect(screen.getByTestId('doc-export-btn')).toBeDefined();
    expect(screen.getByText('Export Docs')).toBeDefined();
  });
});
