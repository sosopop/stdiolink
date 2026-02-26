import React, { useEffect } from 'react';
import { Outlet } from 'react-router-dom';
import { AppHeader } from './AppHeader';
import { AppSidebar } from './AppSidebar';
import { useLayoutStore } from '@/stores/useLayoutStore';
import { useGlobalEventStream } from '@/hooks/useGlobalEventStream';
import styles from './AppLayout.module.css';

export const AppLayout: React.FC = () => {
  const collapsed = useLayoutStore((s) => s.sidebarCollapsed);
  const zoomLevel = useLayoutStore((s) => s.zoomLevel);
  useGlobalEventStream();

  // 将 zoom 应用到 html 根元素，效果等同于浏览器 Ctrl+滚轮缩放
  useEffect(() => {
    document.documentElement.style.zoom = `${zoomLevel}%`;
    return () => { document.documentElement.style.zoom = ''; };
  }, [zoomLevel]);

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
