import { describe, it, expect } from 'vitest';
import { render, screen } from '@testing-library/react';
import { KpiCard } from '../components/KpiCard';
import { AppstoreOutlined } from '@ant-design/icons';
import { ConfigProvider } from 'antd';

function renderCard(props: Partial<Parameters<typeof KpiCard>[0]> = {}) {
  return render(
    <ConfigProvider>
      <KpiCard title="Services" value={12} icon={<AppstoreOutlined />} {...props} />
    </ConfigProvider>,
  );
}

describe('KpiCard', () => {
  it('renders title and value', () => {
    renderCard();
    expect(screen.getByText('Services')).toBeDefined();
    expect(screen.getByTestId('kpi-value').textContent).toBe('12');
  });

  it('renders value of 0', () => {
    renderCard({ value: 0 });
    expect(screen.getByTestId('kpi-value').textContent).toBe('0');
  });

  it('renders subtitle when provided', () => {
    renderCard({ subtitle: '3 enabled' });
    expect(screen.getByText('3 enabled')).toBeDefined();
  });
});
