import { test, expect } from '@playwright/test';
import { setupApiMocks } from './helpers/setup-mocks';

test.describe('Drivers', () => {
  test.beforeEach(async ({ page }) => {
    await setupApiMocks(page);
  });

  test('shows driver list', async ({ page }) => {
    await page.goto('/drivers');
    await expect(page.getByText('Demo Driver').first()).toBeVisible();
    await expect(page.getByText('Modbus RTU Driver')).toBeVisible();
  });

  test('navigates to driver detail', async ({ page }) => {
    await page.goto('/drivers');
    await page.getByText('Demo Driver').first().click();
    await expect(page).toHaveURL(/\/drivers\/demo-driver/);
  });

  test('shows driver detail with commands', async ({ page }) => {
    await page.goto('/drivers/demo-driver');
    await expect(page.getByRole('heading', { name: 'Demo Driver' })).toBeVisible();
    // 默认 tab 是 Metadata，需先切换到 Commands tab
    await page.getByRole('tab', { name: 'Commands' }).click();
    // 等待 Commands tabpanel 渲染完成
    await expect(page.getByRole('tabpanel', { name: 'Commands' })).toBeVisible();
    await expect(page.getByText('read', { exact: true }).first()).toBeVisible();
  });
});
