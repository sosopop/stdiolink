import { describe, it, expect, vi, beforeEach } from 'vitest';
import { render, screen } from '@testing-library/react';
import { ConfigProvider } from 'antd';

vi.mock('@/stores/useDriversStore', () => ({
  useDriversStore: vi.fn(),
}));

vi.mock('react-markdown', () => ({
  default: ({ children }: any) => <div data-testid="markdown-content">{children}</div>,
}));

vi.mock('remark-gfm', () => ({
  default: () => {},
}));

import { DriverDocs } from '../DriverDocs';
import { useDriversStore } from '@/stores/useDriversStore';

function renderDocs(overrides = {}) {
  const state = {
    docsMarkdown: '# Test Docs\n\nSome content',
    docsLoading: false,
    error: null,
    fetchDriverDocs: vi.fn().mockResolvedValue(''),
    ...overrides,
  };
  vi.mocked(useDriversStore).mockImplementation((sel?: any) => sel ? sel(state) : state);
  return render(
    <ConfigProvider>
      <DriverDocs driverId="drv1" />
    </ConfigProvider>,
  );
}

describe('DriverDocs', () => {
  beforeEach(() => vi.clearAllMocks());

  it('renders markdown content', () => {
    renderDocs();
    expect(screen.getByTestId('driver-docs')).toBeDefined();
    expect(screen.getByTestId('markdown-content')).toBeDefined();
  });

  it('shows loading state', () => {
    renderDocs({ docsLoading: true, docsMarkdown: null });
    expect(screen.getByTestId('docs-loading')).toBeDefined();
  });

  it('shows error with retry', () => {
    renderDocs({ error: 'Failed to load', docsMarkdown: null });
    expect(screen.getByTestId('docs-error')).toBeDefined();
    expect(screen.getByTestId('docs-retry')).toBeDefined();
  });

  it('shows empty state when no docs', () => {
    renderDocs({ docsMarkdown: null });
    expect(screen.getByTestId('docs-empty')).toBeDefined();
  });
});
