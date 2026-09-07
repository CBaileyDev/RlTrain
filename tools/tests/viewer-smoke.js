async page => {
  const errors = [];
  page.on('pageerror', error => errors.push(error.message));
  await page.addInitScript(() => {
    localStorage.setItem('rlstudio-onboarded', 'yes');
    localStorage.setItem('rlstudio-theme', 'forest');
    const callbacks = new Map(); const listeners = new Map(); let next = 1;
    window.__TAURI_INTERNALS__ = {
      metadata: {currentWindow: {label: 'main'}, currentWebview: {label: 'main'}},
      transformCallback(callback) { const id = next++; callbacks.set(id, callback); return id; },
      async invoke(command, args) {
        if (command === 'plugin:event|listen') { listeners.set(args.event, args.handler); return args.handler; }
        if (command === 'plugin:event|unlisten') return;
        if (command === 'environment') return {engineAvailable: false};
        if (command === 'list_runs') return [];
        if (command === 'assistant_settings') return {models: [], defaultModel: '', keyConfigured: false};
        return null;
      }
    };
    window.__TAURI_EVENT_PLUGIN_INTERNALS__ = {unregisterListener() {}};
    window.viewerFixture = { tick: 0, demoed: false, paused: false, offset: 0 };
    setInterval(() => {
      const f = window.viewerFixture; if (f.paused) return;
      f.tick += 6; const angle = f.tick / 500;
      const car = (id, team, phase) => ({id, team, pos:[Math.sin(angle + phase) * 1600 + f.offset, Math.cos(angle + phase) * 1800, 36], forward:[Math.cos(angle + phase), -Math.sin(angle + phase), 0], up:[0,0,1], boost:id === 10 ? 0.5 : 75, demoed:id === 10 && f.demoed});
      const callback = callbacks.get(listeners.get('engine-event'));
      callback?.({event:'engine-event', payload:{type:'frame', tick:f.tick, ball:[Math.sin(angle * .7) * 900,Math.cos(angle * .7) * 1100,140], cars:[car(10,0,0),car(20,1,Math.PI)], score:[2,1]}});
    }, 50);
  });
  await page.reload();
  await page.setViewportSize({width:1440,height:1000});
  await page.getByRole('button', {name:'04 Match',exact:true}).click();
  await page.waitForFunction(() => document.querySelector('.panel-heading')?.textContent.includes('Tick'));
  await page.waitForTimeout(800);
  await page.screenshot({path:'engine/build/viewer-orbit.png'});
  await page.getByRole('button',{name:'Third person',exact:true}).click();
  await page.waitForTimeout(1000);
  if (await page.getByRole('button',{name:'Third person',exact:true}).getAttribute('aria-pressed') !== 'true') throw new Error('Third person mode not selected');
  await page.screenshot({path:'engine/build/viewer-chase.png'});
  await page.getByLabel('Follow car').selectOption('20');
  await page.waitForTimeout(700);
  if (!(await page.locator('.match-footer').textContent()).includes('Boost 75%')) throw new Error('Wrong car boost');
  await page.getByLabel('Follow car').selectOption('10');
  if (!(await page.locator('.match-footer').textContent()).includes('Boost 1%')) throw new Error('Small boost incorrectly rescaled');
  await page.evaluate(() => window.viewerFixture.demoed = true);
  await page.waitForTimeout(200);
  if (!(await page.locator('.match-footer').textContent()).includes('Demolished')) throw new Error('Demolition status missing');
  await page.evaluate(() => {window.viewerFixture.demoed = false; window.viewerFixture.tick = 0; window.viewerFixture.offset = 1000;});
  await page.waitForTimeout(500);
  await page.getByRole('button',{name:'Ball tracking',exact:true}).click();
  await page.waitForTimeout(500);
  await page.getByRole('button',{name:'Reset',exact:true}).click();
  if (await page.getByRole('button',{name:'Orbit',exact:true}).getAttribute('aria-pressed') !== 'true') throw new Error('Reset did not restore orbit');
  await page.getByRole('button',{name:'Fullscreen',exact:true}).click();
  await page.waitForTimeout(500);
  if (!await page.evaluate(() => !!document.fullscreenElement)) throw new Error('Fullscreen not entered');
  await page.screenshot({path:'engine/build/viewer-fullscreen.png'});
  await page.getByRole('button',{name:'Fullscreen',exact:true}).click();
  await page.evaluate(() => window.viewerFixture.paused = true);
  await page.waitForTimeout(2800);
  if (!(await page.locator('.match-panel .panel-heading').textContent()).includes('Paused')) throw new Error('Stale stream not identified');
  await page.setViewportSize({width:1100,height:700});
  await page.getByRole('button',{name:'Third person',exact:true}).click();
  await page.waitForTimeout(800);
  await page.screenshot({path:'engine/build/viewer-small.png'});
  const footer = await page.locator('.match-footer').boundingBox();
  if (!footer || footer.y + footer.height > 700) throw new Error('Viewer controls extend below small viewport');
  if (await page.evaluate(() => document.documentElement.scrollWidth > innerWidth)) throw new Error('Viewer causes horizontal overflow');
  if (errors.length) throw new Error(errors.join('\n'));
  return 'PASS: moving fixture stream, orbit/ball/chase, car selection, boost, demolition, tick reset/teleport, fullscreen, paused stream, small viewport; inspect saved images for visual QA';
}
