import React from 'react';
import { Card, Statistic, Row, Col } from 'antd';
import type { ProcessInfo } from '@/types/instance';

function formatBytes(bytes: number): string {
  if (bytes === 0) return '0 B';
  const units = ['B', 'KB', 'MB', 'GB', 'TB'];
  const i = Math.floor(Math.log(bytes) / Math.log(1024));
  return `${(bytes / Math.pow(1024, i)).toFixed(1)} ${units[i]}`;
}

function formatUptime(seconds: number): string {
  const h = Math.floor(seconds / 3600);
  const m = Math.floor((seconds % 3600) / 60);
  const s = Math.floor(seconds % 60);
  if (h > 0) return `${h}h ${m}m ${s}s`;
  if (m > 0) return `${m}m ${s}s`;
  return `${s}s`;
}

interface ResourceMetricCardsProps {
  processes: ProcessInfo[];
}

export const ResourceMetricCards: React.FC<ResourceMetricCardsProps> = ({ processes }) => {
  if (processes.length === 0) {
    return (
      <div data-testid="resource-metrics-empty">
        <Row gutter={[16, 16]}>
          <Col span={4}><Card size="small"><Statistic title="CPU" value="--" /></Card></Col>
          <Col span={4}><Card size="small"><Statistic title="Memory" value="--" /></Card></Col>
          <Col span={4}><Card size="small"><Statistic title="Threads" value="--" /></Card></Col>
          <Col span={4}><Card size="small"><Statistic title="I/O Read" value="--" /></Card></Col>
          <Col span={4}><Card size="small"><Statistic title="I/O Write" value="--" /></Card></Col>
          <Col span={4}><Card size="small"><Statistic title="Uptime" value="--" /></Card></Col>
        </Row>
      </div>
    );
  }

  const totalCpu = processes.reduce((s, p) => s + p.cpuPercent, 0);
  const totalMem = processes.reduce((s, p) => s + p.memoryRssBytes, 0);
  const totalThreads = processes.reduce((s, p) => s + p.threadCount, 0);
  const totalIoRead = processes.reduce((s, p) => s + p.ioReadBytes, 0);
  const totalIoWrite = processes.reduce((s, p) => s + p.ioWriteBytes, 0);
  const maxUptime = Math.max(...processes.map((p) => p.uptimeSeconds));

  return (
    <div data-testid="resource-metrics">
      <Row gutter={[16, 16]}>
        <Col span={4}>
          <Card size="small"><Statistic title="CPU" value={totalCpu.toFixed(1)} suffix="%" data-testid="metric-cpu" /></Card>
        </Col>
        <Col span={4}>
          <Card size="small"><Statistic title="Memory" value={formatBytes(totalMem)} data-testid="metric-memory" /></Card>
        </Col>
        <Col span={4}>
          <Card size="small"><Statistic title="Threads" value={totalThreads} data-testid="metric-threads" /></Card>
        </Col>
        <Col span={4}>
          <Card size="small"><Statistic title="I/O Read" value={formatBytes(totalIoRead)} data-testid="metric-io-read" /></Card>
        </Col>
        <Col span={4}>
          <Card size="small"><Statistic title="I/O Write" value={formatBytes(totalIoWrite)} data-testid="metric-io-write" /></Card>
        </Col>
        <Col span={4}>
          <Card size="small"><Statistic title="Uptime" value={formatUptime(maxUptime)} data-testid="metric-uptime" /></Card>
        </Col>
      </Row>
    </div>
  );
};
