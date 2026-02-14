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

  return (
    <div className="glass-panel" style={{ padding: 24 }} data-testid="event-feed">
      <h3 className={styles.sectionTitle}>Event Feed</h3>
      {events.length === 0 ? (
        <div data-testid="empty-events" style={{ textAlign: 'center', padding: 20, color: 'var(--text-tertiary)' }}>
          No events recorded
        </div>
      ) : (
        <div className={styles.eventList}>
          {events.map((event, i) => (
            <div key={i} className={styles.eventItem} data-testid="event-item">
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
