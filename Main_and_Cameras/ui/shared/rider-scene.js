/* SmartVest local WebGL rider. Metres: +x rider right, +y up, +z forward. */
export function createRiderScene(canvas) {
  const noScene = { supported: false, setPose() {}, render() {}, dispose() {} };
  let gl;
  try { gl = canvas.getContext('webgl', { alpha: false, antialias: true, depth: true, powerPreference: 'low-power' }); }
  catch (_) { return noScene; }
  if (!gl) return noScene;
  const PI = Math.PI, rad = PI / 180;
  const clamp = (v, a, b) => Math.max(a, Math.min(b, v));
  const add = (a,b) => [a[0]+b[0],a[1]+b[1],a[2]+b[2]];
  const sub = (a,b) => [a[0]-b[0],a[1]-b[1],a[2]-b[2]];
  const mul = (a,s) => [a[0]*s,a[1]*s,a[2]*s];
  const dot = (a,b) => a[0]*b[0]+a[1]*b[1]+a[2]*b[2];
  const cross = (a,b) => [a[1]*b[2]-a[2]*b[1],a[2]*b[0]-a[0]*b[2],a[0]*b[1]-a[1]*b[0]];
  const norm = v => mul(v,1/(Math.hypot(...v)||1));
  const identity = () => new Float32Array([1,0,0,0,0,1,0,0,0,0,1,0,0,0,0,1]);
  function multiply(a,b) {
    const out = new Float32Array(16);
    for (let c=0;c<4;c++) for (let r=0;r<4;r++)
      out[c*4+r]=a[r]*b[c*4]+a[r+4]*b[c*4+1]+a[r+8]*b[c*4+2]+a[r+12]*b[c*4+3];
    return out;
  }
  function transform(p=[0,0,0],s=[1,1,1],rx=0,ry=0,rz=0) {
    const a=Math.cos(rx), b=Math.sin(rx), c=Math.cos(ry), d=Math.sin(ry), e=Math.cos(rz), f=Math.sin(rz);
    return new Float32Array([
      c*e*s[0],c*f*s[0],-d*s[0],0,
      (b*d*e-a*f)*s[1],(b*d*f+a*e)*s[1],b*c*s[1],0,
      (a*d*e+b*f)*s[2],(a*d*f-b*e)*s[2],a*c*s[2],0,
      p[0],p[1],p[2],1
    ]);
  }
  const point = (m,p) => [m[0]*p[0]+m[4]*p[1]+m[8]*p[2]+m[12],m[1]*p[0]+m[5]*p[1]+m[9]*p[2]+m[13],m[2]*p[0]+m[6]*p[1]+m[10]*p[2]+m[14]];
  function segment(a,b,r=1) {
    const v=sub(b,a), length=Math.hypot(...v), y=norm(v), x=norm(cross(Math.abs(y[2])<.9?[0,0,1]:[1,0,0],y)), z=cross(x,y);
    return new Float32Array([x[0]*r,x[1]*r,x[2]*r,0,y[0]*length,y[1]*length,y[2]*length,0,z[0]*r,z[1]*r,z[2]*r,0,...a,1]);
  }
  function lookAt(eye,target) {
    const z=norm(sub(eye,target)),x=norm(cross([0,1,0],z)),y=cross(z,x);
    return new Float32Array([x[0],y[0],z[0],0,x[1],y[1],z[1],0,x[2],y[2],z[2],0,-dot(x,eye),-dot(y,eye),-dot(z,eye),1]);
  }
  function projection(aspect,view) {
    const height=Math.max(2.65,(view?2.45:2.25)/aspect), width=height*aspect;
    return new Float32Array([2/width,0,0,0,0,2/height,0,0,0,0,-2/15,0,0,0,-1,1]);
  }
  const palette = {};
  function color(hex,gloss=.25) {
    const key=hex+gloss;
    return palette[key] || (palette[key]=[...hex.match(/\w\w/g).map(v=>Math.pow(parseInt(v,16)/255,2.2)),gloss]);
  }
  const C={ tire:color('121a24',.12), rubber:color('26303b',.16), metal:color('c6d0dc',.8), chrome:color('e8edf2',.95), dark:color('303d4d',.55), black:color('17212d',.27), blue:color('545b52',.72), blueLight:color('77816e',.65), amber:color('ecad36',.3), reflective:color('d8ded7',.56), cloth:color('263440',.14), clothDark:color('162330',.12), seam:color('52606a',.15), visor:color('173d55',.97), shell:color('d9e0df',.7), light:color('e7f4f1',.7), red:color('dc4845',.56), gold:color('d2ac58',.7), floor:color('ffffff',.03) };
  const sphere=[], cylinder=[], torus=[], cube=[];
  function vertex(out,p,n,c) { out.push(...p,...n,...c); }
  function triangle(out,a,b,c,n,co) { vertex(out,a,n,co);vertex(out,b,n,co);vertex(out,c,n,co); }
  function surface(out,nu,nv,sample,co) {
    for(let j=0;j<nv;j++) for(let i=0;i<nu;i++) {
      const q=[sample(i/nu,j/nv),sample((i+1)/nu,j/nv),sample((i+1)/nu,(j+1)/nv),sample(i/nu,(j+1)/nv)];
      for(const k of [0,1,2,0,2,3]) vertex(out,q[k][0],q[k][1],co);
    }
  }
  surface(sphere,20,14,(u,v)=>{const a=u*2*PI,b=v*PI,p=[Math.sin(b)*Math.sin(a),Math.cos(b),Math.sin(b)*Math.cos(a)];return [p,p];},C.metal);
  surface(cylinder,20,1,(u,v)=>{const a=u*2*PI;return [[Math.sin(a),v,Math.cos(a)],[Math.sin(a),0,Math.cos(a)]];},C.metal);
  for(let i=0;i<20;i++) {
    const a=i*PI/10,b=(i+1)*PI/10;
    triangle(cylinder,[0,0,0],[Math.sin(b),0,Math.cos(b)],[Math.sin(a),0,Math.cos(a)],[0,-1,0],C.metal);
    triangle(cylinder,[0,1,0],[Math.sin(a),1,Math.cos(a)],[Math.sin(b),1,Math.cos(b)],[0,1,0],C.metal);
  }
  surface(torus,36,10,(u,v)=>{
    const a=u*2*PI,b=v*2*PI,r=1+.23*Math.cos(b);
    return [[.23*Math.sin(b),r*Math.sin(a),r*Math.cos(a)],[Math.sin(b),Math.cos(b)*Math.sin(a),Math.cos(b)*Math.cos(a)]];
  },C.tire);
  for(const [n,a,b,c,d] of [
    [[0,0,1],[-1,-1,1],[1,-1,1],[1,1,1],[-1,1,1]],
    [[0,0,-1],[1,-1,-1],[-1,-1,-1],[-1,1,-1],[1,1,-1]],
    [[1,0,0],[1,-1,1],[1,-1,-1],[1,1,-1],[1,1,1]],
    [[-1,0,0],[-1,-1,-1],[-1,-1,1],[-1,1,1],[-1,1,-1]],
    [[0,1,0],[-1,1,1],[1,1,1],[1,1,-1],[-1,1,-1]],
    [[0,-1,0],[-1,-1,-1],[1,-1,-1],[1,-1,1],[-1,-1,1]]
  ]) {triangle(cube,a,b,c,n,C.metal);triangle(cube,a,c,d,n,C.metal);}
  function append(out,mesh,m,co) {
    const x=[m[0],m[1],m[2]],y=[m[4],m[5],m[6]],z=[m[8],m[9],m[10]],nx=cross(y,z),ny=cross(z,x),nz=cross(x,y);
    for(let i=0;i<mesh.length;i+=10) {
      const p=point(m,mesh.slice(i,i+3)), n=norm([nx[0]*mesh[i+3]+ny[0]*mesh[i+4]+nz[0]*mesh[i+5],nx[1]*mesh[i+3]+ny[1]*mesh[i+4]+nz[1]*mesh[i+5],nx[2]*mesh[i+3]+ny[2]*mesh[i+4]+nz[2]*mesh[i+5]]);
      vertex(out,p,n,co||mesh.slice(i+6,i+10));
    }
  }
  const ellipsoid=(out,p,s,c,rx=0,ry=0,rz=0)=>append(out,sphere,transform(p,s,rx,ry,rz),c);
  const box=(out,p,s,c,rx=0,ry=0,rz=0)=>append(out,cube,transform(p,s,rx,ry,rz),c);
  const tube=(out,a,b,r,c)=>append(out,cylinder,segment(a,b,r),c);
  const ring=(out,p,s,c)=>append(out,torus,transform(p,s),c);
  function curve(out,pts,r,c) {for(let i=1;i<pts.length;i++)tube(out,pts[i-1],pts[i],r,c);}
  function arc(out,center,radius,width,from,to,c) {
    const pts=[];for(let i=0;i<=16;i++){const a=from+(to-from)*i/16;pts.push([center[0],center[1]+radius*Math.sin(a),center[2]+radius*Math.cos(a)]);}
    for(let i=1;i<pts.length;i++) {tube(out,pts[i-1],pts[i],width,c);}
  }
  const bike=[],body=[],helmet=[],pelvis=[],upperArm=[],foreArm=[],thigh=[],shin=[],floor=[],gloves=[[],[]];
  // Tyres, alloy spokes, floating brake discs, calipers and wheel hubs.
  for(const z of [-.77,.79]) {
    ring(bike,[0,.345,z],[.34,.275,.275],C.tire);
    ring(bike,[0,.345,z],[.27,.201,.201],C.dark);
    ring(bike,[.047,.345,z],[.06,.204,.204],C.metal);
    ring(bike,[-.047,.345,z],[.06,.204,.204],C.metal);
    tube(bike,[-.095,.345,z],[.095,.345,z],.065,C.dark);
    for(let i=0;i<10;i++) {
      const a=i*PI/5;
      for(const sign of [-1,1]) {
        tube(bike,[sign*.035,.345+.054*Math.sin(a),z+.054*Math.cos(a)],[sign*.045,.345+.193*Math.sin(a+.14),z+.193*Math.cos(a+.14)],.012,C.metal);
      }
    }
    const bx=.083;
    ring(bike,[bx,.345,z],[.022,.151,.151],C.metal);
    ring(bike,[bx,.345,z],[.018,.12,.12],C.dark);
    for(let i=0;i<12;i++) {
      const a=i*PI/6;
      tube(bike,[bx+.005,.345+.143*Math.sin(a),z+.143*Math.cos(a)],[bx+.016,.345+.143*Math.sin(a),z+.143*Math.cos(a)],.009,C.black);
    }
    ellipsoid(bike,[.10,.43,z+.12],[.032,.069,.042],C.gold);
    // Low-profile tread grooves remain geometric and legible in the side view.
    for(let i=0;i<24;i++) {
      const a=i*PI/12;
      tube(bike,[-.046,.345+.336*Math.sin(a),z+.336*Math.cos(a)],[.046,.345+.336*Math.sin(a+.032),z+.336*Math.cos(a+.032)],.003,C.rubber);
    }
  }
  // Rear swingarm, trellis chassis, engine, cooling fins and exhaust.
  for(const sign of [-1,1]) {
    tube(bike,[sign*.105,.35,-.77],[sign*.14,.50,-.14],.032,C.dark);
    tube(bike,[sign*.14,.50,-.14],[sign*.14,.42,-.70],.023,C.dark);
    curve(bike,[[sign*.14,.51,-.16],[sign*.16,.85,-.40],[sign*.12,1.04,.31],[sign*.14,.51,-.16]],.023,C.metal);
    tube(bike,[sign*.16,.85,-.40],[sign*.14,.58,.22],.018,C.dark);
    tube(bike,[sign*.11,.58,-.52],[sign*.11,.88,-.35],.027,C.black);
    for(let i=0;i<7;i++) ring(bike,[sign*.11,.60+i*.035,-.51+i*.02],[.04,.033,.033],C.gold);
    tube(bike,[sign*.15,.48,-.06],[sign*.28,.46,-.08],.018,C.metal);
    tube(bike,[sign*.27,.46,-.15],[sign*.27,.46,.01],.023,C.rubber);
  }
  ellipsoid(bike,[0,.58,.03],[.15,.19,.20],C.dark);
  ellipsoid(bike,[.14,.56,.0],[.037,.12,.13],C.metal);
  ellipsoid(bike,[-.14,.56,.0],[.037,.12,.13],C.black);
  for(let i=0;i<7;i++) box(bike,[0,.69+i*.021,.09],[.14,.006,.11],i%2?C.metal:C.dark,0.17);
  box(bike,[0,.79,.32],[.17,.14,.024],C.black,-.12);
  for(let i=0;i<9;i++) box(bike,[0,.69+i*.025,.35],[.15,.004,.007],C.metal);
  curve(bike,[[.13,.69,.17],[.19,.52,.27],[.20,.36,.10],[.23,.35,-.42]],.022,C.chrome);
  ellipsoid(bike,[.24,.38,-.52],[.075,.074,.26],C.dark,.06);
  ellipsoid(bike,[.24,.39,-.75],[.061,.060,.025],C.chrome);
  ellipsoid(bike,[.24,.39,-.768],[.041,.040,.009],C.black);
  // Sculpted blue tank, tank pad, fuel cap, stepped saddle and tail light.
  ellipsoid(bike,[0,.96,.06],[.206,.205,.32],C.blue,-.17);
  ellipsoid(bike,[0,.91,-.15],[.145,.143,.19],C.blueLight,-.28);
  ellipsoid(bike,[0,1.112,.12],[.065,.014,.065],C.metal,-.13);
  ellipsoid(bike,[0,1.125,.12],[.041,.006,.041],C.dark,-.13);
  ellipsoid(bike,[0,1.05,-.17],[.079,.008,.091],C.black,-.42);
  ellipsoid(bike,[0,.984,-.37],[.185,.066,.235],C.black,-.04);
  ellipsoid(bike,[0,1.018,-.62],[.145,.061,.16],C.clothDark,-.09);
  ellipsoid(bike,[0,.963,-.63],[.154,.060,.20],C.blue,-.11);
  box(bike,[0,.96,-.806],[.088,.020,.012],C.red,.12);
  tube(bike,[0,.92,-.72],[0,.74,-.91],.016,C.black);
  box(bike,[0,.75,-.91],[.073,.043,.009],C.dark,.2);
  // Twin forks, front fender, headlight housing and compact screen.
  for(const sign of [-1,1]) {
    tube(bike,[sign*.105,.35,.79],[sign*.105,1.10,.40],.026,C.chrome);
    tube(bike,[sign*.105,.60,.66],[sign*.105,1.10,.40],.035,C.dark);
    ellipsoid(bike,[sign*.14,.85,.48],[.043,.069,.09],C.blue,-.5);
    tube(bike,[sign*.11,1.11,.40],[sign*.35,1.21,.42],.018,C.dark);
    tube(bike,[sign*.35,1.21,.42],[sign*.415,1.235,.38],.027,C.rubber);
    tube(bike,[sign*.35,1.22,.43],[sign*.41,1.24,.50],.006,C.metal);
    curve(bike,[[sign*.25,1.185,.43],[sign*.30,1.39,.43],[sign*.39,1.42,.42]],.009,C.dark);
    ellipsoid(bike,[sign*.405,1.425,.42],[.067,.039,.016],C.dark,0,sign*.2);
    ellipsoid(bike,[sign*.406,1.427,.403],[.056,.028,.004],C.metal,0,sign*.2);
    ellipsoid(bike,[sign*.19,1.005,.62],[.037,.024,.042],C.amber);
  }
  surface(bike,30,4,(u,v)=> {
    const a=.04+u*(PI+.02),r=.37;
    return [[(v-.5)*.19,.345+r*Math.sin(a),.79+r*Math.cos(a)],[0,Math.sin(a),Math.cos(a)]];
  },C.blue);
  for(const sign of [-1,1]) arc(bike,[sign*.095,.345,.79],.366,.009,.04,PI+.06,C.blue);
  ellipsoid(bike,[0,1.075,.54],[.155,.107,.104],C.dark,-.2);
  ellipsoid(bike,[0,1.085,.628],[.126,.074,.018],C.light,-.2);
  box(bike,[0,1.084,.65],[.099,.007,.006],C.chrome,-.2);
  ellipsoid(bike,[0,1.225,.47],[.121,.12,.016],C.visor,-.35);
  box(bike,[0,1.19,.385],[.075,.039,.015],C.black,.55);
  box(bike,[0,1.213,.365],[.053,.025,.003],C.visor,.55);
  // Glove batches allow an unreachable extreme pose to release the grips.
  // Boots keep a stable footpeg contact throughout the illustration.
  for(const sign of [-1,1]) {
    const glove=gloves[sign<0?0:1];
    ellipsoid(glove,[sign*.007,-.001,.029],[.052,.040,.063],C.clothDark,0,0,sign*.2);
    ellipsoid(glove,[sign*.009,.035,.041],[.041,.012,.036],C.dark,0,0,sign*.2);
    for(let i=0;i<4;i++) ellipsoid(glove,[sign*(-.021+i*.015),-.012,.056],[.009,.019,.025],C.black,-.4);
    ellipsoid(bike,[sign*.27,.49,.025],[.066,.104,.115],C.clothDark,.15);
    ellipsoid(bike,[sign*.27,.413,.085],[.071,.055,.162],C.black,.06);
    ellipsoid(bike,[sign*.27,.378,.083],[.074,.018,.164],C.dark,.06);
    ellipsoid(bike,[sign*.30,.50,.0],[.012,.062,.046],C.metal);
    box(bike,[sign*.27,.445,.19],[.048,.006,.035],C.dark,.1);
  }
  // Tailored torso surface, reflective safety vest and restrained jacket seams.
  const torsoProfile=[[0,.175,.107],[.08,.19,.13],[.25,.223,.146],[.42,.247,.128],[.49,.228,.105],[.54,.145,.087]];
  function profile(y) {
    for(let i=1;i<torsoProfile.length;i++) if(y<=torsoProfile[i][0]) {
      const a=torsoProfile[i-1],b=torsoProfile[i],t=(y-a[0])/(b[0]-a[0]);return [a[1]+(b[1]-a[1])*t,a[2]+(b[2]-a[2])*t];
    }
    return torsoProfile[torsoProfile.length-1].slice(1);
  }
  function jacket(out,y0,y1,a0,a1,co,lift=0) {
    surface(out,24,10,(u,v)=> {
      const y=y0+(y1-y0)*v,a=a0+(a1-a0)*u,[w,d]=profile(y),p=[(w+lift)*Math.sin(a),y,(d+lift)*Math.cos(a)];
      return [p,norm([Math.sin(a)/w,.05,Math.cos(a)/d])];
    },co);
  }
  jacket(body,0,.54,0,2*PI,C.cloth);
  for(const sign of [-1,1]) {
    jacket(body,.035,.49,sign*.09,sign*1.38,C.amber,.004);
    jacket(body,.27,.321,sign*.09,sign*1.38,C.reflective,.007);
    jacket(body,.335,.49,sign*.63,sign*.85,C.reflective,.008);
    jacket(body,.02,.05,sign*.1,sign*1.38,C.clothDark,.009);
    jacket(body,.15,.22,sign*.22,sign*.80,C.gold,.008);
    jacket(body,.201,.211,sign*.24,sign*.78,C.clothDark,.011);
    ellipsoid(body,[sign*.225,.435,-.005],[.062,.071,.113],C.clothDark,0,0,sign*.2);
    curve(body,[[sign*.19,.05,-.067],[sign*.23,.26,-.07],[sign*.225,.42,-.055]],.003,C.seam);
  }
  jacket(body,.03,.48,-.025,.025,C.black,.01);
  box(body,[0,.27,.16],[.008,.018,.005],C.metal);
  ellipsoid(body,[0,.55,0],[.084,.055,.083],C.clothDark);
  ellipsoid(pelvis,[0,-.045,-.005],[.195,.135,.145],C.clothDark);
  box(pelvis,[0,.015,.135],[.035,.018,.009],C.dark);
  for(const sign of [-1,1]) tube(pelvis,[sign*.075,.04,.116],[sign*.16,-.08,.105],.006,C.seam);
  function limb(out,r0,r1,co) {
    surface(out,16,8,(u,v)=>{
      const a=u*2*PI,r=r0+(r1-r0)*v+.012*Math.sin(v*PI),p=[r*Math.sin(a),v,r*Math.cos(a)];
      return [p,norm([Math.sin(a),r0-r1,Math.cos(a)])];
    },co);
    ellipsoid(out,[0,.06,0],[r0,.08,r0],co);
    ellipsoid(out,[0,.94,0],[r1,.08,r1],co);
  }
  limb(upperArm,.073,.063,C.cloth);limb(foreArm,.067,.045,C.clothDark);
  ellipsoid(upperArm,[0,.30,.065],[.056,.14,.022],C.dark);
  box(upperArm,[0,.51,.07],[.044,.026,.003],C.reflective);
  box(foreArm,[0,.78,.045],[.038,.038,.004],C.seam);
  limb(thigh,.098,.075,C.clothDark);limb(shin,.077,.053,C.clothDark);
  ellipsoid(thigh,[0,.77,.047],[.073,.16,.05],C.dark);
  ellipsoid(shin,[0,.19,.056],[.064,.145,.035],C.dark);
  tube(shin,[.055,.23,.019],[.043,.75,.018],.003,C.seam);
  // Full-face helmet: shell, curved smoked visor, chin guard, vents and seals.
  ellipsoid(helmet,[0,0,0],[.166,.196,.182],C.shell,-.07);
  surface(helmet,26,8,(u,v)=> {
    const a=-1.27+u*2.54,b=1.02+v*.74,p=[.169*Math.sin(b)*Math.sin(a),.198*Math.cos(b),.187*Math.sin(b)*Math.cos(a)];
    return [p,norm([p[0]/(.169*.169),p[1]/(.198*.198),p[2]/(.187*.187)])];
  },C.visor);
  for(const sign of [-1,1]) {
    ellipsoid(helmet,[sign*.154,.006,.054],[.013,.032,.032],C.dark);
    ellipsoid(helmet,[sign*.166,.006,.054],[.004,.017,.017],C.metal);
    curve(helmet,[[sign*.06,.174,-.03],[sign*.115,.135,-.10],[sign*.142,.045,-.13]],.007,C.blue);
    curve(helmet,[[sign*.10,-.093,.127],[sign*.14,-.081,.07],[sign*.154,-.04,.025]],.008,C.black);
    tube(helmet,[sign*.07,.099,.158],[sign*.112,.079,.13],.004,C.metal);
  }
  ellipsoid(helmet,[0,-.106,.108],[.125,.062,.10],C.shell,-.24);
  ellipsoid(helmet,[0,-.114,.199],[.048,.019,.009],C.dark,-.2);
  for(let i=-1;i<=1;i++) box(helmet,[i*.018,-.111,.209],[.004,.011,.002],C.black,-.2);
  ellipsoid(helmet,[0,-.164,.009],[.106,.026,.103],C.black);
  box(helmet,[0,.135,.139],[.027,.009,.006],C.dark,-.7);
  // A studio ground disc and layered contact shadows, kept out of the bank transform.
  tube(floor,[0,-.035,0],[0,-.029,0],2.9,C.floor);
  for(let i=7;i>=0;i--) {
    const shade=170+i*10,h=Math.round(shade).toString(16).padStart(2,'0');
    ellipsoid(floor,[0,-.025+i*.0004,0],[.30+i*.067,.002,1.0+i*.063],color(h+h+h,.02));
  }
  function pack(data) {
    const unique=[],indices=[],seen=new Map();
    for(let i=0;i<data.length;i+=10) {
      let key='';for(let j=0;j<10;j++)key+=Math.round(data[i+j]*100000)+',';
      let index=seen.get(key);
      if(index===undefined){index=unique.length/10;seen.set(key,index);for(let j=0;j<10;j++)unique.push(data[i+j]);}
      indices.push(index);
    }
    const indexed=unique.length/10<65536;
    const result={vertices:new Float32Array(indexed?unique:data),indices:indexed?new Uint16Array(indices):null};
    data.length=0;return result;
  }
  const meshes=[floor,bike,pelvis,body,helmet,upperArm,foreArm,thigh,shin,...gloves].map(pack);
  let program, buffers=[], loc, lost=false, disposed=false, side=0, forward=0,invertedSide=0;
  const vertexSource=`attribute vec3 aPosition;attribute vec3 aNormal;attribute vec4 aColor;uniform mat4 uModel;uniform mat4 uViewProjection;varying vec3 vNormal;varying vec3 vPosition;varying vec4 vColor;void main(){mat3 m=mat3(uModel);mat3 n=mat3(cross(m[1],m[2]),cross(m[2],m[0]),cross(m[0],m[1]));vec4 p=uModel*vec4(aPosition,1.);vNormal=normalize(n*aNormal);vPosition=p.xyz;vColor=aColor;gl_Position=uViewProjection*p;}`;
  const fragmentSource=`precision mediump float;varying vec3 vNormal;varying vec3 vPosition;varying vec4 vColor;uniform vec3 uEye;uniform vec3 uFloor;uniform float uGround;void main(){vec3 n=normalize(vNormal),v=normalize(uEye-vPosition),l=normalize(vec3(-3.,6.,4.)),f=normalize(vec3(4.,2.,-3.));float d=max(dot(n,l),0.),fill=max(dot(n,f),0.),g=vColor.a;float spec=pow(max(dot(n,normalize(l+v)),0.),mix(10.,96.,g))*g*.65;float rim=pow(1.-max(dot(n,v),0.),3.)*.09;vec3 ambient=mix(vec3(.25,.25,.23),vec3(.50,.50,.46),n.y*.5+.5);vec3 base=mix(vColor.rgb,uFloor*vColor.rgb,uGround);vec3 c=base*(ambient+d*.78+fill*vec3(.20,.21,.19))+vec3(spec)+rim*vec3(.46,.47,.43);c=vec3(1.)-exp(-c*1.25);gl_FragColor=vec4(pow(max(c,vec3(0.)),vec3(.4545)),1.);}`;
  function shader(type,source) {
    const s=gl.createShader(type);gl.shaderSource(s,source);gl.compileShader(s);
    if(!gl.getShaderParameter(s,gl.COMPILE_STATUS)){gl.deleteShader(s);throw new Error('Rider graphics unavailable');}
    return s;
  }
  function initialize() {
    const vs=shader(gl.VERTEX_SHADER,vertexSource),fs=shader(gl.FRAGMENT_SHADER,fragmentSource);
    program=gl.createProgram();gl.attachShader(program,vs);gl.attachShader(program,fs);gl.linkProgram(program);gl.deleteShader(vs);gl.deleteShader(fs);
    if(!gl.getProgramParameter(program,gl.LINK_STATUS))throw new Error('Rider graphics unavailable');
    loc={position:gl.getAttribLocation(program,'aPosition'),normal:gl.getAttribLocation(program,'aNormal'),color:gl.getAttribLocation(program,'aColor'),model:gl.getUniformLocation(program,'uModel'),vp:gl.getUniformLocation(program,'uViewProjection'),eye:gl.getUniformLocation(program,'uEye'),floor:gl.getUniformLocation(program,'uFloor'),ground:gl.getUniformLocation(program,'uGround')};
    buffers=meshes.map(data=>{
      const buffer=gl.createBuffer();gl.bindBuffer(gl.ARRAY_BUFFER,buffer);gl.bufferData(gl.ARRAY_BUFFER,data.vertices,gl.STATIC_DRAW);
      const indices=data.indices&&gl.createBuffer();
      if(indices){gl.bindBuffer(gl.ELEMENT_ARRAY_BUFFER,indices);gl.bufferData(gl.ELEMENT_ARRAY_BUFFER,data.indices,gl.STATIC_DRAW);}
      return {buffer,indices,count:data.indices?data.indices.length:data.vertices.length/10};
    });
    gl.enable(gl.DEPTH_TEST);gl.disable(gl.CULL_FACE);gl.useProgram(program);
    for(const attribute of [loc.position,loc.normal,loc.color])gl.enableVertexAttribArray(attribute);
  }
  try { initialize(); } catch (_) { if(program)gl.deleteProgram(program);return noScene; }
  function draw(index,model) {
    gl.uniform1f(loc.ground,index===0?1:0);
    const mesh=buffers[index];gl.bindBuffer(gl.ARRAY_BUFFER,mesh.buffer);
    gl.vertexAttribPointer(loc.position,3,gl.FLOAT,false,40,0);gl.vertexAttribPointer(loc.normal,3,gl.FLOAT,false,40,12);gl.vertexAttribPointer(loc.color,4,gl.FLOAT,false,40,24);
    gl.uniformMatrix4fv(loc.model,false,model);
    if(mesh.indices){gl.bindBuffer(gl.ELEMENT_ARRAY_BUFFER,mesh.indices);gl.drawElements(gl.TRIANGLES,mesh.count,gl.UNSIGNED_SHORT,0);}
    else gl.drawArrays(gl.TRIANGLES,0,mesh.count);
  }
  // Analytic two-bone IK: exact contact targets, no iterative solver or joint jitter.
  function joint(a,b,l1,l2,pole) {
    const delta=sub(b,a),d=Math.max(.0001,Math.hypot(...delta)),axis=mul(delta,1/d),p=norm(sub(pole,mul(axis,dot(pole,axis))));
    const along=clamp((l1*l1-l2*l2+d*d)/(2*d),-l1,l1),height=Math.sqrt(Math.max(0,l1*l1-along*along));
    return add(a,add(mul(axis,along),mul(p,height)));
  }
  const identityMatrix=identity();
  function poseParts() {
    const bank=clamp(side*.72,-62,62)*rad,roll=clamp(side-bank/rad,-24,24)*rad,pitch=clamp(18+forward,-25,70)*rad;
    const root=transform([0,0,0],[1,1,1],0,0,-bank),hip=[0,1.105,-.36];
    const waist=multiply(root,transform(hip));
    const torsoLocal=multiply(transform(hip),transform([0,0,0],[1,1,1],pitch,0,-roll));
    const torso=multiply(root,torsoLocal);
    const head=multiply(torso,multiply(transform([0,.565,0],[1,1,1],-pitch*.8,0,roll*.12),transform([0,.17,.005])));
    const parts=[[0,identityMatrix],[1,root],[2,waist],[3,torso],[4,head]],handParts=[];
    for(const sign of [-1,1]) {
      const shoulder=point(torsoLocal,[sign*.228,.465,0]),grip=[sign*.366,1.23,.37],reach=sub(grip,shoulder),distance=Math.hypot(...reach);
      const hand=distance>.789?add(shoulder,mul(reach,.789/distance)):grip;
      const elbow=joint(shoulder,hand,.40,.39,[sign*.8,-.6,-.6]);
      parts.push([5,multiply(root,segment(shoulder,elbow))],[6,multiply(root,segment(elbow,hand))]);
      handParts.push([sign<0?9:10,multiply(root,transform(hand))]);
      const hipJoint=add(hip,[sign*.135,-.075,-.005]),ankle=[sign*.27,.525,.005],knee=joint(hipJoint,ankle,.43,.405,[sign*.15,0,1]);
      parts.push([7,multiply(root,segment(hipJoint,knee))],[8,multiply(root,segment(knee,ankle))]);
    }
    return parts.concat(handParts);
  }
  let poseDirty=true,parts=poseParts(),width=0,height=0,painted=false;
  let sceneBackground=[.933,.914,.859],sceneFloor=[.730,.687,.580];
  function theme() {
    const styles=getComputedStyle(document.documentElement);
    const rgb=(name,fallback)=>{const hex=styles.getPropertyValue(name).trim();return /^#[0-9a-f]{6}$/i.test(hex)?hex.slice(1).match(/../g).map(n=>parseInt(n,16)/255):fallback;};
    sceneBackground=rgb('--scene-bg',sceneBackground);
    sceneFloor=rgb('--scene-floor',[.867,.843,.780]).map(n=>Math.pow(n,2.2));
    painted=false;render();
  }
  theme();
  window.addEventListener('themechange',theme);
  function render() {
    if(disposed||lost||!canvas.isConnected)return;
    const rect=canvas.getBoundingClientRect(),dpr=Math.min(window.devicePixelRatio||1,1.5),w=Math.max(1,Math.round(rect.width*dpr)),h=Math.max(1,Math.round(rect.height*dpr));
    if(rect.width<1||rect.height<1)return;
    const resized=w!==width||h!==height;
    if(!poseDirty&&!resized&&painted)return;
    if(resized){width=canvas.width=w;height=canvas.height=h;}
    if(poseDirty){parts=poseParts();poseDirty=false;}
    gl.useProgram(program);gl.clearColor(...sceneBackground,1);gl.uniform3fv(loc.floor,sceneFloor);gl.clear(gl.COLOR_BUFFER_BIT|gl.DEPTH_BUFFER_BIT);
    const stacked=rect.width<560,half=Math.floor((stacked?h:w)/2);
    canvas.dataset.sceneLayout=stacked?'stacked':'columns';
    for(let view=0;view<2;view++) {
      const cellHeight=stacked?(view?half:h-half):h,inset=stacked?Math.round(5*dpr):0;
      const viewWidth=stacked?w:(view?w-half:half),viewHeight=cellHeight-(stacked?Math.round(27*dpr):0);
      gl.viewport(stacked?0:(view?half:0),stacked?(view?inset:half+inset):0,viewWidth,Math.max(1,viewHeight));
      // Near-orthographic cameras keep front/side lean easy to compare without foreshortening.
      const centerX=view?0:parts[4][1][12]*.4;
      const target=[centerX,.95,.01],eye=view?[-5.2,2.0,.02]:[centerX+.03,1.95,5.8];
      const proj=projection(viewWidth/viewHeight,view);
      if(!view)proj[0]*=-1;
      const vp=multiply(proj,lookAt(eye,target));
      gl.uniformMatrix4fv(loc.vp,false,vp);gl.uniform3fv(loc.eye,eye);
      for(const [index,matrix] of parts)draw(index,matrix);
    }
    painted=true;
  }
  function contextLost(event) {event.preventDefault();lost=true;canvas.dataset.sceneUnavailable='true';}
  function contextRestored() {
    if(disposed)return;
    try {initialize();lost=false;poseDirty=true;painted=false;delete canvas.dataset.sceneUnavailable;render();}
    catch (_) {lost=true;}
  }
  canvas.addEventListener('webglcontextlost',contextLost);canvas.addEventListener('webglcontextrestored',contextRestored);
  return {
    supported:true,
    setPose(sideDeg,forwardDeg) {
      const angle=Number.isFinite(sideDeg)?((sideDeg+180)%360+360)%360-180:0,absolute=Math.abs(angle);
      if(absolute>=145&&!invertedSide)invertedSide=Math.sign(angle)||1;
      // Outside a seated pose, preserve the fallen side across the ±180° wrap.
      // Returning toward upright releases that bounded illustration continuously.
      let nextSide=clamp(angle,-86,86);
      if(invertedSide&&Math.sign(angle)!==invertedSide&&absolute>4)nextSide=invertedSide*86*Math.min(absolute/145,1);
      else if(absolute<100)invertedSide=0;
      const nextForward=clamp(Number.isFinite(forwardDeg)?forwardDeg:0,-80,80);
      if(Math.abs(nextSide-side)>.001||Math.abs(nextForward-forward)>.001){side=nextSide;forward=nextForward;poseDirty=true;}
    },
    render,
    dispose() {
      if(disposed)return;disposed=true;window.removeEventListener('themechange',theme);canvas.removeEventListener('webglcontextlost',contextLost);canvas.removeEventListener('webglcontextrestored',contextRestored);
      for(const {buffer,indices} of buffers){gl.deleteBuffer(buffer);if(indices)gl.deleteBuffer(indices);}
      gl.deleteProgram(program);buffers=[];
      const release=gl.getExtension('WEBGL_lose_context');if(release)release.loseContext();
    }
  };
}
