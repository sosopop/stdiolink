import React, { useEffect, useState, useMemo } from 'react';
import { Button, Input, Space, Spin, Alert } from 'antd';
import { PlusOutlined, ReloadOutlined } from '@ant-design/icons';
import { useServicesStore } from '@/stores/useServicesStore';
import { ServiceTable } from './components/ServiceTable';
import { ServiceCreateModal } from './components/ServiceCreateModal';

const { Search } = Input;

export const ServicesPage: React.FC = () => {
  const { services, loading, error, fetchServices, deleteService, scanServices, createService } =
    useServicesStore();
  const [search, setSearch] = useState('');
  const [createOpen, setCreateOpen] = useState(false);

  useEffect(() => {
    fetchServices();
  }, [fetchServices]);

  const filtered = useMemo(() => {
    if (!search) return services;
    const q = search.toLowerCase();
    return services.filter(
      (s) => s.id.toLowerCase().includes(q) || s.name.toLowerCase().includes(q),
    );
  }, [services, search]);

  return (
    <div data-testid="page-services">
      {error && <Alert type="error" message={error} closable style={{ marginBottom: 16 }} data-testid="services-error" />}
      <div style={{ display: 'flex', justifyContent: 'space-between', marginBottom: 16 }}>
        <Search
          placeholder="Search services..."
          allowClear
          onChange={(e) => setSearch(e.target.value)}
          style={{ width: 300 }}
          data-testid="search-input"
        />
        <Space>
          <Button icon={<ReloadOutlined />} onClick={scanServices} data-testid="scan-btn">
            Scan
          </Button>
          <Button type="primary" icon={<PlusOutlined />} onClick={() => setCreateOpen(true)} data-testid="create-btn">
            New Service
          </Button>
        </Space>
      </div>
      {loading && services.length === 0 ? (
        <Spin data-testid="loading-spinner" />
      ) : (
        <ServiceTable services={filtered} loading={loading} onDelete={deleteService} />
      )}
      <ServiceCreateModal
        open={createOpen}
        onClose={() => setCreateOpen(false)}
        onCreate={createService}
      />
    </div>
  );
};

export default ServicesPage;
