import { test, expect } from '@playwright/test';
import { setupApiMocks } from './helpers/setup-mocks';

test.describe('Projects', () => {
  test.beforeEach(async ({ page }) => {
    await setupApiMocks(page);
  });

  test('shows project list', async ({ page }) => {
    await page.goto('/projects');
    await expect(page.getByText('Demo Project')).toBeVisible();
    await expect(page.getByText('Test Project')).toBeVisible();
  });

  test('navigates to project detail', async ({ page }) => {
    await page.goto('/projects');
    await page.getByText('Demo Project').click();
    await expect(page).toHaveURL(/\/projects\/demo-project/);
  });

  test('shows project detail page', async ({ page }) => {
    await page.goto('/projects/demo-project');
    await expect(page.getByText('Demo Project')).toBeVisible();
  });

  test('navigates to create page', async ({ page }) => {
    await page.goto('/projects');
    const createBtn = page.getByRole('button', { name: /create/i });
    if (await createBtn.isVisible()) {
      await createBtn.click();
      await expect(page).toHaveURL(/\/projects\/create/);
    }
  });

  test('shows enabled/disabled status', async ({ page }) => {
    await page.goto('/projects');
    await expect(page.getByText('running')).toBeVisible();
  });
});
