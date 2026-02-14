import { ConfigProvider } from 'antd';
import { RouterProvider } from 'react-router-dom';
import { router } from './router';
import { darkTheme, lightTheme } from './theme/antd-theme';
import { useLayoutStore } from './stores/useLayoutStore';

function App() {
  const themeMode = useLayoutStore((s) => s.themeMode);

  return (
    <ConfigProvider theme={themeMode === 'dark' ? darkTheme : lightTheme}>
      <div data-theme={themeMode}>
        <RouterProvider router={router} />
      </div>
    </ConfigProvider>
  );
}

export default App;
