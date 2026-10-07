// My stops: add a favourite (stop + route -> directions -> add), reorder, remove; RTC's refusals are explained
const { test, expect, state, open, posts } = require('./fixtures');

async function find(page, stop, route) {
  await page.locator('#fav-stop').fill(stop);
  await page.locator('#fav-route').fill(route);
  await page.locator('#fav-find').click();
}

test('add a stop: its directions come from the display, the list is saved', async ({ page, request }) => {
  await open(page);
  await expect(page.locator('#fav-none')).toBeVisible();
  await find(page, '1025', '800');
  await expect(page.locator('#fav-dir option')).toHaveText(['Colline Parlementaire', 'Terminus Chute-Montmorency']);
  await page.locator('#fav-add').click();
  await expect(page.locator('#fav-list .fav')).toHaveCount(1);
  await expect(page.locator('#fav-list .fav')).toContainText('800');
  await expect(page.locator('#fav-list .fav')).toContainText('St-Dominique · 1025');
  await expect(page.locator('#fav-none')).toBeHidden();
  expect((await state(request)).favs).toEqual([{ stop: '1025', route: '800', dir: '0' }]);
  await expect(page.locator('#fav-stop')).toHaveValue('');           // ready for the next one
  await expect(page.locator('#fav-dir-box')).toBeHidden();
});

test('reorder and remove', async ({ page, request }) => {
  await open(page);
  for (const [stop, dir] of [['1025', 0], ['1026', 1]]) {
    await find(page, stop, '800');
    await page.locator('#fav-dir').selectOption(String(dir));
    await page.locator('#fav-add').click();
    await expect(page.locator('#fav-stop')).toHaveValue('');
  }
  await expect(page.locator('#fav-list .fav')).toHaveCount(2);
  await page.locator('#fav-list .fav').nth(1).getByTitle('Move up').click();
  await expect.poll(async () => (await state(request)).favs.map(f => f.stop)).toEqual(['1026', '1025']);
  await page.locator('#fav-list .fav').nth(0).getByTitle('Remove').click();
  await expect.poll(async () => (await state(request)).favs.map(f => f.stop)).toEqual(['1025']);
  await expect(page.locator('#fav-list .fav')).toHaveCount(1);
});

test('refusals: unknown route, stop not served, RTC down, bad input, duplicate', async ({ page, request }) => {
  await open(page);
  await find(page, '1025', '4242');
  await expect(page.locator('#msg')).toHaveText("Route 4242 doesn't exist.");
  await expect(page.locator('#fav-dir-box')).toBeHidden();

  await find(page, '9999', '800');                                   // route found; the stop isn't on it
  await page.locator('#fav-add').click();
  await expect(page.locator('#msg')).toHaveText("Route 800 doesn't stop at 9999 in that direction.");
  expect((await state(request)).favs).toEqual([]);

  await find(page, '1025', '999');
  await expect(page.locator('#msg')).toHaveText("The RTC didn't answer. Try again in a moment.");

  const before = (await posts(request, '/api/route')).length;
  await find(page, '10&25', '800');
  await expect(page.locator('#msg')).toHaveText('Check the stop and route numbers.');
  expect((await posts(request, '/api/route')).length, 'nothing asked for bad input').toBe(before);

  await find(page, '1025', '800');
  await page.locator('#fav-add').click();
  await expect(page.locator('#fav-list .fav')).toHaveCount(1);
  await find(page, '1025', '800');
  await page.locator('#fav-add').click();
  await expect(page.locator('#msg')).toHaveText('That stop is already in the list.');
});

test('in French', async ({ page, request }) => {
  await request.post('/api/settings', { data: { lang: 'fr' }, headers: { 'X-Key': '0123456789abcdef' } });
  await open(page);
  await expect(page.locator('h2').first()).toHaveText('Mes arrêts');
  await find(page, '9999', '800');
  await page.locator('#fav-add').click();
  await expect(page.locator('#msg')).toHaveText('Le parcours 800 ne s\'arrête pas au 9999 dans cette direction.');
});

test('on the setup network (no key): a stop can be added', async ({ page, request }) => {
  // A phone that joined the display's setup network through Wi-Fi setup got 403 on "Find directions" (2026-10-06):
  // the stop routes were marked "never on the setup network" in main.c
  await request.post('/__setup');
  await open(page, null);
  await find(page, '1025', '800');
  await expect(page.locator('#fav-dir option')).toHaveCount(2);
  await page.locator('#fav-add').click();
  await expect(page.locator('#fav-list .fav')).toHaveCount(1);
  expect((await state(request)).favs).toEqual([{ stop: '1025', route: '800', dir: '0' }]);
});
