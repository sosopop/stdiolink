import { describe, it, expect } from 'vitest';
import { render, screen } from '@testing-library/react';
import { ConfigProvider } from 'antd';
import { ServiceSchema } from '../components/ServiceSchema';
import type { FieldMeta } from '@/types/service';

function renderComponent(fields: FieldMeta[] = [], requiredKeys: string[] = []) {
  return render(
    <ConfigProvider>
      <ServiceSchema fields={fields} requiredKeys={requiredKeys} />
    </ConfigProvider>,
  );
}

describe('ServiceSchema', () => {
  it('renders schema table with fields', () => {
    const fields: FieldMeta[] = [
      { name: 'host', type: 'string', description: 'Server host', default: 'localhost' },
      { name: 'port', type: 'int', description: 'Server port', default: 8080 },
    ];
    renderComponent(fields, ['host']);
    expect(screen.getByTestId('service-schema')).toBeDefined();
    expect(screen.getByText('host')).toBeDefined();
    expect(screen.getByText('port')).toBeDefined();
  });

  it('shows empty state when no fields', () => {
    renderComponent([]);
    expect(screen.getByTestId('no-schema')).toBeDefined();
  });

  it('flattens nested object fields', () => {
    const fields: FieldMeta[] = [
      {
        name: 'database',
        type: 'object',
        fields: [
          { name: 'host', type: 'string' },
          { name: 'port', type: 'int' },
        ],
        requiredKeys: ['host'],
      },
    ];
    renderComponent(fields);
    expect(screen.getByText('database')).toBeDefined();
    expect(screen.getByText('database.host')).toBeDefined();
    expect(screen.getByText('database.port')).toBeDefined();
  });
});
