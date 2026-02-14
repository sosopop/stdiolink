import React from 'react';
import { Card, Button, Tag, Space } from 'antd';
import { PlayCircleOutlined } from '@ant-design/icons';
import type { CommandMeta } from '@/types/driver';
import { ParamsTable } from './ParamsTable';

interface CommandCardProps {
  command: CommandMeta;
  driverId: string;
  onTest: (commandName: string) => void;
}

export const CommandCard: React.FC<CommandCardProps> = ({ command, onTest }) => (
  <Card
    data-testid={`command-${command.name}`}
    size="small"
    title={
      <Space>
        <span style={{ fontFamily: 'JetBrains Mono, monospace' }}>{command.name}</span>
        {command.description && <span style={{ color: '#8c8c8c', fontWeight: 400 }}>— {command.description}</span>}
      </Space>
    }
    extra={
      <Button
        size="small"
        icon={<PlayCircleOutlined />}
        onClick={() => onTest(command.name)}
        data-testid={`test-cmd-${command.name}`}
      >
        Test
      </Button>
    }
    style={{ marginBottom: 12 }}
  >
    <ParamsTable params={command.params} />
    <div style={{ marginTop: 8 }}>
      <span style={{ fontWeight: 500 }}>Returns: </span>
      <Tag data-testid={`return-type-${command.name}`}>{command.returns?.type || 'void'}</Tag>
      {command.returns?.description && <span style={{ color: '#8c8c8c' }}>{command.returns.description}</span>}
    </div>
  </Card>
);
