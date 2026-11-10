const assert = require('node:assert/strict');
const fs = require('node:fs');
const path = require('node:path');
const vm = require('node:vm');

const source = fs.readFileSync(path.join(__dirname, '../../Main_and_Cameras/ui/shared/rider-scene.js'), 'utf8');
const calls = { draws: 0, data: [], viewports: [], releases: 0, clearColors: [], uniforms: [] };
const handlers = new Map(), themeHandlers = new Map();
const styleValues={'--scene-bg':'#eee9db','--scene-floor':'#ddd7c7'};
let handle = 1;
const gl = new Proxy({
  ARRAY_BUFFER: 0x8892, ELEMENT_ARRAY_BUFFER: 0x8893,
  COMPILE_STATUS: 0x8b81, LINK_STATUS: 0x8b82,
  createShader: () => handle++, getShaderParameter: () => true,
  createProgram: () => handle++, getProgramParameter: () => true,
  createBuffer: () => handle++, getAttribLocation: () => 1, getUniformLocation: (_, name) => name,
  clearColor: (...values) => calls.clearColors.push(values),
  uniform3fv: (location, value) => calls.uniforms.push({location,value:Array.from(value)}),
  bufferData: (target, data) => calls.data.push({ target, data }),
  drawArrays: () => calls.draws++, drawElements: () => calls.draws++,
  viewport: (...values) => calls.viewports.push(values),
  getExtension: () => ({ loseContext: () => calls.releases++ })
}, { get: (object, key) => key in object ? object[key] : () => {} });
let size = { width: 1200, height: 650 };
const canvas = {
  isConnected: true, dataset: {},
  getContext: () => gl, getBoundingClientRect: () => size,
  addEventListener: (name, handler) => handlers.set(name, handler),
  removeEventListener: name => handlers.delete(name)
};
const context = { window: { devicePixelRatio: 3, addEventListener:(name,handler)=>themeHandlers.set(name,handler),removeEventListener:name=>themeHandlers.delete(name) },document:{documentElement:{}},getComputedStyle:()=>({getPropertyValue:name=>styleValues[name]||''}), Float32Array, Uint16Array, Math, Number, canvas };
vm.createContext(context);
// Expose pure kinematics only inside this test's VM; shipping code exports its public API alone.
vm.runInContext(source.replace('export function', 'function').replace(
  'let poseDirty=true',
  'globalThis.inspect={poseParts,point,multiply,projection,lookAt};let poseDirty=true'
), context);
const scene = vm.runInContext('createRiderScene(canvas)', context);
assert.equal(scene.supported, true);
scene.render();
assert.equal(calls.draws, 30, 'static bike is batched; both views use thirty draws');
assert.equal(canvas.width, 1800, 'DPR is capped at 1.5');
assert.equal(canvas.height, 975);
const initialUploads = calls.data.length;
const geometryBytes = calls.data.reduce((sum, entry) => sum + entry.data.byteLength, 0);
assert(geometryBytes < 3_000_000, 'indexed geometry stays below three megabytes');
for (const { data } of calls.data) for (const value of data) assert(Number.isFinite(value));
scene.render();
assert.equal(calls.draws, 30, 'an unchanged pose uses no additional GPU draws');

