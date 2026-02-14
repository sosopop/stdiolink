import React from 'react';
import { Switch, Space } from 'antd';
import { SunOutlined, MoonOutlined } from '@ant-design/icons';
import { useLayoutStore } from '@/stores/useLayoutStore';
import { useEventStreamStore } from '@/stores/useEventStreamStore';
import { SseStatusIndicator } from '@/components/Common/SseStatusIndicator';
import { LanguageSwitcher } from '../LanguageSwitcher';
import styles from './AppLayout.module.css';

export const AppHeader: React.FC = () => {
  const themeMode = useLayoutStore((s) => s.themeMode);
  const toggleTheme = useLayoutStore((s) => s.toggleTheme);

  const sseStatus = useEventStreamStore((s) => s.status);
  const lastEventTime = useEventStreamStore((s) => s.lastEventTime);
  const sseError = useEventStreamStore((s) => s.error);

  return (
    <header className={styles.header}>
      <div className={styles.headerLeft}>
        <div className={styles.brand}>
          <svg width="32" height="32" viewBox="0 0 32 32" fill="none" xmlns="http://www.w3.org/2000/svg" className={styles.brandLogo}>
            <path d="M16 2L2 9L16 16L30 9L16 2Z" fill="url(#paint0_linear)" fillOpacity="0.8" />
            <path d="M2 23L16 30L30 23V9L16 16L2 9V23Z" stroke="url(#paint1_linear)" strokeWidth="2" strokeLinejoin="round" />
            <path d="M16 16V30" stroke="url(#paint2_linear)" strokeWidth="2" strokeLinecap="round" />
            <circle cx="16" cy="16" r="4" fill="#6366F1" fillOpacity="0.5" style={{ filter: 'blur(4px)' }} />
            <defs>
              <linearGradient id="paint0_linear" x1="16" y1="2" x2="16" y2="30" gradientUnits="userSpaceOnUse">
                <stop stopColor="#6366F1" />
                <stop offset="1" stopColor="#8B5CF6" stopOpacity="0.5" />
              </linearGradient>
              <linearGradient id="paint1_linear" x1="2" y1="9" x2="30" y2="30" gradientUnits="userSpaceOnUse">
                <stop stopColor="#A5B4FC" />
                <stop offset="1" stopColor="#6366F1" />
              </linearGradient>
              <linearGradient id="paint2_linear" x1="16" y1="16" x2="16" y2="30" gradientUnits="userSpaceOnUse">
                <stop stopColor="#A5B4FC" stopOpacity="0" />
                <stop offset="0.5" stopColor="#A5B4FC" />
                <stop offset="1" stopColor="#A5B4FC" stopOpacity="0" />
              </linearGradient>
            </defs>
          </svg>
          <span className={styles.logoText}>STDIOLINK</span>
        </div>
      </div>

      <div className={styles.headerRight}>
        <Space size={24}>
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
          <LanguageSwitcher />
        </Space>
      </div>
    </header>
  );
};
