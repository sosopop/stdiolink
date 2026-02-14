import React from 'react';
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
    <div className={styles.layoutWrapper}>
      <AppHeader />
      <div className={styles.bodyContainer}>
        <AppSidebar collapsed={collapsed} />
        <main className={styles.content}>
          <Outlet />
        </main>
      </div>
    </div>
  );
};
