import React, { useEffect } from 'react';
import { Typography, Switch, Space } from 'antd';
import { ExperimentOutlined } from '@ant-design/icons';
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
        <Space size={12}>
          <ExperimentOutlined style={{ fontSize: 24, color: 'var(--brand-primary)' }} />
          <div>
            <Title level={3} style={{ margin: 0 }}>DriverLab</Title>
            <Text type="secondary">Interactive driver debugging and protocol testing</Text>
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
      <div style={{ flex: 1, display: 'flex', gap: 24, minHeight: 0, overflow: 'hidden' }}>
        {/* Left panel: Controls */}
        <div
          data-testid="left-panel"
          style={{
            width: 360,
            display: 'flex',
            flexDirection: 'column',
            gap: 20,
            overflow: 'auto',
          }}
        >
          <div className="glass-panel" style={{ padding: 20 }}>
            <Title level={5} style={{ marginTop: 0, marginBottom: 16 }}>Session Config</Title>
            <ConnectionPanel
              drivers={drivers}
              status={connection.status}
              onConnect={connect}
              onDisconnect={disconnect}
            />
          </div>

          <div className="glass-panel" style={{ padding: 20, flex: 1 }}>
            <Title level={5} style={{ marginTop: 0, marginBottom: 16 }}>Command Palette</Title>
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

        {/* Right panel: Stream */}
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
          
          <div style={{ flex: 1, overflow: 'hidden', display: 'flex', flexDirection: 'column', background: 'rgba(0,0,0,0.2)' }}>
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
  );
};

export default DriverLabPage;
