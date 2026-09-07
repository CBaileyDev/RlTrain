async page => {
  const source = await (await page.request.get('http://127.0.0.1:1420/src/App.svelte')).text();
  const schemaPath = source.match(/from "([^"\n]*run\.schema\.json)\?import"/)[1];
  const schema = await (await page.request.get(new URL(schemaPath,'http://127.0.0.1:1420/').href)).json();
  const config = Object.fromEntries(Object.entries(schema.properties).map(([k,v])=>[k,v.default]));
  await page.addInitScript(({config}) => {
    localStorage.setItem('rlstudio-onboarded','yes');
    localStorage.setItem('rlstudio-config',JSON.stringify(config));
    const callbacks = new Map(), listeners = new Map();
    let id = 1;
    window.testCalls = [];
    window.emitTest = payload => callbacks.get(listeners.get('engine-event'))?.({payload});
    window.__TAURI_EVENT_PLUGIN_INTERNALS__ = {unregisterListener:()=>{}};
    window.__TAURI_INTERNALS__ = {
      metadata:{currentWindow:{label:'main'},currentWebview:{label:'main'}},
      transformCallback: cb => {callbacks.set(id,cb);return id++;},
      unregisterCallback: id => callbacks.delete(id),
      invoke: async (cmd,args) => {
        window.testCalls.push({cmd,args});
        if(cmd==='plugin:event|listen'){listeners.set(args.event,args.handler);return id++;}
        if(cmd==='environment')return {engineAvailable:true};
        if(cmd==='assistant_settings')return {provider:'NeoToken',baseUrl:'https://api.v2.neokens.com/v1',defaultModel:'claude-sonnet-4-6',models:['claude-sonnet-4-6'],keyConfigured:true,catalogAvailable:true};
        if(cmd==='list_runs')return [{id:'fixture',path:'fixture',config,summary:{status:'completed',steps:5000000000},checkpoints:[{path:'fixture/checkpoint',metadata:{iteration:1200,steps:5000000000}}]}];
        if(cmd==='start_engine') {window.emitTest({type:'started',run:'fixture/resumed',device:'cpu',config,steps:5000000000,iteration:1200,initialSteps:5000000000,initialIteration:1200});return;}
        if(cmd==='ask_assistant') {
          if(window.deferAssistant) await new Promise(resolve=>window.resolveAssistant=resolve);
          return {explanation:'Fixture: encourage ball contact based on the observed low touch rate.',changes:{touch:6,learningRate:0.00012}};
        }
        if(cmd==='engine_control' && args.command.type==='rewards')return;
        if(cmd==='run_metrics')return Array.from({length:150},(_,i)=>({steps:5000000000+i*32768,iteration:1200+i,reward:Math.sin(i/12)+i/80,entropy:3.5-i/300,valueLoss:0.8/(1+i/20),policyLoss:-0.01,kl:0.01+Math.sin(i/10)*0.003,clipFraction:0.15,explainedVariance:i/180,stepsPerSecond:22000,touches:i,goals:2,episodes:8,elapsedSeconds:i*2}));
      }
    };
  },{config});
  await page.reload();
  await page.getByRole('button',{name:'02 Train',exact:true}).click();
  await page.getByLabel('Starting checkpoint').selectOption('fixture/checkpoint');
  await page.getByRole('button',{name:'Resume checkpoint',exact:true}).click();
  await page.getByRole('button',{name:'02 Train',exact:true}).click();
  if(!(await page.locator('.metric-strip').textContent()).includes('5,000,000,000'))throw Error('Restored count missing before metrics');
  await page.screenshot({path:'engine/build/train-resume-ui.png'});
  const rows = await page.evaluate(()=>window.__TAURI_INTERNALS__.invoke('run_metrics',{}));
  await page.evaluate(rows=>rows.forEach(row=>window.emitTest({type:'metrics',...row})),rows);
  await page.getByRole('button',{name:'09 Graphs',exact:true}).click();
  if(await page.locator('.chart-panel canvas').count()!==4)throw Error('Populated charts missing');
  await page.getByLabel('Chart smoothing').fill('0.5');
  await page.screenshot({path:'engine/build/graphs-ui.png',fullPage:true});
  await page.getByRole('combobox',{name:/^Metrics/}).selectOption('Stability');
  await page.getByRole('combobox',{name:/^Metrics/}).selectOption('Performance');
  await page.getByRole('button',{name:'06 Assistant',exact:true}).click();
  await page.getByRole('button',{name:'Tune with AI',exact:true}).click();
  await page.getByRole('button',{name:'Changes applied',exact:true}).waitFor();
  const calls = await page.evaluate(()=>window.testCalls);
  const ask = calls.find(c=>c.cmd==='ask_assistant');
  if(ask.args.question!=='' || ask.args.metrics.length!==30)throw Error('Automatic context missing');
  const rewards = calls.find(c=>c.cmd==='engine_control' && c.args.command.type==='rewards');
  if(rewards.args.command.values.touch!==6 || 'learningRate' in rewards.args.command.values)throw Error('Live/staged tuning incorrect');
  await page.screenshot({path:'engine/build/assistant-ui.png',fullPage:true});
  await page.evaluate(()=>{window.deferAssistant=true;});
  await page.getByRole('button',{name:'Tune with AI',exact:true}).click();
  await page.getByRole('button',{name:'03 Rewards',exact:true}).click();
  await page.locator('#reward-touch').fill('9');
  await page.evaluate(()=>window.resolveAssistant());
  await page.getByRole('status').filter({hasText:'AI response discarded because the experiment or settings changed.'}).waitFor();
  if(await page.locator('#reward-touch').inputValue()!=='9')throw Error('Stale assistant overwrote edit');
  await page.evaluate(()=>window.emitTest({type:'evaluation',matches:100,blueWins:60,orangeWins:30,draws:10}));
  await page.getByRole('button',{name:'04 Match',exact:true}).click();
  if(!(await page.locator('.resume-panel').textContent()).includes('65%'))throw Error('Skill score calculation missing');
  const errors = await page.locator('.alert.error').count();
  if(errors)throw Error('Unexpected application error');
  return 'PASS: restored 5B counter before metrics; populated graphs and smoothing; automatic context; mixed live rewards/staged learning settings; stale AI rejection; relative skill';
}
