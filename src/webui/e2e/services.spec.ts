import { test, expect } from '@playwright/test';
import { setupApiMocks } from './helpers/setup-mocks';

test.describe('Services', () => {
  test.beforeEach(async ({ page }) => {
    await setupApiMocks(page);
  });

  test('shows service list', async ({ page }) => {
    await page.goto('/services');
    await expect(page.getByText('Demo Service')).toBeVisible();
    await expect(page.getByText('Modbus Service')).toBeVisible();
  });

  test('filters services by search', async ({ page }) => {
    await page.goto('/services');
    const searchInput = page.getByPlaceholder(/search/i);
    if (await searchInput.isVisible()) {
      await searchInput.fill('Demo');
      await expect(page.getByText('Demo Service')).toBeVisible();
    }
  });

  test('navigates to service detail', async ({ page }) => {
    await page.goto('/services');
    await page.getByText('Demo Service').click();
    await expect(page).toHaveURL(/\/services\/demo-service/);
  });

  test('shows detail tabs', async ({ page }) => {
    await page.goto('/services/demo-service');
    await expect(page.getByText('Overview')).toBeVisible();
    await expect(page.getByText('Files')).toBeVisible();
    await expect(page.getByText('Schema')).toBeVisible();
    await expect(page.getByText('Projects')).toBeVisible();
  });

  test('switches detail tabs', async ({ page }) => {
    await page.goto('/services/demo-service');
    await page.getByText('Files').click();
    await expect(page.getByText('index.js')).toBeVisible();
  });
});
