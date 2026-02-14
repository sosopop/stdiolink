import React from 'react';
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
      width={220}
      collapsedWidth={80}
      collapsed={collapsed}
      className={styles.sidebar}
      trigger={null}
    >
      <div style={{ flex: 1, paddingTop: 16 }}>
        <Menu
          mode="inline"
          selectedKeys={[selectedKey]}
          items={menuItems}
          onClick={({ key }) => navigate(key)}
          style={{ background: 'transparent' }}
        />
      </div>
    </Layout.Sider>
  );
};
