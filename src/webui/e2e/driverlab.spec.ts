import { test, expect } from '@playwright/test';
import { setupApiMocks } from './helpers/setup-mocks';

test.describe('DriverLab', () => {
  test.beforeEach(async ({ page }) => {
    await setupApiMocks(page);
  });

  test('shows driverlab page', async ({ page }) => {
    await page.goto('/driverlab');
    // 使用 heading 精确匹配页面标题，避免与侧边栏菜单文本冲突
    await expect(page.getByRole('heading', { name: /Driver\s*Lab/i })).toBeVisible();
  });

  test('shows driver selection', async ({ page }) => {
    await page.goto('/driverlab');
    await expect(page.getByTestId('connection-panel')).toBeVisible();
  });

  test('shows message stream panel', async ({ page }) => {
    await page.goto('/driverlab');
    await expect(page.getByTestId('message-stream')).toBeVisible();
  });
});
