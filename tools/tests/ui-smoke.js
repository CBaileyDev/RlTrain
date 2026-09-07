async page => {
  await page.getByRole('button', {name: '01 Overview', exact:true}).click();
  await page.screenshot({path:'engine/build/overview-ui.png'});
  if (await page.locator('.chart-panel').count() !== 4) throw new Error('Charts missing');
  await page.getByRole('button', {name:'03 Rewards',exact:true}).click();
  await page.locator('#reward-touch').fill('7');
  await page.getByRole('button', {name:'Apply reward weights',exact:true}).click();
  if (!(await page.getByRole('status').textContent()).includes('ready')) throw new Error('Reward editing failed');
  await page.getByRole('button', {name:'06 Assistant',exact:true}).click();
  await page.getByRole('button', {name:'Local diagnostic',exact:true}).click();
  if (!(await page.locator('.proposal-text').textContent()).includes('rule-based')) throw new Error('Local diagnostic not labeled');
  await page.getByRole('button', {name:'07 Learn',exact:true}).click();
  if (await page.locator('.document h1').count() !== 1) throw new Error('Markdown not rendered');
  await page.getByRole('button', {name:'08 Settings',exact:true}).click();
  await page.getByRole('button', {name:'Paper A brighter reading space'}).click();
  await page.screenshot({path:'engine/build/paper-ui.png'});
  await page.reload();
  await page.waitForSelector('.shell');
  if (await page.locator('html').getAttribute('data-theme') !== 'paper') throw new Error('Theme not persisted');
  await page.getByRole('button', {name:'08 Settings',exact:true}).click();
  await page.getByRole('button', {name:'Forest A cool green workspace'}).click();
  await page.getByRole('button', {name:'04 Match',exact:true}).click();
  if (!(await page.getByRole('button',{name:'Watch match',exact:true}).isDisabled())) throw new Error('Playback enabled without checkpoint');
  await page.getByRole('button', {name:'05 Runs',exact:true}).click();
  if (!(await page.locator('.empty-state').textContent()).includes('first experiment')) throw new Error('Empty library missing');
  await page.setViewportSize({width:1100,height:700});
  await page.getByRole('button', {name:'01 Overview',exact:true}).click();
  const overflow = await page.evaluate(() => document.documentElement.scrollWidth > window.innerWidth);
  if (overflow) throw new Error('Horizontal overflow');
  await page.screenshot({path:'engine/build/overview-small.png'});
  const warnings = [];
  const capture = message => { if (message.text().includes('Too many active WebGL')) warnings.push(message.text()); };
  page.on('console', capture);
  for (let i = 0; i < 18; i++) {
    await page.getByRole('button', {name:'04 Match',exact:true}).click();
    await page.getByRole('button', {name:'01 Overview',exact:true}).click();
  }
  page.off('console', capture);
  if (warnings.length) throw new Error('Viewer leaked WebGL contexts');
  return 'PASS: navigation, reward editing, diagnostic, Markdown, theme persistence, empty library, disabled playback, small layout';
}

