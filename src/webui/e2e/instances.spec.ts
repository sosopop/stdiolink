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

  test('navigates to instance detail', async ({ page }) => {
    await page.goto('/instances');
    // 用表格行中的 Detail 按钮导航，而非直接点击 inst-001 文本
    await page.getByRole('button', { name: 'Detail' }).first().click();
    await expect(page).toHaveURL(/\/instances\/inst-001/);
  });

  test('shows instance detail page', async ({ page }) => {
    await page.goto('/instances/inst-001');
    await expect(page.getByText('inst-001').first()).toBeVisible();
  });

  test('shows running status', async ({ page }) => {
    await page.goto('/instances');
    await expect(page.getByText('Running').first()).toBeVisible();
  });
});
