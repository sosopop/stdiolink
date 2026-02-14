import React, { useEffect } from 'react';
import { Typography, Tag, Switch, Space } from 'antd';
import { useDriverLabStore } from '@/stores/useDriverLabStore';
import { useDriversStore } from '@/stores/useDriversStore';
import { ConnectionPanel } from '@/components/DriverLab/ConnectionPanel';
import { CommandPanel } from '@/components/DriverLab/CommandPanel';
import { MessageStream } from '@/components/DriverLab/MessageStream';
import { MessageToolbar } from '@/components/DriverLab/MessageToolbar';
import { StatusBar } from '@/components/DriverLab/StatusBar';

const statusColorMap: Record<string, string> = {
  connected: 'green',
  connecting: 'blue',
  disconnected: 'default',
  error: 'red',
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
    <div data-testid="driverlab-page" style={{ display: 'flex', flexDirection: 'column', height: '100%' }}>
      {/* Header */}
      <div style={{ padding: '12px 16px', display: 'flex', alignItems: 'center', justifyContent: 'space-between', borderBottom: '1px solid var(--border-secondary, #303030)' }}>
        <Typography.Title level={4} style={{ margin: 0 }}>
          DriverLab
        </Typography.Title>
        <Tag color={statusColorMap[connection.status]} data-testid="header-status">
          {connection.status}
        </Tag>
      </div>

      {/* Main content */}
      <div style={{ flex: 1, display: 'flex', minHeight: 0, overflow: 'hidden' }}>
        {/* Left panel */}
        <div
          data-testid="left-panel"
          style={{
            width: 320,
            minWidth: 280,
            borderRight: '1px solid var(--border-secondary, #303030)',
            overflow: 'auto',
            padding: 16,
          }}
        >
          <Typography.Text strong style={{ display: 'block', marginBottom: 8 }}>
            Connection
          </Typography.Text>
          <ConnectionPanel
            drivers={drivers}
            status={connection.status}
            onConnect={connect}
            onDisconnect={disconnect}
          />

          <div style={{ marginTop: 16 }}>
            <Typography.Text strong style={{ display: 'block', marginBottom: 8 }}>
              Commands
            </Typography.Text>
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

        {/* Right panel */}
        <div
          data-testid="right-panel"
          style={{ flex: 1, display: 'flex', flexDirection: 'column', minWidth: 0 }}
        >
          <div style={{ padding: '8px 16px', display: 'flex', alignItems: 'center', justifyContent: 'space-between', borderBottom: '1px solid var(--border-secondary, #303030)' }}>
            <Typography.Text strong>Messages</Typography.Text>
            <Space size={8}>
              <Typography.Text type="secondary" style={{ fontSize: 12 }}>Auto-scroll</Typography.Text>
              <Switch
                size="small"
                checked={autoScroll}
                onChange={toggleAutoScroll}
                data-testid="autoscroll-toggle"
              />
            </Space>
          </div>
          <MessageStream
            messages={messages}
            autoScroll={autoScroll}
            onToggleMessage={handleToggleMessage}
          />
          <div style={{ padding: '0 16px', borderTop: '1px solid var(--border-secondary, #303030)' }}>
            <MessageToolbar
              messages={messages}
              driverId={connection.driverId}
              onClear={clearMessages}
            />
          </div>
        </div>
      </div>

      {/* Status bar */}
      <StatusBar connection={connection} />
    </div>
  );
};

export default DriverLabPage;
