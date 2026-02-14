import React from 'react';
import { Table, Empty } from 'antd';
import type { FieldMeta } from '@/types/service';

interface ParamsTableProps {
  params: FieldMeta[];
}

function flattenParams(params: FieldMeta[], prefix = '', depth = 0): Array<FieldMeta & { fullPath: string; depth: number }> {
  if (depth > 5) return [];
  const result: Array<FieldMeta & { fullPath: string; depth: number }> = [];
  for (const p of params) {
    const fullPath = prefix ? `${prefix}.${p.name}` : p.name;
    result.push({ ...p, fullPath, depth });
    if (p.type === 'object' && p.fields) {
      result.push(...flattenParams(p.fields, fullPath, depth + 1));
    }
  }
  return result;
}

export const ParamsTable: React.FC<ParamsTableProps> = ({ params }) => {
  if (!params || params.length === 0) {
    return <Empty description="No parameters" data-testid="empty-params" image={Empty.PRESENTED_IMAGE_SIMPLE} />;
  }

  const flat = flattenParams(params);

  const columns = [
    {
      title: 'Name',
      dataIndex: 'fullPath',
      key: 'name',
      width: 180,
      render: (path: string, record: any) => (
        <span style={{ paddingLeft: record.depth * 16 }} data-testid={`param-${path}`}>
          {record.name}
        </span>
      ),
    },
    { title: 'Type', dataIndex: 'type', key: 'type', width: 100 },
    {
      title: 'Required',
      dataIndex: 'required',
      key: 'required',
      width: 80,
      render: (v: boolean) => v ? '✓' : '',
    },
    {
      title: 'Default',
      key: 'default',
      width: 120,
      render: (_: unknown, record: FieldMeta) => {
        if (record.default !== undefined) return String(record.default);
        return '--';
      },
    },
    {
      title: 'Description',
      key: 'description',
      render: (_: unknown, record: FieldMeta) => {
        const parts: string[] = [];
        if (record.description) parts.push(record.description);
        if (record.min !== undefined) parts.push(`min: ${record.min}`);
        if (record.max !== undefined) parts.push(`max: ${record.max}`);
        if (record.pattern) parts.push(`pattern: ${record.pattern}`);
        return parts.join(' | ') || '--';
      },
    },
  ];

  return (
    <Table
      data-testid="params-table"
      dataSource={flat}
      columns={columns}
      rowKey="fullPath"
      size="small"
      pagination={false}
    />
  );
};
