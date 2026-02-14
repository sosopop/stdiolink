import React, { useRef, useEffect, useMemo, useState } from 'react';
import { Empty, Input, Tag, Select } from 'antd';

const { Search } = Input;

interface LogViewerProps {
  lines: string[];
  loading?: boolean;
}

const LOG_LEVELS = ['ALL', 'ERROR', 'WARN', 'INFO', 'DEBUG'] as const;

function detectLevel(line: string): string {
  const upper = line.toUpperCase();
  if (upper.includes('ERROR') || upper.includes('[E]')) return 'ERROR';
  if (upper.includes('WARN') || upper.includes('[W]')) return 'WARN';
  if (upper.includes('INFO') || upper.includes('[I]')) return 'INFO';
  if (upper.includes('DEBUG') || upper.includes('[D]')) return 'DEBUG';
  return 'INFO';
}

function levelColor(level: string): string {
  switch (level) {
    case 'ERROR': return '#ff4d4f';
    case 'WARN': return '#faad14';
    case 'DEBUG': return '#8c8c8c';
    default: return '#d9d9d9';
  }
}

export const LogViewer: React.FC<LogViewerProps> = ({ lines, loading }) => {
  const containerRef = useRef<HTMLDivElement>(null);
  const [filter, setFilter] = useState('ALL');
  const [search, setSearch] = useState('');

  const filtered = useMemo(() => {
    return lines.filter((line) => {
      if (filter !== 'ALL' && detectLevel(line) !== filter) return false;
      if (search && !line.toLowerCase().includes(search.toLowerCase())) return false;
      return true;
    });
  }, [lines, filter, search]);

  useEffect(() => {
    if (containerRef.current) {
      containerRef.current.scrollTop = containerRef.current.scrollHeight;
    }
  }, [filtered]);

  if (!loading && lines.length === 0) {
    return <Empty description="No logs available" data-testid="empty-logs" />;
  }

  return (
    <div data-testid="log-viewer">
      <div style={{ display: 'flex', gap: 8, marginBottom: 8 }}>
        <Select
          value={filter}
          onChange={setFilter}
          options={LOG_LEVELS.map((l) => ({ label: l, value: l }))}
          style={{ width: 120 }}
          data-testid="log-level-filter"
        />
        <Search
          placeholder="Search logs..."
          allowClear
          onChange={(e) => setSearch(e.target.value)}
          style={{ width: 240 }}
          data-testid="log-search"
        />
      </div>
      <div
        ref={containerRef}
        data-testid="log-container"
        style={{
          height: 400,
          overflow: 'auto',
          background: 'rgba(0,0,0,0.3)',
          borderRadius: 6,
          padding: 12,
          fontFamily: 'JetBrains Mono, monospace',
          fontSize: 12,
          lineHeight: 1.6,
        }}
      >
        {filtered.map((line, i) => {
          const level = detectLevel(line);
          return (
            <div key={i} data-testid="log-line" style={{ whiteSpace: 'pre-wrap', wordBreak: 'break-all' }}>
              <Tag color={levelColor(level)} style={{ fontSize: 10, marginRight: 6 }}>{level}</Tag>
              <span>{line}</span>
            </div>
          );
        })}
      </div>
    </div>
  );
};
