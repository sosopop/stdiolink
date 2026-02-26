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

  test('navigates to services via sidebar', async ({ page }) => {
    await page.getByRole('menuitem', { name: /services/i }).click();
    await expect(page).toHaveURL(/\/services/);
  });
});
