import React, { useEffect } from 'react';
import { Typography, Switch, Space } from 'antd';
import { ExperimentOutlined } from '@ant-design/icons';
import { useSearchParams } from 'react-router-dom';
import { useDriverLabStore } from '@/stores/useDriverLabStore';
import { useDriversStore } from '@/stores/useDriversStore';
import { ConnectionPanel } from '@/components/DriverLab/ConnectionPanel';
import { CommandPanel } from '@/components/DriverLab/CommandPanel';
import { MessageStream } from '@/components/DriverLab/MessageStream';
import { MessageToolbar } from '@/components/DriverLab/MessageToolbar';
import { StatusBar } from '@/components/DriverLab/StatusBar';

const { Title, Text } = Typography;

const statusColorMap: Record<string, string> = {
  connected: 'var(--color-success)',
  connecting: 'var(--color-info)',
  disconnected: 'var(--text-tertiary)',
  error: 'var(--color-error)',
};

export const DriverLabPage: React.FC = () => {
  const [searchParams] = useSearchParams();
  const urlDriverId = searchParams.get('driverId');

  const drivers = useDriversStore((s) => s.drivers);
  const fetchDrivers = useDriversStore((s) => s.fetchDrivers);

  const connection = useDriverLabStore((s) => s.connection);
  const messages = useDriverLabStore((s) => s.messages);
  const commands = useDriverLabStore((s) => s.commands);
  const selectedCommand = useDriverLabStore((s) => s.selectedCommand);
  const commandParams = useDriverLabStore((s) => s.commandParams);
  const executing = useDriverLabStore((s) => s.executing);
  const autoScroll = useDriverLabStore((s) => s.autoScroll);

  const connect = useDriverLabStore((s) => s.connect);
  const disconnect = useDriverLabStore((s) => s.disconnect);
  const execCommand = useDriverLabStore((s) => s.execCommand);
  const cancelCommand = useDriverLabStore((s) => s.cancelCommand);
  const selectCommand = useDriverLabStore((s) => s.selectCommand);
  const setCommandParams = useDriverLabStore((s) => s.setCommandParams);
  const clearMessages = useDriverLabStore((s) => s.clearMessages);
  const toggleAutoScroll = useDriverLabStore((s) => s.toggleAutoScroll);

  useEffect(() => {
    fetchDrivers();
  }, [fetchDrivers]);

  const handleToggleMessage = (id: string) => {
    useDriverLabStore.setState((s) => ({
      messages: s.messages.map((m) =>
        m.id === id ? { ...m, expanded: !m.expanded } : m,
      ),
    }));
  };

  const handleExec = () => {
    if (selectedCommand) {
      execCommand(selectedCommand, commandParams);
    }
  };

  return (
    <div data-testid="driverlab-page" style={{ display: 'flex', flexDirection: 'column', height: 'calc(100vh - 120px)' }}>
      {/* Header */}
      <div style={{ display: 'flex', alignItems: 'center', justifyContent: 'space-between', marginBottom: 24 }}>
        <Space size={16} align="center">
          <div style={{ display: 'flex', alignItems: 'center', justifyContent: 'center', width: 40, height: 40, background: 'linear-gradient(135deg, rgba(99,102,241,0.1), rgba(168,85,247,0.1))', borderRadius: 12, border: '1px solid rgba(99,102,241,0.2)' }}>
            <ExperimentOutlined style={{ fontSize: 22, color: 'var(--brand-primary)' }} />
          </div>
          <div>
            <Title level={4} style={{ margin: 0, fontWeight: 600, letterSpacing: '-0.01em' }}>Driver Lab</Title>
            <Text type="secondary" style={{ fontSize: 13 }}>Interactive protocol debugger & playground</Text>
          </div>
        </Space>

        <div style={{ display: 'flex', alignItems: 'center', gap: 12, padding: '6px 16px', background: 'rgba(255,255,255,0.03)', borderRadius: 100, border: '1px solid var(--surface-border)' }}>
          <div
            style={{
              width: 8, height: 8, borderRadius: '50%',
              backgroundColor: statusColorMap[connection.status] || 'var(--text-tertiary)',
              boxShadow: connection.status === 'connected' ? '0 0 8px var(--color-success)' : 'none'
            }}
          />
          <Text strong style={{ fontSize: 13, textTransform: 'uppercase', letterSpacing: '0.5px' }} data-testid="header-status">
            {connection.status}
          </Text>
        </div>
      </div>

      {/* Main content */}
      <div style={{ flex: 1, display: 'flex', flexDirection: 'column', gap: 16, minHeight: 0, overflow: 'hidden' }}>

        {/* Top: Session Config */}
        <div className="glass-panel" style={{ padding: 16, flexShrink: 0 }}>
          <ConnectionPanel
            drivers={drivers}
            status={connection.status}
            initialDriverId={urlDriverId}
            onConnect={connect}
            onDisconnect={disconnect}
            layout="horizontal"
          />
        </div>

        {/* Bottom: Split View */}
        <div style={{ flex: 1, display: 'flex', gap: 16, minHeight: 0, overflow: 'hidden' }}>

          {/* Left: Command Palette */}
          <div className="glass-panel" style={{ width: 320, display: 'flex', flexDirection: 'column', padding: 16 }}>
            <div style={{ marginBottom: 12 }}>
              <Title level={5} style={{ margin: 0 }}>Command Palette</Title>
            </div>
            <div style={{ flex: 1, overflow: 'hidden' }}>
              <CommandPanel
                commands={commands}
                selectedCommand={selectedCommand}
                commandParams={commandParams}
                executing={executing}
                connected={connection.status === 'connected'}
                driverId={connection.driverId}
                onSelectCommand={selectCommand}
                onParamsChange={setCommandParams}
                onExec={handleExec}
                onCancel={cancelCommand}
              />
            </div>
          </div>

          {/* Right: Stream */}
          <div
            data-testid="right-panel"
            className="glass-panel"
            style={{ flex: 1, display: 'flex', flexDirection: 'column', minWidth: 0, overflow: 'hidden' }}
          >
            <div style={{ padding: '12px 20px', display: 'flex', alignItems: 'center', justifyContent: 'space-between', borderBottom: '1px solid var(--surface-border)' }}>
              <Text strong style={{ textTransform: 'uppercase', fontSize: 12, letterSpacing: '1px', opacity: 0.8 }}>Protocol Stream</Text>
              <Space size={16}>
                <Space size={8}>
                  <Text type="secondary" style={{ fontSize: 12 }}>Auto-scroll</Text>
                  <Switch
                    size="small"
                    checked={autoScroll}
                    onChange={toggleAutoScroll}
                    data-testid="autoscroll-toggle"
                  />
                </Space>
                <div style={{ width: 1, height: 16, background: 'var(--surface-border)' }} />
                <MessageToolbar
                  messages={messages}
                  driverId={connection.driverId}
                  onClear={clearMessages}
                />
              </Space>
            </div>

            <div style={{ flex: 1, overflow: 'hidden', display: 'flex', flexDirection: 'column', background: 'var(--surface-hover)' }}>
              <MessageStream
                messages={messages}
                autoScroll={autoScroll}
                onToggleMessage={handleToggleMessage}
              />
            </div>

            <StatusBar connection={connection} />
          </div>

        </div>
      </div>
    </div>
  );
};

export default DriverLabPage;
