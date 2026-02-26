import { test, expect } from '@playwright/test';
import { setupApiMocks } from './helpers/setup-mocks';

test.describe('Instances', () => {
  test.beforeEach(async ({ page }) => {
    await setupApiMocks(page);
  });

  test('shows instance list', async ({ page }) => {
    await page.goto('/instances');
    await expect(page.getByText('inst-001').first()).toBeVisible();
    await expect(page.getByText('inst-002').first()).toBeVisible();
  });

  test('shows instance list table columns', async ({ page }) => {
    await page.goto('/instances');
    // 等待表格渲染完毕
    await expect(page.getByRole('table')).toBeVisible();
    // 验证表头行包含所有列名
    const headerRow = page.getByRole('row').first();
    await expect(headerRow).toContainText('ID');
    await expect(headerRow).toContainText('Project');
    await expect(headerRow).toContainText('Status');
    await expect(headerRow).toContainText('PID');
  });

  test('shows instance PIDs', async ({ page }) => {
    await page.goto('/instances');
    await expect(page.getByText('12345').first()).toBeVisible();
    await expect(page.getByText('12346').first()).toBeVisible();
  });

  test('navigates to instance detail', async ({ page }) => {
    await page.goto('/instances');
    await page.getByRole('button', { name: 'Detail' }).first().click();
    await expect(page).toHaveURL(/\/instances\/inst-001/);
  });

  test('shows instance detail page with info', async ({ page }) => {
    await page.goto('/instances/inst-001');
    await expect(page.getByText('inst-001').first()).toBeVisible();
    // 详情页应显示关联项目和服务
    await expect(page.getByText('demo-project').first()).toBeVisible();
    await expect(page.getByText('demo-service').first()).toBeVisible();
  });

  test('shows instance detail tabs', async ({ page }) => {
    await page.goto('/instances/inst-001');
    // 验证所有 4 个 tab：Overview, Process Tree, Resources, Logs
    await expect(page.getByRole('tab', { name: /overview/i })).toBeVisible();
    await expect(page.getByRole('tab', { name: /process tree/i })).toBeVisible();
    await expect(page.getByRole('tab', { name: /resources/i })).toBeVisible();
    await expect(page.getByRole('tab', { name: /logs/i })).toBeVisible();
  });

  test('shows Terminate button in instance detail', async ({ page }) => {
    await page.goto('/instances/inst-001');
    await expect(page.getByTestId('terminate-btn')).toBeVisible();
  });

  test('switches to Logs tab', async ({ page }) => {
    await page.goto('/instances/inst-001');
    await page.getByRole('tab', { name: /logs/i }).click();
    // 日志 tab 应显示 LogViewer 组件
    await expect(page.getByTestId('instance-logs')).toBeVisible();
    // 验证 mock 日志内容可见
    await expect(page.getByText('Service started').first()).toBeVisible();
  });

  test('shows running status', async ({ page }) => {
    await page.goto('/instances');
    await expect(page.getByText('Running').first()).toBeVisible();
  });
});
