import { Layout, Menu } from 'antd';
import {
  DashboardOutlined,
  AppstoreOutlined,
  ProjectOutlined,
  RocketOutlined,
  ApiOutlined,
  ExperimentOutlined,
} from '@ant-design/icons';
import { useNavigate, useLocation } from 'react-router-dom';
import styles from './AppLayout.module.css';

const menuItems = [
  { key: '/dashboard', icon: <DashboardOutlined />, label: 'Dashboard' },
  { key: '/services', icon: <AppstoreOutlined />, label: 'Services' },
  { key: '/projects', icon: <ProjectOutlined />, label: 'Projects' },
  { key: '/instances', icon: <RocketOutlined />, label: 'Instances' },
  { key: '/drivers', icon: <ApiOutlined />, label: 'Drivers' },
  { key: '/driverlab', icon: <ExperimentOutlined />, label: 'DriverLab' },
];

interface AppSidebarProps {
  collapsed: boolean;
}

export const AppSidebar: React.FC<AppSidebarProps> = ({ collapsed }) => {
  const navigate = useNavigate();
  const location = useLocation();

  const selectedKey = menuItems.find((item) =>
    location.pathname === item.key || location.pathname.startsWith(item.key + '/'),
  )?.key ?? '/dashboard';

  return (
    <Layout.Sider
      width={200}
      collapsedWidth={64}
      collapsed={collapsed}
      className={styles.sidebar}
      trigger={null}
    >
      <Menu
        mode="inline"
        theme="dark"
        selectedKeys={[selectedKey]}
        items={menuItems}
        onClick={({ key }) => navigate(key)}
        style={{ borderRight: 'none', marginTop: 8 }}
      />
    </Layout.Sider>
  );
};
