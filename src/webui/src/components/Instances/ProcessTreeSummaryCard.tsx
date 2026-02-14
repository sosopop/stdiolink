import React from 'react';
import { Card, Statistic, Row, Col } from 'antd';
import type { ProcessTreeSummary } from '@/types/instance';

function formatBytes(bytes: number): string {
  if (bytes === 0) return '0 B';
  const units = ['B', 'KB', 'MB', 'GB', 'TB'];
  const i = Math.floor(Math.log(bytes) / Math.log(1024));
  return `${(bytes / Math.pow(1024, i)).toFixed(1)} ${units[i]}`;
}

interface ProcessTreeSummaryCardProps {
  summary: ProcessTreeSummary;
}

export const ProcessTreeSummaryCard: React.FC<ProcessTreeSummaryCardProps> = ({ summary }) => (
  <Card data-testid="process-tree-summary" size="small">
    <Row gutter={16}>
      <Col span={6}>
        <Statistic title="Total Processes" value={summary.totalProcesses} data-testid="stat-processes" />
      </Col>
      <Col span={6}>
        <Statistic title="Total CPU" value={summary.totalCpuPercent.toFixed(1)} suffix="%" data-testid="stat-cpu" />
      </Col>
      <Col span={6}>
        <Statistic title="Total Memory" value={formatBytes(summary.totalMemoryRssBytes)} data-testid="stat-memory" />
      </Col>
      <Col span={6}>
        <Statistic title="Total Threads" value={summary.totalThreads} data-testid="stat-threads" />
      </Col>
    </Row>
  </Card>
);
