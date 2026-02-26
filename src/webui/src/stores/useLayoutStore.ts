import { create } from 'zustand';
import { persist } from 'zustand/middleware';

interface LayoutState {
  sidebarCollapsed: boolean;
  themeMode: 'dark' | 'light';
  zoomLevel: 100 | 125;
  toggleSidebar: () => void;
  setSidebarCollapsed: (collapsed: boolean) => void;
  setThemeMode: (mode: 'dark' | 'light') => void;
  toggleTheme: () => void;
  toggleZoom: () => void;
}

export const useLayoutStore = create<LayoutState>()(
  persist(
    (set) => ({
      sidebarCollapsed: false,
      themeMode: 'dark',
      zoomLevel: 100,
      toggleSidebar: () => set((s) => ({ sidebarCollapsed: !s.sidebarCollapsed })),
      setSidebarCollapsed: (collapsed) => set({ sidebarCollapsed: collapsed }),
      setThemeMode: (mode) => set({ themeMode: mode }),
      toggleTheme: () => set((s) => ({ themeMode: s.themeMode === 'dark' ? 'light' : 'dark' })),
      toggleZoom: () => set((s) => ({ zoomLevel: s.zoomLevel === 100 ? 125 : 100 })),
    }),
    {
      name: 'stdiolink-layout-storage',
      partialize: (state) => ({
        sidebarCollapsed: state.sidebarCollapsed,
        themeMode: state.themeMode,
        zoomLevel: state.zoomLevel,
      }),
    },
  ),
);
