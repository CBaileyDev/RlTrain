<script lang="ts">
  import { onMount } from 'svelte';
  import * as THREE from 'three';
  import { OrbitControls } from 'three/addons/controls/OrbitControls.js';
  import type { Frame } from './types';
  let { frame = null, large = false }: { frame?: Frame | null; large?: boolean } = $props();
  let container: HTMLDivElement;
  let panel: HTMLElement;
  let failure = $state('');
  let mode = $state<'orbit' | 'ball' | 'chase'>('orbit');
  let selected = $state(-1);
  let distance = $state(9);
  let reset = $state(0);
  let stale = $state(false);
  const followed = $derived(frame?.cars.find((car) => car.id === selected) ?? frame?.cars[0]);
  async function fullscreen() {
    try {
      if (document.fullscreenElement === panel) await document.exitFullscreen();
      else await panel.requestFullscreen();
    } catch {
      failure = 'Fullscreen is unavailable in this window.';
    }
  }
  onMount(() => {
    let renderer: THREE.WebGLRenderer;
    try {
      renderer = new THREE.WebGLRenderer({ antialias: true, powerPreference: 'high-performance' });
    } catch {
      failure =
        '3D rendering is unavailable. Enable hardware acceleration or update the graphics driver.';
      return;
    }
    renderer.setPixelRatio(Math.min(window.devicePixelRatio || 1, 2));
    renderer.setClearColor('#09121e');
    renderer.shadowMap.enabled = true;
    renderer.shadowMap.type = THREE.PCFSoftShadowMap;
    renderer.toneMapping = THREE.ACESFilmicToneMapping;
    renderer.toneMappingExposure = 1.25;
    container.appendChild(renderer.domElement);
    const scene = new THREE.Scene();
    scene.fog = new THREE.Fog('#09121e', 140, 330);
    const camera = new THREE.PerspectiveCamera(48, 1, 0.08, 450);
    const controls = new OrbitControls(camera, renderer.domElement);
    controls.enableDamping = true;
    controls.dampingFactor = 0.08;
    controls.minDistance = 5;
    controls.maxDistance = 220;
    controls.maxPolarAngle = Math.PI / 2 - 0.015;
    function home() {
      camera.position.set(82, 76, 99);
      controls.target.set(0, 0, 0);
      controls.update();
    }
    home();
    scene.add(new THREE.HemisphereLight('#c6e7ff', '#18352d', 2.3));
    const sun = new THREE.DirectionalLight('#ecf6ff', 3.3);
    sun.position.set(-28, 70, 20);
    sun.castShadow = true;
    sun.shadow.mapSize.set(2048, 2048);
    Object.assign(sun.shadow.camera, {
      left: -68,
      right: 68,
      top: 75,
      bottom: -75,
      near: 1,
      far: 150,
    });
    sun.shadow.normalBias = 0.12;
    sun.shadow.bias = -0.0001;
    scene.add(sun);
    const fill = new THREE.DirectionalLight('#83b7ff', 1.1);
    fill.position.set(50, 25, -60);
    scene.add(fill);
    const textures: THREE.Texture[] = [];
    function material(color: string, roughness = 0.65, metalness = 0) {
      return new THREE.MeshStandardMaterial({ color, roughness, metalness });
    }
    const turfCanvas = document.createElement('canvas');
    turfCanvas.width = 512;
    turfCanvas.height = 512;
    const ctx = turfCanvas.getContext('2d');
    if (ctx) {
      ctx.fillStyle = '#285a4b';
      ctx.fillRect(0, 0, 512, 512);
      for (let i = 0; i < 16; i++) {
        ctx.fillStyle = i % 2 ? '#285347' : '#306150';
        ctx.fillRect(0, i * 32, 512, 32);
      }
      // Deterministic grass grain keeps the arena consistent across mounts.
      let seed = 731;
      for (let i = 0; i < 22000; i++) {
        seed = (seed * 16807) % 2147483647;
        const x = seed % 512;
        seed = (seed * 16807) % 2147483647;
        const y = seed % 512;
        ctx.fillStyle = i % 2 ? '#ffffff09' : '#0000000b';
        ctx.fillRect(x, y, 1, 2);
      }
    }
    const turf = new THREE.CanvasTexture(turfCanvas);
    turf.colorSpace = THREE.SRGBColorSpace;
    turf.anisotropy = renderer.capabilities.getMaxAnisotropy();
    textures.push(turf);
    const floor = new THREE.Mesh(
      new THREE.PlaneGeometry(81.92, 102.4),
      new THREE.MeshStandardMaterial({ map: turf, roughness: 0.94 }),
    );
    floor.rotation.x = -Math.PI / 2;
    floor.receiveShadow = true;
    scene.add(floor);
    function box(
      w: number,
      h: number,
      d: number,
      x: number,
      y: number,
      z: number,
      mat: THREE.Material,
      parent: THREE.Object3D = scene,
    ) {
      const mesh = new THREE.Mesh(new THREE.BoxGeometry(w, h, d), mat);
      mesh.position.set(x, y, z);
      mesh.castShadow = true;
      mesh.receiveShadow = true;
      parent.add(mesh);
      return mesh;
    }
    function line(points: THREE.Vector3[], color: string, opacity = 1) {
      const object = new THREE.Line(
        new THREE.BufferGeometry().setFromPoints(points),
        new THREE.LineBasicMaterial({ color, transparent: opacity < 1, opacity }),
      );
      scene.add(object);
      return object;
    }
    const fieldWhite = '#a7c9bd';
    line(
      [
        new THREE.Vector3(-40.8, 0.04, -51),
        new THREE.Vector3(40.8, 0.04, -51),
        new THREE.Vector3(40.8, 0.04, 51),
        new THREE.Vector3(-40.8, 0.04, 51),
        new THREE.Vector3(-40.8, 0.04, -51),
      ],
      fieldWhite,
    );
    line([new THREE.Vector3(-40.8, 0.04, 0), new THREE.Vector3(40.8, 0.04, 0)], fieldWhite);
    const circle = new THREE.Mesh(
      new THREE.RingGeometry(9.85, 10, 96),
      new THREE.MeshBasicMaterial({ color: fieldWhite, side: THREE.DoubleSide }),
    );
    circle.rotation.x = -Math.PI / 2;
    circle.position.y = 0.045;
    scene.add(circle);
    const trim = material('#243545', 0.4, 0.5);
    const wallMaterial = new THREE.MeshPhysicalMaterial({
      color: '#8acfe6',
      transparent: true,
      opacity: 0.075,
      roughness: 0.2,
      depthWrite: false,
      side: THREE.DoubleSide,
    });
    for (const sign of [-1, 1]) {
      const teamColor = sign > 0 ? '#429bff' : '#ff9b42';
      const glow = new THREE.MeshStandardMaterial({
        color: teamColor,
        emissive: teamColor,
        emissiveIntensity: 1.8,
      });
      box(0.3, 1.1, 102.4, sign * 41.1, 0.55, 0, trim);
      box(0.12, 0.08, 102.4, sign * 40.93, 1.13, 0, glow);
      const side = box(0.08, 19.3, 102.4, sign * 41, 10.8, 0, wallMaterial);
      side.castShadow = false;
      for (let z = -48; z <= 48; z += 12) box(0.12, 8, 0.12, sign * 41.1, 4, z, trim);
      for (const x of [-25.05, 25.05]) box(31.9, 1.1, 0.4, x, 0.55, sign * 51.4, trim);
      for (const x of [-8.93, 8.93]) box(0.25, 6.43, 0.25, x, 3.215, sign * 51.2, glow);
      box(18.1, 0.25, 0.25, 0, 6.43, sign * 51.2, glow);
      box(17.86, 0.08, 8.8, 0, 0.04, sign * 55.6, material(sign > 0 ? '#194a70' : '#704329'));
      for (let x = -8.9; x <= 9; x += 1.12) {
        line(
          [
            new THREE.Vector3(x, 0, sign * 60),
            new THREE.Vector3(x, 6.43, sign * 60),
            new THREE.Vector3(x, 6.43, sign * 51.2),
          ],
          '#8396a7',
          0.35,
        );
      }
      for (let y = 0; y <= 6.5; y += 0.8)
        line(
          [
            new THREE.Vector3(-8.93, y, sign * 51.2),
            new THREE.Vector3(-8.93, y, sign * 60),
            new THREE.Vector3(8.93, y, sign * 60),
            new THREE.Vector3(8.93, y, sign * 51.2),
          ],
          '#8396a7',
          0.35,
        );
      line(
        [
          new THREE.Vector3(-17, 0.05, sign * 51),
          new THREE.Vector3(-17, 0.05, sign * 36),
          new THREE.Vector3(17, 0.05, sign * 36),
          new THREE.Vector3(17, 0.05, sign * 51),
        ],
        fieldWhite,
        0.65,
      );
      // Stadium seating outside the playable bounds, with illuminated fascia.
      for (let tier = 0; tier < 3; tier++) {
        box(
          5,
          2.5,
          117,
          sign * (47 + tier * 5),
          2 + tier * 3.5,
          0,
          material(tier % 2 ? '#182c40' : '#22394b'),
        );
        box(0.1, 0.1, 117, sign * (44.5 + tier * 5), 3.3 + tier * 3.5, 0, glow);
      }
    }
    const ball = new THREE.Group();
    const sphere = new THREE.Mesh(
      new THREE.SphereGeometry(0.9125, 40, 28),
      material('#e2e9dd', 0.37, 0.18),
    );
    sphere.castShadow = true;
    ball.add(sphere);
    const panelGeometry = new THREE.IcosahedronGeometry(1, 0);
    const edges = new THREE.EdgesGeometry(panelGeometry);
    const edgePositions = edges.getAttribute('position');
    const seamPoints: THREE.Vector3[] = [];
    for (let i = 0; i < edgePositions.count; i += 2) {
      const a = new THREE.Vector3().fromBufferAttribute(edgePositions, i);
      const b = new THREE.Vector3().fromBufferAttribute(edgePositions, i + 1);
      for (let j = 0; j < 12; j++)
        seamPoints.push(
          a
            .clone()
            .lerp(b, j / 12)
            .normalize()
            .multiplyScalar(0.916),
          a
            .clone()
            .lerp(b, (j + 1) / 12)
            .normalize()
            .multiplyScalar(0.916),
        );
    }
    edges.dispose();
    panelGeometry.dispose();
    const seams = new THREE.LineSegments(
      new THREE.BufferGeometry().setFromPoints(seamPoints),
      new THREE.LineBasicMaterial({ color: '#3f505a' }),
    );
    ball.add(seams);
    ball.position.y = 0.93;
    ball.visible = false;
    scene.add(ball);
    const marker = new THREE.Mesh(
      new THREE.RingGeometry(1.25, 1.38, 48),
      new THREE.MeshBasicMaterial({
        color: '#e8efdb',
        transparent: true,
        opacity: 0.65,
        depthWrite: false,
      }),
    );
    marker.rotation.x = -Math.PI / 2;
    marker.visible = false;
    scene.add(marker);
    const cars = new Map<number, THREE.Group>();
    const rubber = material('#10151b', 0.9),
      glass = material('#263e50', 0.16, 0.65),
      chrome = material('#a1b2bb', 0.3, 0.8);
    function createCar(team: number) {
      const car = new THREE.Group();
      const paint = material(team ? '#f79939' : '#378eff', 0.28, 0.45);
      box(1.42, 0.33, 0.88, 0, 0.02, 0, paint, car);
      box(0.68, 0.29, 0.7, -0.12, 0.3, 0, glass, car);
      box(0.54, 0.055, 0.72, -0.16, 0.465, 0, paint, car);
      box(0.36, 0.06, 0.94, -0.64, 0.31, 0, paint, car);
      box(0.15, 0.18, 0.94, 0.74, -0.06, 0, chrome, car);
      for (const z of [-0.3, 0.3]) {
        box(
          0.03,
          0.09,
          0.18,
          0.824,
          0.08,
          z,
          new THREE.MeshStandardMaterial({
            color: '#e8f8ff',
            emissive: '#b2e1ff',
            emissiveIntensity: 2,
          }),
          car,
        );
        box(
          0.025,
          0.08,
          0.16,
          -0.723,
          0.08,
          z,
          new THREE.MeshStandardMaterial({
            color: '#ff4040',
            emissive: '#ff2222',
            emissiveIntensity: 1.5,
          }),
          car,
        );
      }
      for (const x of [-0.47, 0.47])
        for (const z of [-0.47, 0.47]) {
          const wheel = new THREE.Mesh(new THREE.CylinderGeometry(0.22, 0.22, 0.16, 20), rubber);
          wheel.rotation.x = Math.PI / 2;
          wheel.position.set(x, -0.14, z);
          wheel.castShadow = true;
          car.add(wheel);
          const hub = new THREE.Mesh(new THREE.CylinderGeometry(0.105, 0.105, 0.17, 12), chrome);
          hub.rotation.x = Math.PI / 2;
          hub.position.copy(wheel.position);
          car.add(hub);
        }
      scene.add(car);
      return car;
    }
    function position(v: [number, number, number]) {
      return new THREE.Vector3(v[0] / 100, v[2] / 100, -v[1] / 100);
    }
    function direction(v: [number, number, number]) {
      return new THREE.Vector3(v[0], v[2], -v[1]);
    }
    let request = 0,
      previousTime = performance.now(),
      lastFrame: Frame | null = null,
      lastArrival = previousTime;
    let oldMode = mode,
      oldReset = reset;
    const gaze = new THREE.Vector3(),
      target = new THREE.Vector3();
    const resize = new ResizeObserver(() => {
      const width = Math.max(container.clientWidth, 1),
        height = Math.max(container.clientHeight, 1);
      renderer.setSize(width, height);
      camera.aspect = width / height;
      camera.updateProjectionMatrix();
    });
    resize.observe(container);
    function render(now: number) {
      const dt = Math.min(Math.max((now - previousTime) / 1000, 0), 0.1);
      previousTime = now;
      const blend = 1 - Math.exp(-18 * dt),
        cameraBlend = 1 - Math.exp(-7 * dt);
      const changed = frame !== lastFrame;
      const discontinuity = !!(
        frame &&
        lastFrame &&
        (frame.tick < lastFrame.tick || now - lastArrival > 2000)
      );
      if (changed) {
        lastArrival = now;
        lastFrame = frame;
      }
      stale = !!frame && now - lastArrival > 2500;
      if (oldReset !== reset) {
        home();
        oldReset = reset;
      }
      if (mode !== oldMode) {
        gaze.copy(controls.target);
        oldMode = mode;
      }
      controls.enabled = mode !== 'chase';
      ball.visible = marker.visible = !!frame;
      if (frame) {
        const ballTarget = position(frame.ball);
        if (discontinuity || ball.position.distanceTo(ballTarget) > 25 || !ball.userData.ready)
          ball.position.copy(ballTarget);
        else ball.position.lerp(ballTarget, blend);
        ball.userData.ready = true;
        marker.position.set(ball.position.x, 0.065, ball.position.z);
        marker.scale.setScalar(1 + Math.min(ball.position.y / 20, 1));
        for (const state of frame.cars) {
          let car = cars.get(state.id);
          if (!car) {
            car = createCar(state.team);
            cars.set(state.id, car);
          }
          const carTarget = position(state.pos),
            wasHidden = !car.visible;
          car.visible = !state.demoed;
          const snap =
            discontinuity ||
            wasHidden ||
            !car.userData.ready ||
            car.position.distanceTo(carTarget) > 25;
          if (snap) car.position.copy(carTarget);
          else car.position.lerp(carTarget, blend);
          const forward = direction(state.forward).normalize(),
            up = direction(state.up).normalize();
          const right = forward.clone().cross(up).normalize();
          if (forward.lengthSq() > 0.5 && right.lengthSq() > 0.5) {
            up.crossVectors(right, forward).normalize();
            const rotation = new THREE.Quaternion().setFromRotationMatrix(
              new THREE.Matrix4().makeBasis(forward, up, right),
            );
            if (snap) car.quaternion.copy(rotation);
            else car.quaternion.slerp(rotation, blend);
          }
          car.userData.ready = true;
        }
        for (const [id, car] of cars) if (!frame.cars.some((c) => c.id === id)) car.visible = false;
        if (mode === 'ball') controls.target.lerp(ball.position, cameraBlend);
        if (mode === 'chase') {
          const car = followed ? cars.get(followed.id) : undefined;
          if (car) {
            // Keep horizon stable through aerials and rolls; follow the car's horizontal heading.
            const forward = new THREE.Vector3(1, 0, 0).applyQuaternion(car.quaternion);
            forward.y = 0;
            if (forward.lengthSq() < 0.01) forward.set(1, 0, 0);
            else forward.normalize();
            target
              .copy(car.position)
              .addScaledVector(forward, -distance)
              .add(new THREE.Vector3(0, distance * 0.32 + 1, 0));
            target.y = Math.max(target.y, 1.3);
            camera.position.lerp(target, cameraBlend);
            target
              .copy(car.position)
              .addScaledVector(forward, 4)
              .add(new THREE.Vector3(0, 0.75, 0));
            gaze.lerp(target, cameraBlend);
            camera.lookAt(gaze);
            controls.target.copy(gaze);
          }
        }
      } else {
        for (const car of cars.values()) car.visible = false;
      }
      if (mode !== 'chase') controls.update(dt);
      renderer.render(scene, camera);
      request = requestAnimationFrame(render);
    }
    request = requestAnimationFrame(render);
    const contextLost = (event: Event) => {
      event.preventDefault();
      failure = 'The graphics context was lost. Reopen the match view to reconnect.';
    };
    renderer.domElement.addEventListener('webglcontextlost', contextLost);
    return () => {
      cancelAnimationFrame(request);
      resize.disconnect();
      controls.dispose();
      renderer.domElement.removeEventListener('webglcontextlost', contextLost);
      const geometries = new Set<THREE.BufferGeometry>(),
        materials = new Set<THREE.Material>();
      scene.traverse((object) => {
        if (object instanceof THREE.Mesh || object instanceof THREE.Line) {
          geometries.add(object.geometry);
          (Array.isArray(object.material) ? object.material : [object.material]).forEach((m) =>
            materials.add(m),
          );
        }
      });
      geometries.forEach((g) => g.dispose());
      materials.forEach((m) => m.dispose());
      textures.forEach((t) => t.dispose());
      renderer.dispose();
      renderer.forceContextLoss();
      renderer.domElement.remove();
    };
  });
