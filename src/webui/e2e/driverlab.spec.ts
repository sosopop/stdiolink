import { test, expect } from '@playwright/test';
import { setupApiMocks } from './helpers/setup-mocks';

test.describe('DriverLab', () => {
  test.beforeEach(async ({ page }) => {
    await setupApiMocks(page);
  });

  test('shows driverlab page', async ({ page }) => {
    await page.goto('/driverlab');
    await expect(page.getByText(/DriverLab/i)).toBeVisible();
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
