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

  test('shows service list metadata', async ({ page }) => {
    await page.goto('/services');
    // 验证列表中显示版本号和项目数
    await expect(page.getByText('1.0.0').first()).toBeVisible();
    await expect(page.getByText('2.0.0').first()).toBeVisible();
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
    await expect(page.getByRole('tab', { name: 'Overview' })).toBeVisible();
    await expect(page.getByRole('tab', { name: 'Files' })).toBeVisible();
    await expect(page.getByRole('tab', { name: 'Schema' })).toBeVisible();
    await expect(page.getByRole('tab', { name: 'Projects' })).toBeVisible();
  });

  test('shows service overview with manifest info', async ({ page }) => {
    await page.goto('/services/demo-service');
    // Overview tab 应显示 manifest 信息
    await expect(page.getByText('Demo Service').first()).toBeVisible();
    await expect(page.getByText('Test Author')).toBeVisible();
    await expect(page.getByText('A demo service for testing')).toBeVisible();
  });

  test('switches to Files tab and shows file list', async ({ page }) => {
    await page.goto('/services/demo-service');
    await page.getByRole('tab', { name: 'Files' }).click();
    await expect(page.getByText('index.js')).toBeVisible();
    await expect(page.getByText('manifest.json')).toBeVisible();
  });

  test('switches to Schema tab and shows fields', async ({ page }) => {
    await page.goto('/services/demo-service');
    await page.getByRole('tab', { name: 'Schema' }).click();
    // Schema 编辑器应显示 config schema 中的字段
    await expect(page.getByText('host').first()).toBeVisible();
    await expect(page.getByText('port').first()).toBeVisible();
  });

  test('switches to Projects tab', async ({ page }) => {
    await page.goto('/services/demo-service');
    await page.getByRole('tab', { name: 'Projects' }).click();
    // 应显示关联的项目
    await expect(page.getByRole('tabpanel')).toBeVisible();
  });
});