</script>

<section class="match-panel" class:large bind:this={panel}>
  <div class="panel-heading">
    <h3>Match view</h3>
    <span
      >{frame
        ? `${stale ? 'Paused · ' : ''}Tick ${frame.tick.toLocaleString()}`
        : 'Waiting for simulation'}</span
    >
  </div>
  <div class="match-canvas" bind:this={container}></div>
  <div class="match-overlay">
    <span class="blue">BLUE {frame?.score[0] ?? 0}</span><span>:</span><span class="orange"
      >{frame?.score[1] ?? 0} ORANGE</span
    >
  </div>
  {#if !frame}<div class="waiting">Your live simulation appears here</div>{/if}
  {#if failure}<p class="render-error" role="status">{failure}</p>{/if}
  <div class="viewer-controls">
    <div class="camera-modes" role="group" aria-label="Camera mode">
      <button
        class:chosen={mode === 'orbit'}
        aria-pressed={mode === 'orbit'}
        onclick={() => (mode = 'orbit')}>Orbit</button
      >
      <button
        class:chosen={mode === 'ball'}
        aria-pressed={mode === 'ball'}
        onclick={() => (mode = 'ball')}>Ball tracking</button
      >
      <button
        class:chosen={mode === 'chase'}
        aria-pressed={mode === 'chase'}
        onclick={() => (mode = 'chase')}>Third person</button
      >
    </div>
    {#if mode === 'chase'}
      <label
        >Car <select aria-label="Follow car" bind:value={selected}
          ><option value={-1}>First car</option>{#each frame?.cars ?? [] as car}<option
              value={car.id}>{car.team ? 'Orange' : 'Blue'} #{car.id}</option
            >{/each}</select
        ></label
      >
      <label
        >Distance <input
          aria-label="Chase camera distance"
          type="range"
          min="5"
          max="18"
          step=".5"
          bind:value={distance}
        /></label
      >
    {/if}
    <button
      title="Reset camera to arena overview"
      onclick={() => {
        mode = 'orbit';
        reset += 1;
      }}>Reset</button
    >
    <button title="Toggle fullscreen" onclick={fullscreen}>Fullscreen</button>
  </div>
  <div class="match-footer">
    <span
      >{mode === 'chase'
        ? 'Car chase · Stable horizon · Select a car above'
        : 'Drag to orbit · Scroll to zoom · Right-drag to pan'}</span
    >{#if followed && mode === 'chase'}<span
        class:orange={followed.team === 1}
        class:blue={followed.team === 0}
        >{followed.demoed
          ? 'Demolished'
          : `Boost ${Math.round(Math.max(0, Math.min(100, followed.boost)))}%`}</span
      >{/if}
  </div>
</section>

<style>
  .match-panel.large .match-canvas {
    height: calc(100dvh - 440px);
    min-height: 260px;
  }
  .viewer-controls {
    display: flex;
    align-items: center;
    flex-wrap: wrap;
    gap: 8px;
    padding: 11px 15px;
    border-top: 1px solid var(--border);
  }
  .viewer-controls button,
  .viewer-controls select {
    font-size: 11px;
    padding: 6px 10px;
  }
  .camera-modes {
    display: flex;
    gap: 4px;
    margin-right: auto;
  }
  .viewer-controls label {
    display: flex;
    align-items: center;
    gap: 7px;
    color: var(--muted);
    font-size: 10px;
  }
  .viewer-controls input {
    width: 70px;
    accent-color: var(--accent);
  }
  .chosen {
    color: var(--accent);
    background: color-mix(in srgb, var(--accent) 12%, var(--panel));
  }
  .match-overlay {
    width: max-content;
    margin: 0 auto;
    padding: 8px 15px;
    border: 1px solid #7896ac40;
    border-radius: 5px;
    background: #09121ed9;
    font-variant-numeric: tabular-nums;
  }
  .waiting {
    position: absolute;
    top: 48%;
    left: 0;
    right: 0;
    text-align: center;
    pointer-events: none;
    color: #d5e4e8;
    font-size: 12px;
    text-shadow: 0 2px 8px #000;
  }
  .match-panel:fullscreen {
    display: flex;
    flex-direction: column;
    border: 0;
    border-radius: 0;
  }
  .match-panel:fullscreen .match-canvas {
    flex: 1;
    height: auto;
    min-height: 0;
  }
  .match-footer {
    min-height: 35px;
  }
</style>
