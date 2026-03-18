import React from 'react';
import { useTranslation } from 'react-i18next';
import { Link } from 'react-router-dom';
import { Switch, Space, Button, Tooltip } from 'antd';
import { SunOutlined, MoonOutlined, ZoomInOutlined, ZoomOutOutlined } from '@ant-design/icons';
import { useLayoutStore } from '@/stores/useLayoutStore';
import { useEventStreamStore } from '@/stores/useEventStreamStore';
import { SseStatusIndicator } from '@/components/Common/SseStatusIndicator';
import { LanguageSwitcher } from '../LanguageSwitcher';
import styles from './AppLayout.module.css';

export const AppHeader: React.FC = () => {
  const { t } = useTranslation();
  const themeMode = useLayoutStore((s) => s.themeMode);
  const toggleTheme = useLayoutStore((s) => s.toggleTheme);
  const zoomLevel = useLayoutStore((s) => s.zoomLevel);
  const toggleZoom = useLayoutStore((s) => s.toggleZoom);

  const sseStatus = useEventStreamStore((s) => s.status);
  const lastEventTime = useEventStreamStore((s) => s.lastEventTime);
  const sseError = useEventStreamStore((s) => s.error);

  return (
    <header className={styles.header}>
      <div className={styles.headerLeft}>
        <Link to="/" className={styles.brand} style={{ textDecoration: 'none', userSelect: 'none' }}>
          <svg width="32" height="32" viewBox="70 120 340 270" fill="none" xmlns="http://www.w3.org/2000/svg" className={styles.brandLogo}>
            <defs>
              <linearGradient id="brandGrad" x1="0%" y1="0%" x2="100%" y2="100%">
                <stop offset="0%" stopColor={themeMode === 'dark' ? '#818CF8' : '#6366F1'} />
                <stop offset="100%" stopColor={themeMode === 'dark' ? '#C084FC' : '#8B5CF6'} />
              </linearGradient>
              <filter id="softShadow" x="-20%" y="-20%" width="140%" height="140%">
                <feDropShadow dx="0" dy="12" stdDeviation="20" floodColor={themeMode === 'dark' ? '#000000' : '#8B5CF6'} floodOpacity="0.3" />
              </filter>
            </defs>
            <g filter="url(#softShadow)">
              <polyline points="105,160 225,256 105,352" stroke="url(#brandGrad)" strokeWidth="44" strokeLinecap="round" strokeLinejoin="round" />
              <circle cx="332" cy="256" r="48" fill="transparent" stroke="url(#brandGrad)" strokeWidth="32" />
            </g>
          </svg>
          <span className={styles.brandCopy}>
            <span className={styles.logoText}>{t('layout.brand_title')}</span>
            <span className={styles.logoSubtitle}>{t('layout.brand_subtitle')}</span>
          </span>
        </Link>
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
          <Space size={8}>
            <Tooltip title={zoomLevel === 100 ? '放大到 125%' : '恢复为 100%'}>
              <Button
                type="text"
                icon={zoomLevel === 100 ? <ZoomInOutlined /> : <ZoomOutOutlined />}
                onClick={toggleZoom}
                aria-label="Toggle zoom"
              />
            </Tooltip>
            <LanguageSwitcher />
          </Space>
        </Space>
      </div>
    </header>
  );
};
