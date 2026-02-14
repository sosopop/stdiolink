import React, { useState } from 'react';
import { useTranslation } from 'react-i18next';
import { Button, Empty } from 'antd';
import { PlusOutlined } from '@ant-design/icons';
import type { SchemaNode } from '@/utils/schemaPath';
import { useSchemaEditorStore } from '@/stores/useSchemaEditorStore';
import { getFieldByPath } from '@/utils/schemaPath';
import { FieldCard } from './FieldCard';
import { FieldEditModal } from './FieldEditModal';

export const VisualEditor: React.FC = () => {
  const { t } = useTranslation();
  const { nodes, addField, updateField, removeField, moveField } = useSchemaEditorStore();
  const [modalVisible, setModalVisible] = useState(false);
  const [editingPath, setEditingPath] = useState<string | null>(null);
  const [parentPath, setParentPath] = useState<string | undefined>(undefined);

  const handleEdit = (path: string) => {
    setEditingPath(path);
    setParentPath(undefined);
    setModalVisible(true);
  };

  const handleAdd = () => {
    setEditingPath(null);
    setParentPath(undefined);
    setModalVisible(true);
  };

  const handleAddChild = (pPath: string) => {
    setEditingPath(null);
    setParentPath(pPath);
    setModalVisible(true);
  };

  const handleSave = (node: SchemaNode) => {
    if (editingPath) {
      updateField(editingPath, node);
    } else {
      addField(node, parentPath);
    }
    setModalVisible(false);
  };

  const editingField = editingPath ? getFieldByPath(nodes, editingPath) : null;

  const existingNames = (() => {
    if (parentPath) {
      const parent = getFieldByPath(nodes, parentPath);
      return parent?.children?.map((c) => c.name) ?? [];
    }
    return nodes.map((n) => n.name);
  })();

  return (
    <div data-testid="visual-editor">
      {nodes.length === 0 ? (
        <Empty description={t('schema.no_fields')} data-testid="visual-empty">
          <Button
            type="primary"
            icon={<PlusOutlined />}
            onClick={handleAdd}
            data-testid="visual-add-first"
          >
            {t('schema.add_first_field')}
          </Button>
        </Empty>
      ) : (
        <>
          {nodes.map((node, idx) => (
            <FieldCard
              key={node.name}
              field={node}
              path={node.name}
              level={0}
              isFirst={idx === 0}
              isLast={idx === nodes.length - 1}
              onEdit={handleEdit}
              onDelete={removeField}
              onMove={moveField}
              onAddChild={handleAddChild}
            />
          ))}
          <Button
            type="dashed"
            icon={<PlusOutlined />}
            onClick={handleAdd}
            style={{ marginTop: 8 }}
            data-testid="visual-add-btn"
            block
          >
            {t('schema.add_field')}
          </Button>
        </>
      )}

      <FieldEditModal
        visible={modalVisible}
        field={editingField}
        existingNames={existingNames}
        onSave={handleSave}
        onCancel={() => setModalVisible(false)}
      />
    </div>
  );
};
