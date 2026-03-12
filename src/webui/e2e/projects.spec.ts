import { test, expect } from '@playwright/test';
import { setupApiMocks } from './helpers/setup-mocks';

test.describe('Projects', () => {
  test.beforeEach(async ({ page }) => {
    await setupApiMocks(page);
  });

  test('shows project list', async ({ page }) => {
    await page.goto('/projects');
    await expect(page.getByText('Demo Project').first()).toBeVisible();
    await expect(page.getByText('Test Project').first()).toBeVisible();
  });

  test('navigates to project detail', async ({ page }) => {
    await page.goto('/projects');
    await page.getByText('Demo Project').first().click();
    await expect(page).toHaveURL(/\/projects\/demo-project/);
  });

  test('shows project detail page', async ({ page }) => {
    await page.goto('/projects/demo-project');
    await expect(page.getByRole('heading', { name: 'Demo Project' })).toBeVisible();
  });

  test('shows project detail tabs', async ({ page }) => {
    await page.goto('/projects/demo-project');
    // 验证所有 5 个 tab 存在且顺序正确：Overview, Configuration, Parameters, Instances, Logs
    const tabs = page.getByRole('tab');
    await expect(tabs).toHaveText(['Overview', 'Configuration', 'Parameters', 'Instances', 'Logs']);
  });

  test('shows project overview with action buttons', async ({ page }) => {
    await page.goto('/projects/demo-project');
    // Overview 页面有 Start/Stop/Reload 按钮
    await expect(page.getByRole('button', { name: /start/i }).first()).toBeVisible();
    await expect(page.getByRole('button', { name: /stop/i }).first()).toBeVisible();
    await expect(page.getByRole('button', { name: /reload/i })).toBeVisible();
    await expect(page.getByTestId('project-config-test-commands')).toHaveCount(0);
  });

  test('shows test commands in parameters tab', async ({ page }) => {
    await page.goto('/projects/demo-project');
    await page.getByRole('tab', { name: 'Parameters' }).click();
    const panel = page.getByTestId('project-config-test-commands');
    await expect(panel).toBeVisible();
    await expect(panel).toContainText('--config-file="projects/demo-project/param.json"');
    await expect(panel).not.toContainText('cd "/data"');
  });

  test('switches project detail tabs', async ({ page }) => {
    await page.goto('/projects/demo-project');
    // 切换到 Parameters tab
    await page.getByRole('tab', { name: 'Parameters' }).click();
    await expect(page).toHaveURL(/tab=parameters/);

    // 切换到 Configuration tab
    await page.getByRole('tab', { name: 'Configuration' }).click();
    await expect(page).toHaveURL(/tab=configuration/);
  });

  test('opens create project wizard', async ({ page }) => {
    await page.goto('/projects');
    const createBtn = page.getByRole('button', { name: /create|new/i });
    await createBtn.click();
    // 创建项目是模态弹窗
    await expect(page.getByRole('dialog', { name: 'Create Project' })).toBeVisible();
    // 向导步骤和服务列表可见
    await expect(page.getByText('Demo Service').first()).toBeVisible();
  });

  test('shows enabled/disabled status', async ({ page }) => {
    await page.goto('/projects');
    const enableSwitch = page.locator('.ant-switch-checked').first();
    await expect(enableSwitch).toBeVisible();
  });
});
