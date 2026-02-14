import { test, expect } from '@playwright/test';
import { setupApiMocks } from './helpers/setup-mocks';

test.describe('Drivers', () => {
  test.beforeEach(async ({ page }) => {
    await setupApiMocks(page);
  });

  test('shows driver list', async ({ page }) => {
    await page.goto('/drivers');
    await expect(page.getByText('Demo Driver')).toBeVisible();
    await expect(page.getByText('Modbus RTU Driver')).toBeVisible();
  });

  test('navigates to driver detail', async ({ page }) => {
    await page.goto('/drivers');
    await page.getByText('Demo Driver').click();
    await expect(page).toHaveURL(/\/drivers\/demo-driver/);
  });

  test('shows driver detail with commands', async ({ page }) => {
    await page.goto('/drivers/demo-driver');
    await expect(page.getByText('Demo Driver')).toBeVisible();
    await expect(page.getByText('read')).toBeVisible();
  });
});
