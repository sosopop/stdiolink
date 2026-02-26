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

  test('shows driver detail tabs', async ({ page }) => {
    await page.goto('/drivers/demo-driver');
    // 验证所有 3 个 tab：Metadata, Commands, Docs
    await expect(page.getByRole('tab', { name: 'Metadata' })).toBeVisible();
    await expect(page.getByRole('tab', { name: 'Commands' })).toBeVisible();
    await expect(page.getByRole('tab', { name: 'Docs' })).toBeVisible();
  });

  test('shows driver metadata content', async ({ page }) => {
    await page.goto('/drivers/demo-driver');
    // 默认 Metadata tab 应显示驱动信息
    await expect(page.getByRole('heading', { name: 'Demo Driver' })).toBeVisible();
    await expect(page.getByText('1.0.0').first()).toBeVisible();
    await expect(page.getByText('A demo driver for testing')).toBeVisible();
  });

  test('shows driver detail with commands', async ({ page }) => {
    await page.goto('/drivers/demo-driver');
    await expect(page.getByRole('heading', { name: 'Demo Driver' })).toBeVisible();
    // 默认 tab 是 Metadata，需先切换到 Commands tab
    await page.getByRole('tab', { name: 'Commands' }).click();
    await expect(page.getByRole('tabpanel', { name: 'Commands' })).toBeVisible();
    await expect(page.getByText('read', { exact: true }).first()).toBeVisible();
    await expect(page.getByText('write', { exact: true }).first()).toBeVisible();
  });

  test('shows command parameters', async ({ page }) => {
    await page.goto('/drivers/demo-driver');
    await page.getByRole('tab', { name: 'Commands' }).click();
    await expect(page.getByRole('tabpanel', { name: 'Commands' })).toBeVisible();
    // 验证命令参数表
    await expect(page.getByText('address').first()).toBeVisible();
    await expect(page.getByText('Device address').first()).toBeVisible();
  });

  test('shows action buttons in detail header', async ({ page }) => {
    await page.goto('/drivers/demo-driver');
    await expect(page.getByRole('button', { name: /test in driverlab/i })).toBeVisible();
    await expect(page.getByRole('button', { name: /export docs/i })).toBeVisible();
    await expect(page.getByRole('button', { name: /export meta json/i })).toBeVisible();
  });
});
