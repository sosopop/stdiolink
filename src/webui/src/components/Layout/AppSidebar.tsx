import React from 'react';
import { Layout, Menu } from 'antd';
import {
  DashboardOutlined,
  ProjectOutlined,
  CloudServerOutlined,
  DeploymentUnitOutlined,
  ApiOutlined,
  ExperimentOutlined,
  MenuFoldOutlined,
  MenuUnfoldOutlined,
} from '@ant-design/icons';
import { useNavigate, useLocation } from 'react-router-dom';
import { useLayoutStore } from '@/stores/useLayoutStore';
import styles from './AppLayout.module.css';

interface AppSidebarProps {
  collapsed: boolean;
}

export const AppSidebar: React.FC<AppSidebarProps> = ({ collapsed }) => {
  const navigate = useNavigate();
  const location = useLocation();
  const toggleSidebar = useLayoutStore((s) => s.toggleSidebar);

  const menuItems = [
    { key: '/', icon: <DashboardOutlined />, label: 'Dashboard' },
    { key: '/projects', icon: <ProjectOutlined />, label: 'Projects' },
    { key: '/instances', icon: <DeploymentUnitOutlined />, label: 'Instances' },
    { key: '/services', icon: <CloudServerOutlined />, label: 'Services' },
    { key: '/drivers', icon: <ApiOutlined />, label: 'Drivers' },
    { key: '/driverlab', icon: <ExperimentOutlined />, label: 'DriverLab' },
  ];

  const selectedKey = location.pathname === '/' ? '/' : `/${location.pathname.split('/')[1]}`;

  return (
    <Layout.Sider
      width={240}
      collapsedWidth={80}
      collapsed={collapsed}
      className={styles.sidebar}
      trigger={null}
    >
      <div style={{ flex: 1, paddingTop: 24, overflowY: 'auto' }}>
        <Menu
          mode="inline"
          selectedKeys={[selectedKey]}
          items={menuItems}
          onClick={({ key }) => navigate(key)}
        />
      </div>

      <div
        className={styles.sidebarCollapseTrigger}
        onClick={toggleSidebar}
      >
        {collapsed ? <MenuUnfoldOutlined /> : <MenuFoldOutlined />}
      </div>
    </Layout.Sider>
  );
};
