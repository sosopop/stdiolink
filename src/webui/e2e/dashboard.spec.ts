import { test, expect } from '@playwright/test';
import { setupApiMocks } from './helpers/setup-mocks';

test.describe('Dashboard', () => {
  test.beforeEach(async ({ page }) => {
    await setupApiMocks(page);
    await page.goto('/dashboard');
  });

  test('shows KPI stat cards', async ({ page }) => {
    await expect(page.getByTestId('page-dashboard')).toBeVisible();
    await expect(page.getByText('Services')).toBeVisible();
    await expect(page.getByText('Projects')).toBeVisible();
    await expect(page.getByText('Instances')).toBeVisible();
  });

  test('shows server info', async ({ page }) => {
    await expect(page.getByText('0.1.0')).toBeVisible();
  });

  test('shows active instances', async ({ page }) => {
    await expect(page.getByText('inst-001')).toBeVisible();
  });

  test('navigates to services via sidebar', async ({ page }) => {
    await page.getByRole('menuitem', { name: /services/i }).click();
    await expect(page).toHaveURL(/\/services/);
  });
});
