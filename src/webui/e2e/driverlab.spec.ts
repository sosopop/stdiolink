import { test, expect } from '@playwright/test';
import { setupApiMocks } from './helpers/setup-mocks';

test.describe('DriverLab', () => {
  test.beforeEach(async ({ page }) => {
    await setupApiMocks(page);
  });

  test('shows driverlab page', async ({ page }) => {
    await page.goto('/driverlab');
    await expect(page.getByRole('heading', { name: /Driver\s*Lab/i })).toBeVisible();
  });

  test('shows driver selection', async ({ page }) => {
    await page.goto('/driverlab');
    await expect(page.getByTestId('connection-panel')).toBeVisible();
    // 应有 Target Driver 选择器
    await expect(page.getByText(/Target Driver/i)).toBeVisible();
  });

  test('shows connection mode options', async ({ page }) => {
    await page.goto('/driverlab');
    // OneShot 和 KeepAlive 是 radio 按钮
    await expect(page.getByRole('radio', { name: 'OneShot' })).toBeVisible();
    await expect(page.getByRole('radio', { name: 'KeepAlive' })).toBeVisible();
  });

  test('shows connect button', async ({ page }) => {
    await page.goto('/driverlab');
    await expect(page.getByRole('button', { name: /connect/i })).toBeVisible();
  });

  test('shows message stream panel', async ({ page }) => {
    await page.goto('/driverlab');
    await expect(page.getByTestId('message-stream')).toBeVisible();
    // 消息流面板应有工具栏
    await expect(page.getByRole('button', { name: /clear/i })).toBeVisible();
  });

  test('shows command palette', async ({ page }) => {
    await page.goto('/driverlab');
    await expect(page.getByText(/Command Palette/i)).toBeVisible();
  });

  test('shows status bar with disconnected state', async ({ page }) => {
    await page.goto('/driverlab');
    await expect(page.getByText(/disconnected/i).first()).toBeVisible();
  });

  test('shows empty message state', async ({ page }) => {
    await page.goto('/driverlab');
    await expect(page.getByText(/no messages/i)).toBeVisible();
  });
});
