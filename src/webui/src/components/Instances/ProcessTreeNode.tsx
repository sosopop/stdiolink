import React, { useState } from 'react';
import type { ProcessTreeNode as TreeNodeType } from '@/types/instance';

function formatBytes(bytes: number): string {
  if (bytes === 0) return '0 B';
  const units = ['B', 'KB', 'MB', 'GB', 'TB'];
  const i = Math.floor(Math.log(bytes) / Math.log(1024));
  return `${(bytes / Math.pow(1024, i)).toFixed(1)} ${units[i]}`;
}

function cpuColor(percent: number): string {
  if (percent > 80) return '#ff4d4f';
  if (percent > 50) return '#faad14';
  return '#52c41a';
}

interface ProcessTreeNodeProps {
  node: TreeNodeType;
  level: number;
}

export const ProcessTreeNodeComponent: React.FC<ProcessTreeNodeProps> = ({ node, level }) => {
  const [collapsed, setCollapsed] = useState(false);
  const hasChildren = node.children && node.children.length > 0;

  return (
    <div data-testid={`tree-node-${node.pid}`}>
      <div
        style={{
          display: 'flex',
          alignItems: 'center',
          gap: 8,
          paddingLeft: level * 24,
          padding: '4px 8px 4px ' + (level * 24 + 8) + 'px',
          borderLeft: node.resources.cpuPercent > 80 ? '3px solid #ff4d4f' : '3px solid transparent',
          fontFamily: 'JetBrains Mono, monospace',
          fontSize: 12,
        }}
      >
        {hasChildren ? (
          <button
            data-testid={`toggle-${node.pid}`}
            onClick={() => setCollapsed(!collapsed)}
            style={{ background: 'none', border: 'none', cursor: 'pointer', padding: 0, color: 'inherit', fontSize: 12 }}
          >
            {collapsed ? '▶' : '▼'}
          </button>
        ) : (
          <span style={{ width: 12, display: 'inline-block' }} />
        )}
        <span data-testid={`pid-${node.pid}`} style={{ color: '#8c8c8c', minWidth: 60 }}>PID {node.pid}</span>
        <span style={{ minWidth: 120 }}>{node.name}</span>
        <span data-testid={`cpu-${node.pid}`} style={{ color: cpuColor(node.resources.cpuPercent), minWidth: 80 }}>
          CPU: {node.resources.cpuPercent.toFixed(1)}%
        </span>
        <span style={{ minWidth: 100 }}>Mem: {formatBytes(node.resources.memoryRssBytes)}</span>
        <span>Threads: {node.resources.threadCount}</span>
      </div>
      {hasChildren && !collapsed && (
        <div data-testid={`children-${node.pid}`}>
          {node.children.map((child) => (
            <ProcessTreeNodeComponent key={child.pid} node={child} level={level + 1} />
          ))}
        </div>
      )}
    </div>
  );
};
