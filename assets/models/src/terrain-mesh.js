export const terrainSettings = {
  "gridSize": 20,
  "segments": 128,
  "heightScale": 0.4,
  "frequency": 1,
  "ridgeSharpness": 0,
  "seed": 44596,
  "craterDensity": 10,
  "waterLevel": 2.05,
  "hasWater": 1,
  "waterColor": "#a600ff",
  "lowColor": "#ac2020",
  "midColor": "#695c4e",
  "highColor": "#d9e7f2",
  "flatShading": 0
};

function makePRNG(seed = 12345) {
    let s = Math.abs(seed) % 2147483647;
    if (s === 0) s = 1;
    return () => {
      s = (s * 16807) % 2147483647;
      return (s - 1) / 2147483646;
    };
  }

function createNoise2D(rand) {
    const perm = new Uint8Array(512);
    const p = new Uint8Array(256);
    for (let i = 0; i < 256; i++) p[i] = i;
    for (let i = 255; i > 0; i--) {
      const r = Math.floor(rand() * (i + 1));
      const t = p[i]; p[i] = p[r]; p[r] = t;
    }
    for (let i = 0; i < 512; i++) perm[i] = p[i & 255];

    const fade = t => t * t * t * (t * (t * 6 - 15) + 10);
    const grad = (hash, x, y) => {
      const h = hash & 7;
      const u = h < 4 ? x : y;
      const v = h < 4 ? y : x;
      return ((h & 1) === 0 ? u : -u) + ((h & 2) === 0 ? v : -v);
    };

    return (x, y) => {
      const X = Math.floor(x) & 255;
      const Y = Math.floor(y) & 255;
      const xf = x - Math.floor(x);
      const yf = y - Math.floor(y);
      const u = fade(xf);
      const v = fade(yf);
      const aa = perm[perm[X] + Y];
      const ab = perm[perm[X] + Y + 1];
      const ba = perm[perm[X + 1] + Y];
      const bb = perm[perm[X + 1] + Y + 1];
      const x1 = (1 - u) * grad(aa, xf, yf) + u * grad(ba, xf - 1, yf);
      const x2 = (1 - u) * grad(ab, xf, yf - 1) + u * grad(bb, xf - 1, yf - 1);
      return (1 - v) * x1 + v * x2;
    };
  }

function createTerrain(THREE, settings, sharedMaterial) {
    const config = { ...settings };
    const rand = makePRNG(config.seed);
    const noise2d = createNoise2D(rand);

    const segs = Math.min(128, Math.max(16, Math.round(config.segments)));
    let geom = new THREE.PlaneGeometry(config.gridSize, config.gridSize, segs, segs);
    geom.rotateX(-Math.PI * 0.5);

    const pos = geom.attributes.position;
    const colors = new Float32Array(pos.count * 3);
    const cLow = new THREE.Color(config.lowColor).convertLinearToSRGB();
    const cMid = new THREE.Color(config.midColor).convertLinearToSRGB();
    const cHigh = new THREE.Color(config.highColor).convertLinearToSRGB();

    const craters = [];
    const craterCount = Math.round(config.craterDensity);
    for (let i = 0; i < craterCount; i++) {
      craters.push({
        x: (rand() - 0.5) * config.gridSize * 0.7,
        z: (rand() - 0.5) * config.gridSize * 0.7,
        rad: 1.0 + rand() * 2.5,
        depth: 0.5 + rand() * 1.2
      });
    }

    let minH = Infinity, maxH = -Infinity;
    const heights = new Float32Array(pos.count);

    for (let i = 0; i < pos.count; i++) {
      const px = pos.getX(i);
      const pz = pos.getZ(i);
      const f = config.frequency * 0.25;
      const n1 = noise2d(px * f, pz * f);
      const n2 = noise2d(px * f * 2.3, pz * f * 2.3) * 0.5;
      let h = (n1 + n2);
      if (config.ridgeSharpness > 0.05) {
        h = Math.pow(Math.abs(h), 1.0 + config.ridgeSharpness * 0.8) * Math.sign(h);
      }
      h *= config.heightScale;

      for (const c of craters) {
        const dx = px - c.x, dz = pz - c.z;
        const dist = Math.sqrt(dx * dx + dz * dz);
        if (dist < c.rad) {
          const t = dist / c.rad;
          const craterShape = (1.0 - Math.cos(t * Math.PI)) * 0.5;
          const rim = Math.sin(t * Math.PI) * 0.25;
          h -= (1.0 - craterShape) * c.depth - rim;
        }
      }

      pos.setY(i, h);
      heights[i] = h;
      if (h < minH) minH = h;
      if (h > maxH) maxH = h;
    }

    const range = Math.max(0.001, maxH - minH);
    for (let i = 0; i < pos.count; i++) {
      const normH = (heights[i] - minH) / range;
      const color = new THREE.Color();
      if (normH < 0.45) color.copy(cLow).lerp(cMid, normH / 0.45);
      else color.copy(cMid).lerp(cHigh, (normH - 0.45) / 0.55);
      colors[i * 3] = color.r;
      colors[i * 3 + 1] = color.g;
      colors[i * 3 + 2] = color.b;
    }

    geom.setAttribute('color', new THREE.BufferAttribute(colors, 3));
    if (config.flatShading > 0.5) geom = geom.toNonIndexed();
    geom.computeVertexNormals();

    const terrainMat = sharedMaterial.clone();
    terrainMat.vertexColors = true;
    terrainMat.flatShading = config.flatShading > 0.5;
    terrainMat.roughness = 0.85;
    terrainMat.needsUpdate = true;

    const terrainMesh = new THREE.Mesh(geom, terrainMat);
    const group = new THREE.Group();
    group.add(terrainMesh);

    let waterGeom = null, waterMat = null;
    if (config.hasWater > 0.5) {
      waterGeom = new THREE.PlaneGeometry(config.gridSize, config.gridSize);
      waterGeom.rotateX(-Math.PI * 0.5);
      waterMat = new THREE.MeshStandardMaterial({
        color: new THREE.Color(config.waterColor).convertLinearToSRGB(),
        transparent: true, opacity: 0.72, roughness: 0.1, metalness: 0.25
      });
      const waterMesh = new THREE.Mesh(waterGeom, waterMat);
      waterMesh.position.y = config.waterLevel;
      group.add(waterMesh);
    }

    return {
      group,
      primaryMesh: terrainMesh,
      step() {},
      dispose() {
        geom.dispose(); terrainMat.dispose();
        if (waterGeom) waterGeom.dispose();
        if (waterMat) waterMat.dispose();
        group.removeFromParent();
      }
    };
  }

export function makeTerrainMesh(THREE, overrides = {}) {

  const material = new THREE.MeshStandardMaterial({ roughness: 0.85 });

  return createTerrain(THREE, { ...terrainSettings, ...overrides }, material);

}