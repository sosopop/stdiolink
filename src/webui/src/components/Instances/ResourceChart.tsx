import React from 'react';
import { Empty } from 'antd';
import { AreaChart, Area, XAxis, YAxis, Tooltip, ResponsiveContainer, CartesianGrid } from 'recharts';
import type { ResourceSample } from '@/stores/useInstancesStore';

function formatTime(ts: number): string {
  const d = new Date(ts);
  return `${d.getHours().toString().padStart(2, '0')}:${d.getMinutes().toString().padStart(2, '0')}:${d.getSeconds().toString().padStart(2, '0')}`;
}

interface ResourceChartProps {
  data: ResourceSample[];
}

export const ResourceChart: React.FC<ResourceChartProps> = ({ data }) => {
  if (data.length === 0) {
    return <Empty description="No resource data yet" data-testid="empty-chart" />;
  }

  const chartData = data.map((s) => ({
    time: formatTime(s.timestamp),
    cpu: Number(s.cpuPercent.toFixed(1)),
    memoryMB: Number((s.memoryRssBytes / (1024 * 1024)).toFixed(1)),
  }));

  return (
    <div data-testid="resource-chart">
      <div style={{ marginBottom: 24 }}>
        <h4 style={{ margin: '0 0 8px' }}>CPU Usage (%)</h4>
        <ResponsiveContainer width="100%" height={200}>
          <AreaChart data={chartData}>
            <defs>
              <linearGradient id="cpuGrad" x1="0" y1="0" x2="0" y2="1">
                <stop offset="5%" stopColor="#6366F1" stopOpacity={0.3} />
                <stop offset="95%" stopColor="#6366F1" stopOpacity={0} />
              </linearGradient>
            </defs>
            <CartesianGrid stroke="none" />
            <XAxis dataKey="time" tick={{ fontSize: 10 }} axisLine={false} />
            <YAxis domain={[0, 100]} tick={{ fontSize: 10 }} axisLine={false} />
            <Tooltip />
            <Area type="monotone" dataKey="cpu" stroke="#6366F1" fill="url(#cpuGrad)" isAnimationActive={true} />
          </AreaChart>
        </ResponsiveContainer>
      </div>
      <div>
        <h4 style={{ margin: '0 0 8px' }}>Memory Usage (MB)</h4>
        <ResponsiveContainer width="100%" height={200}>
          <AreaChart data={chartData}>
            <defs>
              <linearGradient id="memGrad" x1="0" y1="0" x2="0" y2="1">
                <stop offset="5%" stopColor="#10B981" stopOpacity={0.3} />
                <stop offset="95%" stopColor="#10B981" stopOpacity={0} />
              </linearGradient>
            </defs>
            <CartesianGrid stroke="none" />
            <XAxis dataKey="time" tick={{ fontSize: 10 }} axisLine={false} />
            <YAxis tick={{ fontSize: 10 }} axisLine={false} />
            <Tooltip />
            <Area type="monotone" dataKey="memoryMB" stroke="#10B981" fill="url(#memGrad)" isAnimationActive={true} />
          </AreaChart>
        </ResponsiveContainer>
      </div>
    </div>
  );
};
