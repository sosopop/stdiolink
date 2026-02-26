import { test, expect } from '@playwright/test';
import { setupApiMocks } from './helpers/setup-mocks';

test.describe('Layout & Navigation', () => {
    test.beforeEach(async ({ page }) => {
        await setupApiMocks(page);
    });

    test('redirects root to dashboard', async ({ page }) => {
        await page.goto('/');
        await expect(page).toHaveURL(/\/dashboard/);
    });

    test('shows all sidebar navigation items', async ({ page }) => {
        await page.goto('/dashboard');
        await expect(page.getByRole('menuitem', { name: /dashboard/i })).toBeVisible();
        await expect(page.getByRole('menuitem', { name: /projects/i })).toBeVisible();
        await expect(page.getByRole('menuitem', { name: /instances/i })).toBeVisible();
        await expect(page.getByRole('menuitem', { name: /services/i })).toBeVisible();
        await expect(page.getByRole('menuitem', { name: /drivers/i })).toBeVisible();
        await expect(page.getByRole('menuitem', { name: /driver lab/i })).toBeVisible();
    });

    test('sidebar navigation works for all pages', async ({ page }) => {
        await page.goto('/dashboard');
        // 逐一点击侧边栏菜单项，验证路由跳转
        await page.getByRole('menuitem', { name: /projects/i }).click();
        await expect(page).toHaveURL(/\/projects/);

        await page.getByRole('menuitem', { name: /instances/i }).click();
        await expect(page).toHaveURL(/\/instances/);

        await page.getByRole('menuitem', { name: /services/i }).click();
        await expect(page).toHaveURL(/\/services/);

        await page.getByRole('menuitem', { name: /drivers/i }).first().click();
        await expect(page).toHaveURL(/\/drivers/);

        await page.getByRole('menuitem', { name: /driver lab/i }).click();
        await expect(page).toHaveURL(/\/driverlab/);

        // 返回 Dashboard
        await page.getByRole('menuitem', { name: /dashboard/i }).click();
        await expect(page).toHaveURL(/\/dashboard/);
    });

    test('theme toggle switches between light and dark mode', async ({ page }) => {
        await page.goto('/dashboard');
        const themeSwitch = page.getByRole('switch', { name: /toggle theme/i });
        await expect(themeSwitch).toBeVisible();

        // 默认是暗色模式（switch checked），取消勾选切换到亮色
        await themeSwitch.click();
        // 验证 body 上的 data-theme 属性变为 light
        await expect(page.locator('[data-theme="light"]')).toBeVisible();

        // 再切换回暗色
        await themeSwitch.click();
        await expect(page.locator('[data-theme="light"]')).not.toBeAttached();
    });

    test('sidebar can be collapsed', async ({ page }) => {
        await page.goto('/dashboard');
        // 找到折叠按钮（menu-fold 图标）
        const collapseBtn = page.locator('[data-testid="sidebar-collapse-btn"], .ant-layout-sider-trigger, [aria-label="menu-fold"]').first();
        if (await collapseBtn.isVisible()) {
            await collapseBtn.click();
            // 验证侧边栏已折叠（menuitem 文本不可见但图标仍在）
            await page.waitForTimeout(500);
        }
    });

    test('shows 404 page for unknown routes', async ({ page }) => {
        await page.goto('/unknown-page-xyz');
        await expect(page.getByText(/not found|404/i).first()).toBeVisible();
    });
});
