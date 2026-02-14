import React from 'react';
import { Empty } from 'antd';
import type { ProcessTreeNode as TreeNodeType, ProcessTreeSummary } from '@/types/instance';
import { ProcessTreeNodeComponent } from './ProcessTreeNode';
import { ProcessTreeSummaryCard } from './ProcessTreeSummaryCard';

interface ProcessTreeProps {
  tree: TreeNodeType | null;
  summary: ProcessTreeSummary | null;
}

export const ProcessTree: React.FC<ProcessTreeProps> = ({ tree, summary }) => {
  if (!tree) {
    return <Empty description="No process tree data" data-testid="empty-process-tree" />;
  }

  return (
    <div data-testid="process-tree">
      <div style={{ background: 'rgba(0,0,0,0.2)', borderRadius: 8, padding: 12, marginBottom: 16 }}>
        <ProcessTreeNodeComponent node={tree} level={0} />
      </div>
      {summary && <ProcessTreeSummaryCard summary={summary} />}
    </div>
  );
};
