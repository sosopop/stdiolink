import { Tag } from 'antd';
import type { ServerEvent } from '@/types/server';
import styles from '../dashboard.module.css';

interface EventFeedProps {
  events: ServerEvent[];
}

const eventColorMap: Record<string, string> = {
  'instance.started': 'green',
  'instance.finished': 'default',
  'schedule.triggered': 'blue',
  'schedule.suppressed': 'orange',
};

export const EventFeed: React.FC<EventFeedProps> = ({ events }) => (
  <div className="glass-panel" style={{ padding: 24 }} data-testid="event-feed">
    <h3 style={{ marginBottom: 16 }}>Event Feed</h3>
    {events.length === 0 ? (
      <div data-testid="empty-events">No events</div>
    ) : (
      <div className={styles.eventList}>
        {events.map((event, i) => (
          <div key={i} className={styles.eventItem} data-testid="event-item">
            <Tag color={eventColorMap[event.type] ?? 'default'}>{event.type}</Tag>
            <span className={styles.eventData}>
              {JSON.stringify(event.data).slice(0, 80)}
            </span>
          </div>
        ))}
      </div>
    )}
  </div>
);