const { point, multiply, projection, lookAt } = context.inspect;
const distance = (a, b) => Math.hypot(...a.map((value, i) => value - b[i]));
const near = (a, b, message) => assert(distance(a, b) < 0.00001, message);
const length = matrix => Math.hypot(matrix[4], matrix[5], matrix[6]);
let poses = 0, released = 0;
for (let side = -86; side <= 86; side += 2) {
  for (let forward = -80; forward <= 80; forward += 4) {
    scene.setPose(side, forward);
    const parts = context.inspect.poseParts(), root = parts[1][1];
    for (const [, matrix] of parts) for (const value of matrix) assert(Number.isFinite(value));
    for (let leg = 0; leg < 2; leg++) {
      const start = 5 + leg * 4, sign = leg === 0 ? -1 : 1;
      const upper = parts[start][1], lower = parts[start + 1][1];
      const thigh = parts[start + 2][1], shin = parts[start + 3][1], glove = parts[13 + leg][1];
      near(point(upper, [0, 1, 0]), point(lower, [0, 0, 0]), 'sleeves remain connected at the elbow');
      near(point(lower, [0, 1, 0]), point(glove, [0, 0, 0]), 'wrists remain connected to gloves');
      near(point(thigh, [0, 1, 0]), point(shin, [0, 0, 0]), 'trousers remain connected at the knee');
      near(point(shin, [0, 1, 0]), point(root, [sign * .27, .525, .005]), 'boots remain on their footpegs');
      for (const [bone, expected] of [[upper, .40], [lower, .39], [thigh, .43], [shin, .405]])
        assert(Math.abs(length(bone) - expected) < 0.00001, 'anatomical limb lengths do not stretch');
      const gripGap = distance(point(glove, [0, 0, 0]), point(root, [sign * .366, 1.23, .37]));
      if (Math.abs(side) <= 60 && forward >= 0 && forward <= 50)
        assert(gripGap < .00001, 'normal riding poses keep both hands on their grips');
      if (gripGap > .00001) released++;
    }
    poses++;
  }
}
assert(released > 0, 'unreachable extreme poses release grips instead of stretching arms');
scene.setPose(40, 0);
const headRight = point(context.inspect.poseParts()[4][1], [0, 0, 0]);
assert(headRight[0] > 0, 'right lean moves the torso toward rider right');
const frontProjection = projection(1, 0); frontProjection[0] *= -1;
const frontCamera = multiply(frontProjection, lookAt([0, 1.95, 5.8], [0, .95, .01]));
assert(point(frontCamera, [1, 1, 0])[0] < point(frontCamera, [0, 1, 0])[0], 'rider right appears screen-left in the front view');
const sideCamera = multiply(projection(1, 1), lookAt([-5.2, 2, .02], [0, .95, .01]));
assert(point(sideCamera, [0, 1, 1])[0] > point(sideCamera, [0, 1, 0])[0], 'forward appears screen-right in the side view');
scene.setPose(0, -20);
const backward20 = point(context.inspect.poseParts()[4][1], [0, 0, 0]);
scene.setPose(0, -40);
const backward40 = point(context.inspect.poseParts()[4][1], [0, 0, 0]);
assert(backward40[2] < backward20[2] - .10, 'backward lean remains visibly articulated');
scene.setPose(179, 0);
const beforeWrap = [...context.inspect.poseParts()[1][1]];
scene.setPose(-179, 0);
near([...context.inspect.poseParts()[1][1]], beforeWrap, 'the ±180 degree wrap does not mirror the fallen pose');
scene.setPose(0, 0);
scene.setPose(-40, 0);
assert(point(context.inspect.poseParts()[4][1], [0, 0, 0])[0] < 0, 'upright releases the inversion hemisphere');
scene.setPose(NaN, Infinity);
for (const [, matrix] of context.inspect.poseParts()) for (const value of matrix) assert(Number.isFinite(value));

size = { width: 320, height: 400 };
scene.render();
assert.equal(canvas.dataset.sceneLayout, 'stacked');
const views = calls.viewports.slice(-2);
assert(views[0][1] > views[1][1], 'front is above side on small displays');
assert.equal(views[0][0], 0); assert.equal(views[1][0], 0);
assert.equal(calls.data.length, initialUploads, 'poses and resize never rebuild geometry');
assert.equal(themeHandlers.size,1,'one theme listener belongs to the live scene');
const beforeThemeDraws=calls.draws;
for(let n=0;n<20;n++){
 styleValues['--scene-bg']=n%2?'#eee9db':'#222620';styleValues['--scene-floor']=n%2?'#ddd7c7':'#343a30';
 themeHandlers.get('themechange')();
}
assert.equal(calls.data.length,initialUploads,'appearance changes reuse all vertex/index buffers');
assert.equal(calls.draws,beforeThemeDraws+20*30,'theme changes repaint the existing two views');
assert(Math.abs(calls.clearColors.at(-1)[0]-238/255)<1e-8,'cream background reaches the GL clear color');
assert(calls.uniforms.some(row=>row.location==='uFloor'&&Math.abs(row.value[0]-Math.pow(52/255,2.2))<1e-8),'dim ground tint reaches its shader uniform');

handlers.get('webglcontextlost')({ preventDefault() {} });
const beforeLoss = calls.draws;
scene.render(); assert.equal(calls.draws, beforeLoss);
assert.equal(canvas.dataset.sceneUnavailable, 'true');
handlers.get('webglcontextrestored')();
assert(calls.draws > beforeLoss); assert.equal(canvas.dataset.sceneUnavailable, undefined);
scene.dispose(); scene.dispose();
assert.equal(handlers.size, 0); assert.equal(themeHandlers.size,0,'scene disposal removes the theme listener'); assert.equal(calls.releases, 1);
const beforeDispose = calls.draws;
scene.render(); assert.equal(calls.draws, beforeDispose);
context.canvas = { getContext: () => null };
assert.equal(vm.runInContext('createRiderScene(canvas).supported', context), false);
console.log(`PASS rider scene: ${poses} poses; exact joints and contacts; fixed limb lengths; projection signs; roll wrap; mobile; GPU lifecycle and appearance resource reuse; ${geometryBytes} packed bytes.`);
