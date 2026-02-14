import React from 'react';
import { Tag } from 'antd';
import type { ServerEvent } from '@/types/server';
import styles from '../dashboard.module.css';

interface EventFeedProps {
  events: ServerEvent[];
}

const eventColorMap: Record<string, string> = {
  'instance.started': 'var(--color-success)',
  'instance.finished': 'var(--text-tertiary)',
  'schedule.triggered': 'var(--color-info)',
  'schedule.suppressed': 'var(--color-warning)',
};

export const EventFeed: React.FC<EventFeedProps> = ({ events }) => {
  const formatTime = (ts?: string) => {
    if (!ts) return '';
    return new Date(ts).toLocaleTimeString([], { hour12: false, hour: '2-digit', minute: '2-digit', second: '2-digit' });
  };

  // Group consecutive duplicate events
  const groupedEvents = React.useMemo(() => {
    if (events.length === 0) return [];

    const result: { event: ServerEvent; count: number }[] = [];
    let current = { event: events[0]!, count: 1 };

    for (let i = 1; i < events.length; i++) {
      const next = events[i]!;
      // Compare type and data content strings
      const isSame = next.type === current.event.type &&
        JSON.stringify(next.data) === JSON.stringify(current.event.data);

      if (isSame) {
        current.count++;
      } else {
        result.push(current);
        current = { event: next, count: 1 };
      }
    }
    result.push(current);
    return result;
  }, [events]);

  return (
    <div className="glass-panel" style={{ padding: 24 }} data-testid="event-feed">
      <h3 className={styles.sectionTitle}>Event Feed</h3>
      {groupedEvents.length === 0 ? (
        <div data-testid="empty-events" style={{ textAlign: 'center', padding: 20, color: 'var(--text-tertiary)' }}>
          No events recorded
        </div>
      ) : (
        <div className={styles.eventList}>
          {groupedEvents.map(({ event, count }, i) => (
            <div key={i} className={styles.eventItem} data-testid="event-item" style={{ position: 'relative' }}>
              {count > 1 && (
                <div style={{
                  position: 'absolute',
                  top: 8,
                  right: 8,
                  background: 'var(--color-error)',
                  color: '#fff',
                  minWidth: '20px',
                  height: '20px',
                  display: 'flex',
                  alignItems: 'center',
                  justifyContent: 'center',
                  fontSize: '11px',
                  fontWeight: 700,
                  boxShadow: '0 2px 4px rgba(0,0,0,0.2)',
                  zIndex: 1,
                  padding: count > 99 ? '0 4px' : '0', // Adjust for 3 digits
                  borderRadius: count > 99 ? '10px' : '50%', // Lozenge for large numbers
                }}>
                  {count > 99 ? '99+' : count}
                </div>
              )}
              <div className={styles.eventHeader}>
                <Tag
                  bordered={false}
                  style={{
                    backgroundColor: 'rgba(255,255,255,0.05)',
                    color: eventColorMap[event.type] || 'var(--text-secondary)',
                    fontWeight: 600,
                    fontSize: 11
                  }}
                >
                  {event.type}
                </Tag>
                <span className={styles.eventTime}>{formatTime((event as any).timestamp)}</span>
              </div>
              <div className={styles.eventData}>
                {JSON.stringify(event.data)}
              </div>
            </div>
          ))}
        </div>
      )}
    </div>
  );
};
