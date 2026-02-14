import React, { useEffect, useState, useMemo } from 'react';
import { useTranslation } from 'react-i18next';
import { Button, Input, Space } from 'antd';
import { ReloadOutlined, ScanOutlined } from '@ant-design/icons';
import { useDriversStore } from '@/stores/useDriversStore';
import { DriversTable } from '@/components/Drivers/DriversTable';

const { Search } = Input;

export const DriversPage: React.FC = () => {
  const { t } = useTranslation();
  const { drivers, loading, fetchDrivers, scanDrivers } = useDriversStore();
  const [search, setSearch] = useState('');

  useEffect(() => {
    fetchDrivers();
  }, [fetchDrivers]);

  const filtered = useMemo(() => {
    if (!search) return drivers;
    const q = search.toLowerCase();
    return drivers.filter((d) =>
      d.id.toLowerCase().includes(q) || (d.name?.toLowerCase().includes(q)),
    );
  }, [drivers, search]);

  return (
    <div data-testid="page-drivers">
      <div style={{ display: 'flex', justifyContent: 'space-between', alignItems: 'center', marginBottom: 16 }}>
        <h2 style={{ margin: 0 }}>{t('drivers.title')}</h2>
        <Space>
          <Button icon={<ScanOutlined />} onClick={() => scanDrivers()} loading={loading} data-testid="scan-btn">
            {t('drivers.scan')}
          </Button>
          <Button icon={<ReloadOutlined />} onClick={() => fetchDrivers()} loading={loading} data-testid="refresh-btn">
            {t('drivers.refresh')}
          </Button>
        </Space>
      </div>
      <div style={{ marginBottom: 16 }}>
        <Search
          placeholder={t('drivers.search_placeholder')}
          allowClear
          onChange={(e) => setSearch(e.target.value)}
          style={{ width: 300 }}
          data-testid="driver-search"
        />
      </div>
      <DriversTable drivers={filtered} />
    </div>
  );
};

export default DriversPage;
