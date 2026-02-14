import React, { useRef, useEffect, useMemo, useState } from 'react';
import { useTranslation } from 'react-i18next';
import { Empty, Input, Select, Button, Tooltip } from 'antd';
import { ArrowDownOutlined, PauseCircleOutlined, PlayCircleOutlined } from '@ant-design/icons';
import styles from './LogViewer.module.css';

const { Search } = Input;

interface LogViewerProps {
  lines: string[];
  loading?: boolean;
}

const LOG_LEVELS = ['ALL', 'ERROR', 'WARN', 'INFO', 'DEBUG'] as const;

function parseLine(line: string): { timestamp?: string; level: string; message: string } {
  // Try to extract timestamp: 2023-01-01 12:00:00 or [2023-01-01...]
  const timeRegex = /^\[?(\d{4}-\d{2}-\d{2}[T ]\d{2}:\d{2}:\d{2}(?:\.\d+)?)\]?\s*/;
  const match = line.match(timeRegex);

  let timestamp = undefined;
  let message = line;

  if (match) {
    timestamp = match[1];
    message = line.substring(match[0].length);
  }

  // Detect level in the remaining message
  let level = 'INFO';
  const upper = message.toUpperCase();
  if (upper.includes('ERROR') || upper.includes('[E]')) level = 'ERROR';
  else if (upper.includes('WARN') || upper.includes('[W]')) level = 'WARN';
  else if (upper.includes('INFO') || upper.includes('[I]')) level = 'INFO';
  else if (upper.includes('DEBUG') || upper.includes('[D]')) level = 'DEBUG';

  return { timestamp, level, message };
}

function getLevelColor(level: string): string {
  switch (level) {
    case 'ERROR': return '#f87171'; // Red-400
    case 'WARN': return '#fbbf24';  // Amber-400
    case 'DEBUG': return '#9ca3af'; // Gray-400
    case 'INFO': return '#60a5fa';  // Blue-400
    default: return '#9ca3af';
  }
}

export const LogViewer: React.FC<LogViewerProps> = ({ lines, loading }) => {
  const { t } = useTranslation();
  const containerRef = useRef<HTMLDivElement>(null);
  const [filter, setFilter] = useState('ALL');
  const [search, setSearch] = useState('');
  const [autoScroll, setAutoScroll] = useState(true);

  const filtered = useMemo(() => {
    return lines.filter((line) => {
      const { level } = parseLine(line);
      if (filter !== 'ALL' && level !== filter) return false;
      if (search && !line.toLowerCase().includes(search.toLowerCase())) return false;
      return true;
    });
  }, [lines, filter, search]);

  useEffect(() => {
    if (containerRef.current && autoScroll) {
      containerRef.current.scrollTop = containerRef.current.scrollHeight;
    }
  }, [filtered, autoScroll]);

  const handleScroll = () => {
    if (containerRef.current) {
      const { scrollTop, scrollHeight, clientHeight } = containerRef.current;
      const isAtBottom = scrollHeight - scrollTop - clientHeight < 50;
      if (!isAtBottom && autoScroll) {
        setAutoScroll(false);
      } else if (isAtBottom && !autoScroll) {
        setAutoScroll(true);
      }
    }
  };

  if (!loading && lines.length === 0) {
    return (
      <div className={styles.logWindow} style={{ display: 'grid', placeItems: 'center', height: 400 }}>
        <Empty description={<span style={{ color: 'var(--text-tertiary)' }}>{t('log_viewer.no_logs')}</span>} />
      </div>
    );
  }

  return (
    <div className={styles.container}>
      <div className={styles.toolbar}>
        <div className={styles.filterGroup}>
          <Select
            value={filter}
            onChange={setFilter}
            options={LOG_LEVELS.map((l) => ({ label: l === 'ALL' ? t('log_viewer.level_all') : l, value: l }))}
            style={{ width: 120 }}
            size="small"
            variant="borderless"
            className="glass-input" // Assuming global class exists or will fallback
          />
          <Search
            placeholder={t('log_viewer.search_placeholder')}
            allowClear
            onChange={(e) => setSearch(e.target.value)}
            style={{ width: 240 }}
            size="small"
            variant="borderless"
          />
        </div>
        <div className={styles.filterGroup}>
          <Tooltip title={autoScroll ? t('log_viewer.pause_scroll') : t('log_viewer.resume_scroll')}>
            <Button
              type="text"
              size="small"
              icon={autoScroll ? <PauseCircleOutlined /> : <PlayCircleOutlined />}
              onClick={() => setAutoScroll(!autoScroll)}
              style={{ color: autoScroll ? 'var(--color-success)' : 'var(--text-tertiary)' }}
            />
          </Tooltip>
        </div>
      </div>

      <div
        className={styles.logWindow}
        ref={containerRef}
        onScroll={handleScroll}
      >
        {filtered.map((line, i) => {
          const { timestamp, level, message } = parseLine(line);
          const color = getLevelColor(level);

          return (
            <div key={i} className={styles.logLine}>
              {timestamp && <span className={styles.timestamp}>{timestamp}</span>}
              <span className={styles.levelTag} style={{ color, border: `1px solid ${color}40`, background: `${color}10` }}>
                {level}
              </span>
              <span className={styles.message} style={{ color: level === 'ERROR' ? '#fca5a5' : 'inherit' }}>
                {message}
              </span>
            </div>
          );
        })}
        {!autoScroll && (
          <div
            style={{
              position: 'absolute',
              bottom: 20,
              right: 36,
              zIndex: 10
            }}
          >
            <Button
              type="primary"
              shape="circle"
              icon={<ArrowDownOutlined />}
              onClick={() => {
                setAutoScroll(true);
                containerRef.current?.scrollTo({ top: containerRef.current.scrollHeight, behavior: 'smooth' });
              }}
            />
          </div>
        )}
      </div>
    </div>
  );
};
