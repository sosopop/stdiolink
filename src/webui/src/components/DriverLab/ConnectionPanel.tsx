import React from 'react';
import { Select, Radio, Input, Button, Form, Space } from 'antd';
import { LinkOutlined, DisconnectOutlined, LoadingOutlined } from '@ant-design/icons';
import type { DriverListItem } from '@/types/driver';
import type { ConnectionStatus } from '@/stores/useDriverLabStore';

interface ConnectionPanelProps {
  drivers: DriverListItem[];
  status: ConnectionStatus;
  initialDriverId?: string | null;
  onConnect: (driverId: string, runMode: 'oneshot' | 'keepalive', args?: string[]) => void;
  onDisconnect: () => void;
  layout?: 'horizontal' | 'vertical';
}

export const ConnectionPanel: React.FC<ConnectionPanelProps> = ({
  drivers,
  status,
  initialDriverId,
  onConnect,
  onDisconnect,
  layout = 'vertical',
}) => {
  const [driverId, setDriverId] = React.useState<string | null>(null);
  const [runMode, setRunMode] = React.useState<'oneshot' | 'keepalive'>('oneshot');

  // Sync from URL query param when not connected
  React.useEffect(() => {
    if (initialDriverId && status === 'disconnected') {
      setDriverId(initialDriverId);
    }
  }, [initialDriverId, status]);
  const [args, setArgs] = React.useState('');

  const connected = status === 'connected';
  const connecting = status === 'connecting';
  const disabled = connected || connecting;

  const handleConnect = () => {
    if (!driverId) return;
    const argList = args.trim() ? args.split(',').map((s) => s.trim()) : undefined;
    onConnect(driverId, runMode, argList);
  };

  return (
    <div data-testid="connection-panel">
      <Form layout={layout === 'horizontal' ? 'inline' : 'vertical'} style={layout === 'horizontal' ? { alignItems: 'center', gap: 24 } : undefined}>
        <Form.Item label={<span style={{ fontWeight: 500, fontSize: 13 }}>Target Driver</span>} style={layout === 'horizontal' ? { marginBottom: 0, flex: 1, minWidth: 240 } : undefined}>
          <Select
            value={driverId}
            onChange={setDriverId}
            disabled={disabled}
            placeholder="Select a driver"
            data-testid="driver-select"
            options={drivers.map((d) => ({
              label: d.name || d.id,
              value: d.id,
            }))}
          />
        </Form.Item>
        <Form.Item label={<span style={{ fontWeight: 500, fontSize: 13 }}>Mode</span>} style={layout === 'horizontal' ? { marginBottom: 0 } : undefined}>
          <Radio.Group
            value={runMode}
            onChange={(e) => setRunMode(e.target.value)}
            disabled={disabled}
            data-testid="runmode-radio"
          >
            <Radio value="oneshot">OneShot</Radio>
            <Radio value="keepalive">KeepAlive</Radio>
          </Radio.Group>
        </Form.Item>
        <Form.Item label={<span style={{ fontWeight: 500, fontSize: 13 }}>Arguments</span>} style={layout === 'horizontal' ? { marginBottom: 0, flex: 1.5, minWidth: 200 } : undefined}>
          <Input
            value={args}
            onChange={(e) => setArgs(e.target.value)}
            disabled={disabled}
            placeholder="arg1, arg2, ..."
            data-testid="args-input"
          />
        </Form.Item>
        <Form.Item style={layout === 'horizontal' ? { marginBottom: 0 } : undefined}>
          <Space>
            {!connected ? (
              <Button
                type="primary"
                icon={connecting ? <LoadingOutlined /> : <LinkOutlined />}
                onClick={handleConnect}
                disabled={!driverId || connecting}
                loading={connecting}
                data-testid="connect-btn"
              >
                {connecting ? 'Connecting...' : 'Connect'}
              </Button>
            ) : (
              <Button
                danger
                icon={<DisconnectOutlined />}
                onClick={onDisconnect}
                data-testid="disconnect-btn"
              >
                Disconnect
              </Button>
            )}
          </Space>
        </Form.Item>
      </Form>
    </div>
  );
};
