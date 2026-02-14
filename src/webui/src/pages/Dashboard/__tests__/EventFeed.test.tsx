import { describe, it, expect } from 'vitest';
import { render, screen } from '@testing-library/react';
import { EventFeed } from '../components/EventFeed';
import { ConfigProvider } from 'antd';
import type { ServerEvent } from '@/types/server';

function renderComponent(events: ServerEvent[] = []) {
  return render(
    <ConfigProvider>
      <EventFeed events={events} />
    </ConfigProvider>,
  );
}

describe('EventFeed', () => {
  it('renders empty state', () => {
    renderComponent();
    expect(screen.getByTestId('empty-events')).toBeDefined();
  });

  it('renders event items', () => {
    const events: ServerEvent[] = [
      { type: 'instance.started', data: { instanceId: 'i1' } },
      { type: 'instance.finished', data: { instanceId: 'i2' } },
    ];
    renderComponent(events);
    const items = screen.getAllByTestId('event-item');
    expect(items).toHaveLength(2);
  });

  it('displays event type as tag', () => {
    const events: ServerEvent[] = [
      { type: 'instance.started', data: { instanceId: 'i1' } },
    ];
    renderComponent(events);
    expect(screen.getByText('instance.started')).toBeDefined();
  });

  it('displays event data summary', () => {
    const events: ServerEvent[] = [
      { type: 'schedule.triggered', data: { projectId: 'p1' } },
    ];
    renderComponent(events);
    expect(screen.getByTestId('event-item').textContent).toContain('projectId');
  });
});
