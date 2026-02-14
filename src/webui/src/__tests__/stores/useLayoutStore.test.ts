import { describe, it, expect, beforeEach } from 'vitest';
import { useLayoutStore } from '@/stores/useLayoutStore';

describe('useLayoutStore', () => {
  beforeEach(() => {
    const { setState } = useLayoutStore;
    setState({ sidebarCollapsed: false, themeMode: 'dark' });
  });

  it('has correct initial state', () => {
    const state = useLayoutStore.getState();
    expect(state.sidebarCollapsed).toBe(false);
    expect(state.themeMode).toBe('dark');
  });

  it('toggleSidebar() flips collapsed state', () => {
    useLayoutStore.getState().toggleSidebar();
    expect(useLayoutStore.getState().sidebarCollapsed).toBe(true);
    useLayoutStore.getState().toggleSidebar();
    expect(useLayoutStore.getState().sidebarCollapsed).toBe(false);
  });

  it('setSidebarCollapsed() sets explicit value', () => {
    useLayoutStore.getState().setSidebarCollapsed(true);
    expect(useLayoutStore.getState().sidebarCollapsed).toBe(true);
    useLayoutStore.getState().setSidebarCollapsed(false);
    expect(useLayoutStore.getState().sidebarCollapsed).toBe(false);
  });

  it('setThemeMode() sets theme', () => {
    useLayoutStore.getState().setThemeMode('light');
    expect(useLayoutStore.getState().themeMode).toBe('light');
    useLayoutStore.getState().setThemeMode('dark');
    expect(useLayoutStore.getState().themeMode).toBe('dark');
  });

  it('toggleTheme() switches between dark and light', () => {
    expect(useLayoutStore.getState().themeMode).toBe('dark');
    useLayoutStore.getState().toggleTheme();
    expect(useLayoutStore.getState().themeMode).toBe('light');
    useLayoutStore.getState().toggleTheme();
    expect(useLayoutStore.getState().themeMode).toBe('dark');
  });
});
