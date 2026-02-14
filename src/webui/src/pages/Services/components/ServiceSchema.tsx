import React from 'react';
import { Table, Tag, Empty, Typography } from 'antd';
import type { FieldMeta } from '@/types/service';

const { Text } = Typography;

interface ServiceSchemaProps {
  fields: FieldMeta[];
  requiredKeys?: string[];
}

function flattenFields(fields: FieldMeta[], prefix = '', parentRequired: string[] = []): Array<FieldMeta & { fullPath: string; isRequired: boolean }> {
  const rows: Array<FieldMeta & { fullPath: string; isRequired: boolean }> = [];
  for (const f of fields) {
    const fullPath = prefix ? `${prefix}.${f.name}` : f.name;
    const isRequired = parentRequired.includes(f.name) || f.required === true;
    rows.push({ ...f, fullPath, isRequired });
    if (f.type === 'object' && f.fields) {
      rows.push(...flattenFields(f.fields, fullPath, f.requiredKeys ?? []));
    }
  }
  return rows;
}

export const ServiceSchema: React.FC<ServiceSchemaProps> = ({ fields, requiredKeys }) => {
  if (!fields || fields.length === 0) {
    return <Empty description="No config schema defined" data-testid="no-schema" />;
  }

  const rows = flattenFields(fields, '', requiredKeys ?? []);

  const columns = [
    {
      title: 'Field',
      dataIndex: 'fullPath',
      key: 'fullPath',
      render: (v: string) => <Text code>{v}</Text>,
    },
    {
      title: 'Type',
      dataIndex: 'type',
      key: 'type',
      render: (v: string) => <Tag>{v}</Tag>,
    },
    {
      title: 'Required',
      dataIndex: 'isRequired',
      key: 'isRequired',
      render: (v: boolean) => (v ? <Tag color="red">required</Tag> : <Tag>optional</Tag>),
    },
    {
      title: 'Default',
      dataIndex: 'default',
      key: 'default',
      render: (v: unknown) => (v !== undefined ? <Text code>{JSON.stringify(v)}</Text> : '—'),
    },
    {
      title: 'Description',
      dataIndex: 'description',
      key: 'description',
    },
  ];

  return (
    <div data-testid="service-schema">
      <Table
        dataSource={rows}
        columns={columns}
        rowKey="fullPath"
        pagination={false}
        size="small"
      />
    </div>
  );
};
