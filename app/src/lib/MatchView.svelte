<script lang="ts">
  import { onMount } from 'svelte';
  import * as THREE from 'three';
  import { OrbitControls } from 'three/addons/controls/OrbitControls.js';
  import type { Frame } from './types';
  let { frame = null, large = false }: { frame?: Frame | null; large?: boolean } = $props();
  let container: HTMLDivElement;
  let failure = $state('');
  let follow = $state(false);
  onMount(() => {
    let renderer: THREE.WebGLRenderer;
    try {
      renderer = new THREE.WebGLRenderer({ antialias: true, alpha: false });
    } catch {
      failure =
        '3D rendering is unavailable. Enable hardware acceleration or update the graphics driver.';
      return;
    }
    renderer.setPixelRatio(Math.min(window.devicePixelRatio, 2));
    renderer.setClearColor('#101b23');
    container.appendChild(renderer.domElement);
    const scene = new THREE.Scene();
    scene.fog = new THREE.Fog('#101b23', 180, 380);
    const camera = new THREE.PerspectiveCamera(43, 1, 0.1, 500);
    camera.position.set(110, 110, 135);
    const controls = new OrbitControls(camera, renderer.domElement);
    controls.target.set(0, 0, 0);
    controls.enableDamping = true;
    controls.maxPolarAngle = Math.PI / 2.08;
    controls.minDistance = 20;
    controls.maxDistance = 240;
    scene.add(new THREE.HemisphereLight('#d5efff', '#1c3031', 2.5));
    const sun = new THREE.DirectionalLight('#ffffff', 3);
    sun.position.set(20, 80, 40);
    scene.add(sun);
    const floor = new THREE.Mesh(
      new THREE.PlaneGeometry(81.92, 120),
      new THREE.MeshStandardMaterial({ color: '#24463e', roughness: 1 }),
    );
    floor.rotation.x = -Math.PI / 2;
    scene.add(floor);
    const grid = new THREE.GridHelper(120, 24, '#49645b', '#2c4d44');
    grid.position.y = 0.03;
    scene.add(grid);
    function line(points: THREE.Vector3[], color: string) {
      const object = new THREE.Line(
        new THREE.BufferGeometry().setFromPoints(points),
        new THREE.LineBasicMaterial({ color }),
      );
      scene.add(object);
    }
    for (const sign of [-1, 1]) {
      const z = sign * 51.2;
      line([new THREE.Vector3(-40.96, 0.08, z), new THREE.Vector3(40.96, 0.08, z)], '#74988e');
      const goal = new THREE.Mesh(
        new THREE.BoxGeometry(17.86, 6.43, 8.8),
        new THREE.MeshBasicMaterial({
          color: sign > 0 ? '#589edc' : '#f1ad6e',
          wireframe: true,
          transparent: true,
          opacity: 0.8,
        }),
      );
      goal.position.set(0, 3.22, sign * 55.6);
      scene.add(goal);
      const wall = new THREE.Mesh(
        new THREE.BoxGeometry(0.3, 20.44, 102.4),
        new THREE.MeshStandardMaterial({
          color: '#3d6873',
          transparent: true,
          opacity: 0.12,
          depthWrite: false,
        }),
      );
      wall.position.set(sign * 40.96, 10.22, 0);
      scene.add(wall);
    }
    line([new THREE.Vector3(-40.96, 0.1, 0), new THREE.Vector3(40.96, 0.1, 0)], '#83a69b');
    const ring = new THREE.Mesh(
      new THREE.RingGeometry(9.8, 10, 64),
      new THREE.MeshBasicMaterial({ color: '#83a69b', side: THREE.DoubleSide }),
    );
    ring.rotation.x = -Math.PI / 2;
    ring.position.y = 0.06;
    scene.add(ring);
    const ball = new THREE.Mesh(
      new THREE.SphereGeometry(0.9125, 24, 16),
      new THREE.MeshStandardMaterial({ color: '#eef4ec', roughness: 0.4 }),
    );
    ball.position.y = 0.93;
    scene.add(ball);
    const cars = new Map<number, THREE.Group>();
    function position(v: [number, number, number]) {
      return new THREE.Vector3(v[0] / 100, v[2] / 100, -v[1] / 100);
    }
    function direction(v: [number, number, number]) {
      return new THREE.Vector3(v[0], v[2], -v[1]);
    }
    let request = 0;
    const resize = new ResizeObserver(() => {
      renderer.setSize(container.clientWidth, container.clientHeight);
      camera.aspect = container.clientWidth / container.clientHeight;
      camera.updateProjectionMatrix();
    });
    resize.observe(container);
    function render() {
      if (frame) {
        ball.position.lerp(position(frame.ball), 0.5);
        for (const state of frame.cars) {
          let car = cars.get(state.id);
          if (!car) {
            car = new THREE.Group();
            const body = new THREE.Mesh(
              new THREE.BoxGeometry(1.18, 0.4, 0.85),
              new THREE.MeshStandardMaterial({
                color: state.team ? '#f3a35f' : '#5dabeb',
                metalness: 0.3,
                roughness: 0.4,
              }),
            );
            const roof = new THREE.Mesh(
              new THREE.BoxGeometry(0.6, 0.25, 0.7),
              new THREE.MeshStandardMaterial({ color: '#c3e0e8', metalness: 0.4 }),
            );
            roof.position.set(-0.05, 0.3, 0);
            car.add(body, roof);
            for (const x of [-0.4, 0.4])
              for (const z of [-0.45, 0.45]) {
                const wheel = new THREE.Mesh(
                  new THREE.CylinderGeometry(0.18, 0.18, 0.14, 12),
                  new THREE.MeshStandardMaterial({ color: '#11191d' }),
                );
                wheel.rotation.x = Math.PI / 2;
                wheel.position.set(x, -0.15, z);
                car.add(wheel);
              }
            scene.add(car);
            cars.set(state.id, car);
          }
          car.visible = !state.demoed;
          car.position.lerp(position(state.pos), 0.5);
          const fwd = direction(state.forward).normalize(),
            up = direction(state.up).normalize();
          const right = fwd.clone().cross(up).normalize();
          const matrix = new THREE.Matrix4().makeBasis(fwd, up, right);
          car.quaternion.slerp(new THREE.Quaternion().setFromRotationMatrix(matrix), 0.5);
        }
        for (const [id, car] of cars) if (!frame.cars.some((c) => c.id === id)) car.visible = false;
        if (follow) controls.target.lerp(ball.position, 0.05);
      }
      controls.update();
      renderer.render(scene, camera);
      request = requestAnimationFrame(render);
    }
    render();
    return () => {
      cancelAnimationFrame(request);
      resize.disconnect();
      controls.dispose();
      scene.traverse((object) => {
        if (object instanceof THREE.Mesh || object instanceof THREE.Line) {
          object.geometry.dispose();
          const materials = Array.isArray(object.material) ? object.material : [object.material];
          materials.forEach((m) => m.dispose());
        }
      });
      renderer.dispose();
      renderer.forceContextLoss();
      renderer.domElement.remove();
    };
  });
</script>

<section class="match-panel" class:large>
  <div class="panel-heading">
    <h3>Match view</h3>
    <span>{frame ? `Tick ${frame.tick.toLocaleString()}` : 'Waiting for simulation'}</span>
  </div>
  <div class="match-canvas" bind:this={container}></div>
  <div class="match-overlay">
    <span class="blue">BLUE {frame?.score[0] ?? 0}</span><span>:</span><span class="orange"
      >{frame?.score[1] ?? 0} ORANGE</span
    >
  </div>
  {#if failure}<p class="render-error">{failure}</p>{/if}
  <div class="match-footer">
    <span>Drag to orbit · Scroll to zoom · Simplified visualization</span><button
      class:chosen={follow}
      onclick={() => (follow = !follow)}>Follow ball</button
    >
  </div>
</section>
