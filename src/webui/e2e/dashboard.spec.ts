import { test, expect } from '@playwright/test';
import { setupApiMocks } from './helpers/setup-mocks';

test.describe('Dashboard', () => {
  test.beforeEach(async ({ page }) => {
    await setupApiMocks(page);
    await page.goto('/dashboard');
  });

  test('shows KPI stat cards', async ({ page }) => {
    await expect(page.getByTestId('page-dashboard')).toBeVisible();
    // KPI 标题通过 CSS text-transform 显示大写，但 DOM 中是正常大小写
    // 改用独特子文本来验证卡片渲染
    await expect(page.getByRole('heading', { name: 'Mission Control' })).toBeVisible();
    await expect(page.getByText('3 enabled')).toBeVisible();
    await expect(page.getByText('2 running')).toBeVisible();
  });

  test('shows server info', async ({ page }) => {
    await expect(page.getByText('0.1.0')).toBeVisible();
  });

  test('shows active projects section', async ({ page }) => {
    // Dashboard 显示的是 Active Projects 区域（项目卡片）
    await expect(page.getByText('Active Projects')).toBeVisible();
    await expect(page.getByText('Demo Project').first()).toBeVisible();
  });

  test('shows project cards with details', async ({ page }) => {
    // 验证项目卡片中包含关键信息
    await expect(page.getByText('demo-project').first()).toBeVisible();
    await expect(page.getByText('demo-service').first()).toBeVisible();
  });

  test('shows Live Instances table', async ({ page }) => {
    await expect(page.getByRole('heading', { name: /Live Instances/i })).toBeVisible();
    // 表头列
    await expect(page.getByRole('columnheader', { name: /Target Project/i })).toBeVisible();
    await expect(page.getByRole('columnheader', { name: /System PID/i })).toBeVisible();
    // 表格数据
    await expect(page.getByText('12345').first()).toBeVisible();
  });

  test('shows Event Feed section', async ({ page }) => {
    await expect(page.getByRole('heading', { name: 'Event Feed' })).toBeVisible();
    // 没有事件时应显示空状态
    await expect(page.getByText('No events recorded')).toBeVisible();
  });

  test('navigates to services via sidebar', async ({ page }) => {
    await page.getByRole('menuitem', { name: /services/i }).click();
    await expect(page).toHaveURL(/\/services/);
  });
});
