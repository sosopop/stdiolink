import type { ThemeConfig } from 'antd';
import { theme } from 'antd';

const commonToken = {
  colorPrimary: '#6366F1',
  colorSuccess: '#10B981',
  colorWarning: '#F59E0B',
  colorError: '#EF4444',
  colorInfo: '#3B82F6',
  fontFamily: "'Inter', -apple-system, sans-serif",
  fontFamilyCode: "'JetBrains Mono', monospace",
  borderRadius: 12,
  wireframe: false,
};

const sharedComponentConfig = {
  Button: {
    borderRadius: 6,
    controlHeight: 36,
    controlHeightSM: 28,
    controlHeightLG: 44,
    paddingInline: 16,
    fontWeight: 500,
    primaryShadow: '0 4px 14px 0 rgba(99, 102, 241, 0.3)',
  },
  Input: {
    borderRadius: 6,
    controlHeight: 36,
  },
  Select: {
    borderRadius: 6,
    controlHeight: 36,
  },
  Card: {
    borderRadius: 12,
  },
  Modal: {
    borderRadius: 16,
  },
  Dropdown: {
    borderRadius: 10,
  },
};

export const darkTheme: ThemeConfig = {
  algorithm: theme.darkAlgorithm,
  token: {
    ...commonToken,
    colorBgBase: '#05060A',
    colorBgContainer: '#14161E',
    colorBgElevated: '#1E222D',
    colorBorder: 'rgba(255, 255, 255, 0.1)',
    colorTextBase: '#F1F5F9',
    colorTextSecondary: '#94A3B8',
  },
  components: {
    ...sharedComponentConfig,
    Layout: {
      headerBg: 'transparent',
      siderBg: 'rgba(11, 12, 21, 0.4)',
      bodyBg: 'transparent',
    },
    Menu: {
      darkItemBg: 'transparent',
      darkItemSelectedBg: 'rgba(99, 102, 241, 0.15)',
      darkItemSelectedColor: '#FFFFFF',
    },
    Card: {
      colorBgContainer: 'rgba(30, 34, 45, 0.6)',
    },
    Table: {
      colorBgContainer: 'transparent',
      headerBg: 'rgba(255, 255, 255, 0.04)',
      rowHoverBg: 'rgba(255, 255, 255, 0.06)',
      bodySortBg: 'rgba(255, 255, 255, 0.015)',
      headerSortHoverBg: 'rgba(255, 255, 255, 0.06)',
      headerSortActiveBg: 'rgba(255, 255, 255, 0.08)',
    },
  }
};

export const lightTheme: ThemeConfig = {
  algorithm: theme.defaultAlgorithm,
  token: {
    ...commonToken,
    colorBgBase: '#F4F5F7',
    colorBgContainer: '#FFFFFF',
    colorBgElevated: '#FFFFFF',
    colorBorder: 'rgba(0, 0, 0, 0.08)',
    colorTextBase: '#1E293B',
    colorTextSecondary: '#64748B',
  },
  components: {
    ...sharedComponentConfig,
    Layout: {
      headerBg: 'rgba(255, 255, 255, 0.72)',
      siderBg: 'rgba(255, 255, 255, 0.65)',
      bodyBg: 'transparent',
    },
    Menu: {
      itemBg: 'transparent',
      itemSelectedBg: 'rgba(99, 102, 241, 0.08)',
      itemSelectedColor: '#4F46E5',
    },
    Card: {
      colorBgContainer: 'rgba(255, 255, 255, 0.85)',
    },
    Table: {
      colorBgContainer: 'transparent',
      headerBg: 'rgba(99, 102, 241, 0.03)',
      rowHoverBg: 'rgba(99, 102, 241, 0.06)',
      bodySortBg: 'rgba(99, 102, 241, 0.015)',
      headerSortHoverBg: 'rgba(99, 102, 241, 0.05)',
      headerSortActiveBg: 'rgba(99, 102, 241, 0.07)',
    },
  }
};
