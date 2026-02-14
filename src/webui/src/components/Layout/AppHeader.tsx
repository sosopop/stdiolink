import React from 'react';
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
          className={styles.sidebarTrigger}
          icon={collapsed ? <MenuUnfoldOutlined /> : <MenuFoldOutlined />}
          onClick={toggleSidebar}
          aria-label="Toggle sidebar"
        />
        <div className={styles.brand}>
          <div className={styles.brandIcon}>⚡</div>
          <span className={styles.logoText}>stdiolink</span>
        </div>
      </div>
      
      <div className={styles.headerRight}>
        <Space size={20}>
          <SseStatusIndicator 
            status={sseStatus} 
            lastEventTime={lastEventTime} 
            error={sseError} 
          />
          <div className={styles.themeSwitchWrapper}>
            <SunOutlined style={{ fontSize: 14, opacity: themeMode === 'light' ? 1 : 0.3 }} />
            <Switch
              size="small"
              checked={themeMode === 'dark'}
              onChange={() => toggleTheme()}
              aria-label="Toggle theme"
            />
            <MoonOutlined style={{ fontSize: 14, opacity: themeMode === 'dark' ? 1 : 0.3 }} />
          </div>
        </Space>
      </div>
    </Layout.Header>
  );
};
