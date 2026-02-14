import { Layout } from 'antd';
import { Outlet } from 'react-router-dom';
import { AppHeader } from './AppHeader';
import { AppSidebar } from './AppSidebar';
import { useLayoutStore } from '@/stores/useLayoutStore';
import { useGlobalEventStream } from '@/hooks/useGlobalEventStream';
import styles from './AppLayout.module.css';

export const AppLayout: React.FC = () => {
  const collapsed = useLayoutStore((s) => s.sidebarCollapsed);
  useGlobalEventStream();

  return (
    <Layout style={{ minHeight: '100vh' }}>
      <AppSidebar collapsed={collapsed} />
      <Layout>
        <AppHeader />
        <Layout.Content className={styles.content}>
          <Outlet />
        </Layout.Content>
      </Layout>
    </Layout>
  );
};
