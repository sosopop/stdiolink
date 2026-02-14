import { Layout, Button, Switch, Space } from 'antd';
import { MenuFoldOutlined, MenuUnfoldOutlined, SunOutlined, MoonOutlined } from '@ant-design/icons';
import { useLayoutStore } from '@/stores/useLayoutStore';
import { useEventStreamStore } from '@/stores/useEventStreamStore';
import { SseStatusIndicator } from '@/components/Common/SseStatusIndicator';
import styles from './AppLayout.module.css';

export const AppHeader: React.FC = () => {
  const collapsed = useLayoutStore((s) => s.sidebarCollapsed);
  const toggleSidebar = useLayoutStore((s) => s.toggleSidebar);
  const themeMode = useLayoutStore((s) => s.themeMode);
  const toggleTheme = useLayoutStore((s) => s.toggleTheme);
  const sseStatus = useEventStreamStore((s) => s.status);
  const lastEventTime = useEventStreamStore((s) => s.lastEventTime);
  const sseError = useEventStreamStore((s) => s.error);

  return (
    <Layout.Header className={styles.header}>
      <div className={styles.headerLeft}>
        <Button
          type="text"
          icon={collapsed ? <MenuUnfoldOutlined /> : <MenuFoldOutlined />}
          onClick={toggleSidebar}
          aria-label="Toggle sidebar"
        />
        <span className={styles.logo}>stdiolink</span>
      </div>
      <div className={styles.headerRight}>
        <Space size={12}>
          <SseStatusIndicator status={sseStatus} lastEventTime={lastEventTime} error={sseError} />
          <Switch
            checkedChildren={<MoonOutlined />}
            unCheckedChildren={<SunOutlined />}
            checked={themeMode === 'dark'}
            onChange={() => toggleTheme()}
            aria-label="Toggle theme"
          />
        </Space>
      </div>
    </Layout.Header>
  );
};
