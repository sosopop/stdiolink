import type { ThemeConfig } from 'antd';
import { theme } from 'antd';

const commonToken = {
  colorPrimary: '#6366F1',
  colorSuccess: '#10B981',
  colorWarning: '#F59E0B',
  colorError: '#EF4444',
  colorInfo: '#3B82F6',
  fontFamily: "'Inter', system-ui, -apple-system, BlinkMacSystemFont, sans-serif",
  fontFamilyCode: "'JetBrains Mono', 'Fira Code', monospace",
  borderRadius: 8,
  fontSize: 14,
  wireframe: false,
};

export const darkTheme: ThemeConfig = {
  algorithm: theme.darkAlgorithm,
  token: {
    ...commonToken,
    colorBgBase: '#0F1117',
    colorBgContainer: '#1E222D',
    colorBgElevated: '#2A2F3E',
    colorBorderSecondary: 'rgba(255, 255, 255, 0.06)',
  },
  components: {
    Layout: {
      siderBg: '#0F1117',
      headerBg: 'rgba(30, 34, 45, 0.7)',
      bodyBg: '#0F1117',
    },
    Menu: {
      darkItemBg: 'transparent',
      darkItemSelectedBg: 'rgba(99, 102, 241, 0.15)',
      darkItemSelectedColor: '#6366F1',
    },
    Table: {
      headerBg: '#1E222D',
      rowHoverBg: '#2A2F3E',
      borderColor: 'rgba(255, 255, 255, 0.06)',
    },
    Card: { actionsBg: 'rgba(0, 0, 0, 0.1)' },
  },
};

export const lightTheme: ThemeConfig = {
  algorithm: theme.defaultAlgorithm,
  token: {
    ...commonToken,
    colorBgBase: '#F3F4F6',
    colorBgContainer: '#FFFFFF',
    colorBgElevated: '#F9FAFB',
    colorBorderSecondary: '#E5E7EB',
  },
  components: {
    Layout: {
      siderBg: '#FFFFFF',
      headerBg: 'rgba(255, 255, 255, 0.7)',
      bodyBg: '#F3F4F6',
    },
    Menu: {
      itemBg: 'transparent',
      itemSelectedBg: 'rgba(99, 102, 241, 0.1)',
      itemSelectedColor: '#6366F1',
    },
    Table: {
      headerBg: '#F9FAFB',
      rowHoverBg: '#F3F4F6',
      borderColor: '#E5E7EB',
    },
    Card: { actionsBg: '#F9FAFB' },
  },
};
