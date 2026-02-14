import { test, expect } from '@playwright/test';
import { setupApiMocks } from './helpers/setup-mocks';

test.describe('Instances', () => {
  test.beforeEach(async ({ page }) => {
    await setupApiMocks(page);
  });

  test('shows instance list', async ({ page }) => {
    await page.goto('/instances');
    await expect(page.getByText('inst-001')).toBeVisible();
    await expect(page.getByText('inst-002')).toBeVisible();
  });

  test('navigates to instance detail', async ({ page }) => {
    await page.goto('/instances');
    await page.getByText('inst-001').click();
    await expect(page).toHaveURL(/\/instances\/inst-001/);
  });

  test('shows instance detail page', async ({ page }) => {
    await page.goto('/instances/inst-001');
    await expect(page.getByText('inst-001')).toBeVisible();
  });

  test('shows running status', async ({ page }) => {
    await page.goto('/instances');
    await expect(page.getByText('running').first()).toBeVisible();
  });
});
