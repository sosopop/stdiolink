import { useEffect } from 'react';
import { ConfigProvider, App as AntdApp } from 'antd';
import { RouterProvider } from 'react-router-dom';
import { router } from './router';
import { darkTheme, lightTheme } from './theme/antd-theme';
import { useLayoutStore } from './stores/useLayoutStore';

function App() {
  const themeMode = useLayoutStore((s) => s.themeMode);

  useEffect(() => {
    // 同步主题属性到 html 标签，驱动全局 CSS 变量切换
    document.documentElement.setAttribute('data-theme', themeMode);
  }, [themeMode]);

  return (
    <ConfigProvider theme={themeMode === 'dark' ? darkTheme : lightTheme}>
      <AntdApp>
        <RouterProvider router={router} />
      </AntdApp>
    </ConfigProvider>
  );
}

export default App;
