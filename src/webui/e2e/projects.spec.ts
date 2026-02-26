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
    // 使用 heading 精确匹配标题，避免与表格中 "Demo Project" 冲突
    await expect(page.getByRole('heading', { name: 'Demo Project' })).toBeVisible();
  });

  test('opens create project wizard', async ({ page }) => {
    await page.goto('/projects');
    const createBtn = page.getByRole('button', { name: /create|new/i });
    await createBtn.click();
    // 创建项目是模态弹窗，而非路由跳转
    await expect(page.getByText('Create Project')).toBeVisible();
  });

  test('shows enabled/disabled status', async ({ page }) => {
    await page.goto('/projects');
    // runtime batch mock 返回 "running" 状态；若无 runtime 则显示 STOPPED
    // 使用 switch 控件验证 enabled 状态更可靠
    const enableSwitch = page.locator('.ant-switch-checked').first();
    await expect(enableSwitch).toBeVisible();
  });
});
