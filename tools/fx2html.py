r"""Brute Force (Xbox) effect viewer: the particle effects -> one HTML page that plays them.

Reads common\effects-common_json\ (made by export_effects.bat) and the effect textures
(made by export_textures.bat) and writes common\effects_viewer.html: a self-contained page
(effects and textures embedded, works offline and from a local folder) that lists all 603
effects and plays the selected one in 3D with WebGL. Drag to orbit, mouse wheel to zoom.

Usage:
    python fx2html.py <data folder>

This is a preview, not the game's renderer. It follows the Alchemy effect model
(Freelancer's .ale): emitters (cone, sphere, cube) create particles at Emitter_Frequency
per second with Emitter_Pressure speed and Emitter_InitLifeSpan life; the appearance linked
to an emitter draws them as camera-facing quads with its texture, blend mode and colour /
alpha / size curves over the particle's life; the fields linked to the appearance (gravity,
air, turbulence, radial) move them. Inferred, not confirmed: the cone axis (local +Y), the
sign of gravity, and how texture-frame values pick frames.
Pure Python standard library.
"""
import base64, glob, json, os, sys


def main(argv):
    if not argv:
        print(__doc__); return 1
    root = argv[0]
    src = os.path.join(root, 'common', 'effects-common_json')
    files = sorted(f for f in glob.glob(os.path.join(src, '*.json')) if not os.path.basename(f).startswith('_'))
    if not files:
        print('No effects found in %s. Run export_effects.bat first.' % src); return 1
    effects, textures = [], {}
    for f in files:
        d = json.load(open(f, encoding='utf-8'))
        for n in d.get('nodes', []):
            p = n.get('texture_png')
            if p and p not in textures:
                full = os.path.normpath(os.path.join(src, p))
                if os.path.exists(full):
                    textures[p] = 'data:image/png;base64,' + base64.b64encode(open(full, 'rb').read()).decode()
                else:
                    textures[p] = None
        effects.append(d)
    data = json.dumps({'effects': effects, 'textures': textures}, separators=(',', ':'))
    page = PAGE.replace('/*DATA*/null', data)
    out = os.path.join(root, 'common', 'effects_viewer.html')
    open(out, 'w', encoding='utf-8').write(page)
    print('%d effects, %d textures (%d missing) -> %s (%.1f MB)' % (
        len(effects), len(textures), sum(1 for v in textures.values() if v is None), out, os.path.getsize(out) / 1e6))
    return 0


PAGE = r'''<!doctype html><html><head><meta charset="utf-8"><title>Brute Force effects</title>
<meta name="viewport" content="width=device-width,initial-scale=1"><style>
html,body{margin:0;height:100%;background:#0e0f12;color:#ddd;font:13px system-ui,sans-serif}
#side{position:absolute;left:0;top:0;bottom:0;width:260px;display:flex;flex-direction:column;border-right:1px solid #262a31}
#side input{margin:8px;padding:6px;background:#1a1d22;border:1px solid #333;color:#ddd;border-radius:4px}
#list{flex:1;overflow:auto}#list div{padding:3px 10px;cursor:pointer;white-space:nowrap}
#list div:hover{background:#1c2027}#list div.on{background:#2a3a5a}
#main{position:absolute;left:261px;right:0;top:0;bottom:0}canvas{width:100%;height:100%;display:block}
#bar{position:absolute;left:10px;top:8px;display:flex;gap:10px;align-items:center;background:#000a;padding:6px 10px;border-radius:6px}
#bar button,#bar select{background:#1f2430;color:#ddd;border:1px solid #3a4150;border-radius:4px;padding:3px 8px}
#info{position:absolute;left:10px;bottom:8px;max-width:60%;background:#000a;padding:6px 10px;border-radius:6px;color:#aaa;font-size:12px}
@media (max-width:700px){#side{width:150px}#main{left:151px}}
</style></head><body><div id="side"><input id="q" placeholder="Search 603 effects"><div id="list"></div></div>
<div id="main"><canvas id="c"></canvas><div id="bar"><b id="title"></b><button id="restart">Restart</button>
<label><input type="checkbox" id="loop" checked> Loop</label><select id="speed"><option value="0.25">0.25x</option>
<option value="0.5">0.5x</option><option value="1" selected>1x</option><option value="2">2x</option></select>
<span id="stat"></span></div><div id="info"></div></div>
<script>
var DATA=/*DATA*/null;
var gl,prog,buf,texCache={},white,cur=null,parts=[],T=0,last=0,cam={yaw:0.6,pitch:0.35,dist:10,tgt:[0,0,0]};
function rnd(a,b){return a+Math.random()*(b-a)}
function lerp(a,b,f){return a+(b-a)*f}
function keysAt(keys,t){if(!keys.length)return 0;if(t<=keys[0][0])return keys[0][1];var L=keys[keys.length-1];if(t>=L[0])return L[1];
 for(var i=1;i<keys.length;i++){if(t<=keys[i][0]){var a=keys[i-1],b=keys[i],f=(t-a[0])/((b[0]-a[0])||1);
 if(Array.isArray(a[1]))return a[1].map(function(v,j){return lerp(v,b[1][j],f)});return lerp(a[1],b[1],f)}}return L[1]}
// a property: number / bool / {items:[{at,value?,keys?,loop?}]}; tE = effect time, age = 0..1 particle age
function ev(p,tE,age,def){if(p===undefined||p===null)return def;if(typeof p!=='object')return p;if(!p.items||!p.items.length)return def;
 var it=p.items[0];for(var i=0;i<p.items.length;i++)if(p.items[i].at<=tE)it=p.items[i];
 if(it.keys&&it.keys.length){if('value' in it){var kt=tE-it.at,L=it.keys[it.keys.length-1][0];if(it.loop&&L>0)kt=kt%L;return keysAt(it.keys,kt)}
  return keysAt(it.keys,age===undefined?0:age)}return 'value' in it?it.value:def}
function mat(){return [1,0,0,0,1,0,0,0,1,0,0,0]}
function mmul(a,b){var r=[];for(var i=0;i<3;i++)for(var j=0;j<3;j++)r[i*3+j]=a[i*3]*b[j]+a[i*3+1]*b[3+j]+a[i*3+2]*b[6+j];
 for(var j=0;j<3;j++)r[9+j]=a[9]*b[j]+a[10]*b[3+j]+a[11]*b[6+j]+b[9+j];return r}
function xform(tr,t){if(!tr)return mat();var g=function(k,d){return ev(tr[k],t,0,d)},D=Math.PI/180;
 var rx=g('rotate_x',0)*D,ry=g('rotate_y',0)*D,rz=g('rotate_z',0)*D,sx=g('scale_x',1),sy=g('scale_y',1),sz=g('scale_z',1);
 var cx=Math.cos(rx),sxn=Math.sin(rx),cy=Math.cos(ry),syn=Math.sin(ry),cz=Math.cos(rz),szn=Math.sin(rz);
 var X=[1,0,0,0,cx,sxn,0,-sxn,cx,0,0,0],Y=[cy,0,-syn,0,1,0,syn,0,cy,0,0,0],Z=[cz,szn,0,-szn,cz,0,0,0,1,0,0,0];
 var S=[sx,0,0,0,sy,0,0,0,sz,0,0,0],m=mmul(mmul(mmul(S,X),Y),Z);m[9]=g('translate_x',0);m[10]=g('translate_y',0);m[11]=g('translate_z',0);return m}
function apply(m,v){return [v[0]*m[0]+v[1]*m[3]+v[2]*m[6]+m[9],v[0]*m[1]+v[1]*m[4]+v[2]*m[7]+m[10],v[0]*m[2]+v[1]*m[5]+v[2]*m[8]+m[11]]}
function dirv(m,v){return [v[0]*m[0]+v[1]*m[3]+v[2]*m[6],v[0]*m[1]+v[1]*m[4]+v[2]*m[7],v[0]*m[2]+v[1]*m[5]+v[2]*m[8]]}
function norm(v){var l=Math.hypot(v[0],v[1],v[2])||1;return [v[0]/l,v[1]/l,v[2]/l]}
function world(e,n,t){var m=xform(n.properties.Node_Transform,t),p=n.parent,guard=0;
 while(p!==undefined&&p!==null&&guard++<16){var pn=e.byIndex[p];if(!pn||!pn.properties)break;m=mmul(m,xform(pn.properties.Node_Transform,t));p=pn.parent}return m}
function setup(e){e.byIndex={};e.nodes.forEach(function(n){e.byIndex[n.index]=n});e.emitters=[];
 var out={};(e.links||[]).forEach(function(l){(out[l.from]=out[l.from]||[]).push(l.to)});
 e.nodes.forEach(function(n){if(!n.class||!/Emitter/.test(n.class))return;
  var apps=(out[n.index]||[]).map(function(i){return e.byIndex[i]}).filter(function(a){return a&&a.class&&/Appearance/.test(a.class)});
  apps.forEach(function(a){var fields=(out[a.index]||[]).map(function(i){return e.byIndex[i]}).filter(function(f){return f&&f.class&&/Field/.test(f.class)});
   e.emitters.push({n:n,app:a,fields:fields,acc:0,started:false})})})}
function emitOne(e,em,t){var P=em.n.properties,c=em.n.class,m=world(e,em.n,t),pos=[0,0,0],dir=[0,1,0],D=Math.PI/180;
 if(/Cone/.test(c)){var s=rnd(ev(P.ConeEmitter_MinSpread,t,0,0),ev(P.ConeEmitter_MaxSpread,t,0,0))*D,a=rnd(0,6.2832);
  dir=[Math.sin(s)*Math.cos(a),Math.cos(s),Math.sin(s)*Math.sin(a)];var r=rnd(ev(P.ConeEmitter_MinRadius,t,0,0),ev(P.ConeEmitter_MaxRadius,t,0,0));pos=[dir[0]*r,dir[1]*r,dir[2]*r]}
 else if(/Sphere/.test(c)){dir=norm([rnd(-1,1),rnd(-1,1),rnd(-1,1)]);var r2=rnd(ev(P.SphereEmitter_MinRadius,t,0,0),ev(P.SphereEmitter_MaxRadius,t,0,0));pos=[dir[0]*r2,dir[1]*r2,dir[2]*r2]}
 else if(/Cube/.test(c)){var w=ev(P.CubeEmitter_Width,t,0,0),h=ev(P.CubeEmitter_Height,t,0,0),d=ev(P.CubeEmitter_Depth,t,0,0);
  pos=[rnd(-w/2,w/2),rnd(-h/2,h/2)+ev(P.CubeEmitter_OffsetHeight,t,0,0),rnd(-d/2,d/2)];var s2=rnd(ev(P.CubeEmitter_MinSpread,t,0,0),ev(P.CubeEmitter_MaxSpread,t,0,0))*D,a2=rnd(0,6.2832);
  dir=[Math.sin(s2)*Math.cos(a2),Math.cos(s2),Math.sin(s2)*Math.sin(a2)]}
 var sp=ev(P.Emitter_Pressure,t,0,1),life=Math.max(0.01,ev(P.Emitter_InitLifeSpan,t,0,1)),wd=norm(dirv(m,dir));
 parts.push({p:apply(m,pos),v:[wd[0]*sp,wd[1]*sp,wd[2]*sp],age:0,life:life,em:em,rot:rnd(0,6.28)})}
function step(dt){if(!cur)return;var e=cur;T+=dt;var dur=e.duration;
 e.emitters.forEach(function(em){var P=em.n.properties,ls=P.Node_LifeSpan||0;
  if(!em.started){em.started=true;var ni=ev(P.Emitter_InitialParticles,0,0,0)|0;for(var i=0;i<ni;i++)emitOne(e,em,T)}
  if(ls>0&&T>ls)return;var f=ev(P.Emitter_Frequency,T,0,0),cnt=Math.max(1,ev(P.Emitter_EmitCount,T,0,1)|0);em.acc+=f*dt;
  while(em.acc>=1&&parts.length<6000){em.acc-=1;for(var k=0;k<cnt;k++)emitOne(e,em,T)}});
 for(var i=parts.length-1;i>=0;i--){var q=parts[i];q.age+=dt;if(q.age>=q.life){parts.splice(i,1);continue}
  q.em.fields.forEach(function(fl){var F=fl.properties,c=fl.class;
   if(/Gravity/.test(c)){q.v[1]-=ev(F.GravityField_Gravity,T,0,0)*dt}
   else if(/Air/.test(c)){var m=world(e,fl,T),dd=norm(dirv(m,[0,1,0])),mg=ev(F.AirField_Magnitude,T,0,0),ap=Math.min(1,ev(F.AirField_Approach,T,0,1)*dt);
    for(var j=0;j<3;j++)q.v[j]+=(dd[j]*mg-q.v[j])*ap}
   else if(/Turbulence/.test(c)){var mg2=ev(F.TurbulenceField_Magnitude,T,0,0)*60;for(var j2=0;j2<3;j2++)q.v[j2]+=rnd(-mg2,mg2)*dt}
   else if(/Radial/.test(c)){var m2=world(e,fl,T),o=[m2[9],m2[10],m2[11]],d=[q.p[0]-o[0],q.p[1]-o[1],q.p[2]-o[2]],L=Math.hypot(d[0],d[1],d[2])||1,R=ev(F.RadialField_Radius,T,0,1);
    if(L<R){var a=ev(F.RadialField_Magnitude,T,0,0)*dt/L;for(var j3=0;j3<3;j3++)q.v[j3]+=d[j3]*a}}});
  var va=ev(q.em.n.properties.Emitter_VelocityApproach,T,0,0);if(va>0){var k2=Math.max(0,1-va*dt);q.v=[q.v[0]*k2,q.v[1]*k2,q.v[2]*k2]}
  for(var j4=0;j4<3;j4++)q.p[j4]+=q.v[j4]*dt}
 if(T>dur&&!parts.length&&document.getElementById('loop').checked)start(cur)}
function texFor(app){var key=app.texture_png;if(!key||!DATA.textures[key])return white;if(texCache[key])return texCache[key];
 var t=gl.createTexture(),img=new Image();gl.bindTexture(gl.TEXTURE_2D,t);gl.texImage2D(gl.TEXTURE_2D,0,gl.RGBA,1,1,0,gl.RGBA,gl.UNSIGNED_BYTE,new Uint8Array([255,255,255,255]));
 img.onload=function(){gl.bindTexture(gl.TEXTURE_2D,t);gl.texImage2D(gl.TEXTURE_2D,0,gl.RGBA,gl.RGBA,gl.UNSIGNED_BYTE,img);
  gl.texParameteri(gl.TEXTURE_2D,gl.TEXTURE_MIN_FILTER,gl.LINEAR);gl.texParameteri(gl.TEXTURE_2D,gl.TEXTURE_WRAP_S,gl.CLAMP_TO_EDGE);gl.texParameteri(gl.TEXTURE_2D,gl.TEXTURE_WRAP_T,gl.CLAMP_TO_EDGE)};
 img.src=DATA.textures[key];texCache[key]=t;return t}
var BL={ZERO:0,ONE:1,SRCCOLOR:0x300,INVSRCCOLOR:0x301,SRCALPHA:0x302,INVSRCALPHA:0x303,DESTALPHA:0x304,INVDESTALPHA:0x305,DESTCOLOR:0x306,INVDESTCOLOR:0x307};
function draw(){var c=gl.canvas,w=c.clientWidth,h=c.clientHeight;if(c.width!==w||c.height!==h){c.width=w;c.height=h}gl.viewport(0,0,w,h);
 gl.clearColor(0.06,0.065,0.08,1);gl.clear(gl.COLOR_BUFFER_BIT);if(!cur)return;
 var cp=Math.cos(cam.pitch),eye=[cam.tgt[0]+cam.dist*cp*Math.sin(cam.yaw),cam.tgt[1]+cam.dist*Math.sin(cam.pitch),cam.tgt[2]+cam.dist*cp*Math.cos(cam.yaw)];
 var f=norm([cam.tgt[0]-eye[0],cam.tgt[1]-eye[1],cam.tgt[2]-eye[2]]),r=norm([f[2],0,-f[0]]),u=[r[1]*f[2]-r[2]*f[1],r[2]*f[0]-r[0]*f[2],r[0]*f[1]-r[1]*f[0]];
 var fl=1/Math.tan(0.5),asp=w/h,groups={};
 parts.forEach(function(q){var d=[q.p[0]-eye[0],q.p[1]-eye[1],q.p[2]-eye[2]];q.z=d[0]*f[0]+d[1]*f[1]+d[2]*f[2];var k=q.em.app.index;(groups[k]=groups[k]||[]).push(q)});
 Object.keys(groups).forEach(function(k){var list=groups[k].sort(function(a,b){return b.z-a.z}),app=list[0].em.app,A=app.properties,v=[];
  var pre=A.GeneralApp_Color?'GeneralApp_':'BasicApp_',bi=A[pre+'BlendInfo']||{source:'SRCALPHA',dest:'INVSRCALPHA'};
  var frames=app.texture_frames||1,cols=Math.ceil(Math.sqrt(frames));
  list.forEach(function(q){var a=q.age/q.life,col=ev(A[pre+'Color'],T,a,[1,1,1]),al=ev(A[pre+'Alpha'],T,a,1),
   sz=ev(A.GeneralApp_Scale!==undefined?A.GeneralApp_Scale:A.BasicApp_Size,T,a,1)*0.5,rot=ev(A[pre+'Rotate'],T,a,0)*Math.PI/180;
   if(!(q.z>0.05))return;sz=Math.max(sz,1.5*q.z/(fl*h/2));var cx=(q.p[0]-eye[0])*r[0]+(q.p[1]-eye[1])*r[1]+(q.p[2]-eye[2])*r[2],cy=(q.p[0]-eye[0])*u[0]+(q.p[1]-eye[1])*u[1]+(q.p[2]-eye[2])*u[2];
   var fr=0;if(frames>1){var tv=ev(A[pre+'TexFrame'],T,a,0);fr=Math.floor(tv<=1?tv*(frames-1)+0.5:tv)%frames;if(fr<0)fr=0}
   var u0=(fr%cols)/cols,v0=Math.floor(fr/cols)/cols,du=1/cols,cs=Math.cos(rot)*sz,sn=Math.sin(rot)*sz;
   var cn=[[-1,-1,u0,v0+du],[1,-1,u0+du,v0+du],[1,1,u0+du,v0],[-1,-1,u0,v0+du],[1,1,u0+du,v0],[-1,1,u0,v0]];
   cn.forEach(function(k2){var x=cx+k2[0]*cs-k2[1]*sn,y=cy+k2[0]*sn+k2[1]*cs;v.push(x*fl/asp/q.z,y*fl/q.z,0,k2[2],k2[3],col[0],col[1],col[2],al)})});
  if(!v.length)return;gl.enable(gl.BLEND);gl.blendFunc(BL[bi.source]!==undefined?BL[bi.source]:0x302,BL[bi.dest]!==undefined?BL[bi.dest]:0x303);
  gl.bindTexture(gl.TEXTURE_2D,texFor(app));gl.bufferData(gl.ARRAY_BUFFER,new Float32Array(v),gl.STREAM_DRAW);gl.drawArrays(gl.TRIANGLES,0,v.length/9)});
 document.getElementById('stat').textContent=parts.length+' particles, '+T.toFixed(2)+' s'}
function initGL(){var c=document.getElementById('c');gl=c.getContext('webgl',{premultipliedAlpha:false,alpha:false});
 var vs='attribute vec3 p;attribute vec2 t;attribute vec4 c;varying vec2 vt;varying vec4 vc;void main(){gl_Position=vec4(p,1.0);vt=t;vc=c;}';
 var fs='precision mediump float;uniform sampler2D s;varying vec2 vt;varying vec4 vc;void main(){gl_FragColor=texture2D(s,vt)*vc;}';
 function sh(t,s){var o=gl.createShader(t);gl.shaderSource(o,s);gl.compileShader(o);return o}
 prog=gl.createProgram();gl.attachShader(prog,sh(gl.VERTEX_SHADER,vs));gl.attachShader(prog,sh(gl.FRAGMENT_SHADER,fs));gl.linkProgram(prog);gl.useProgram(prog);
 buf=gl.createBuffer();gl.bindBuffer(gl.ARRAY_BUFFER,buf);var st=36;
 [['p',3,0],['t',2,12],['c',4,20]].forEach(function(a){var l=gl.getAttribLocation(prog,a[0]);gl.enableVertexAttribArray(l);gl.vertexAttribPointer(l,a[1],gl.FLOAT,false,st,a[2])});
 // a soft round particle for appearances without a texture
 var n=32,px=new Uint8Array(n*n*4);for(var y=0;y<n;y++)for(var x=0;x<n;x++){var d=Math.hypot(x-n/2+0.5,y-n/2+0.5)/(n/2),a=Math.max(0,1-d);var o=(y*n+x)*4;px[o]=px[o+1]=px[o+2]=255;px[o+3]=a*a*255}
 white=gl.createTexture();gl.bindTexture(gl.TEXTURE_2D,white);gl.texImage2D(gl.TEXTURE_2D,0,gl.RGBA,n,n,0,gl.RGBA,gl.UNSIGNED_BYTE,px);gl.texParameteri(gl.TEXTURE_2D,gl.TEXTURE_MIN_FILTER,gl.LINEAR);
 var drag=null;c.onmousedown=function(e){drag=[e.clientX,e.clientY]};window.onmouseup=function(){drag=null};
 window.onmousemove=function(e){if(!drag)return;cam.yaw-=(e.clientX-drag[0])*0.01;cam.pitch=Math.max(-1.5,Math.min(1.5,cam.pitch+(e.clientY-drag[1])*0.01));drag=[e.clientX,e.clientY]};
 c.onwheel=function(e){e.preventDefault();cam.dist*=e.deltaY>0?1.1:0.9}}
function start(e){cur=e;parts=[];T=0;if(!e.byIndex)setup(e);e.emitters.forEach(function(m){m.acc=0;m.started=false});
 var bs=e.bounding_sphere||{center:[0,0,0],radius:3};cam.tgt=bs.center.slice();cam.dist=Math.max(1,bs.radius*1.8);
 e.duration=Math.max(0.5,Math.max.apply(null,e.nodes.map(function(n){return n.properties&&n.properties.Node_LifeSpan>0&&n.properties.Node_LifeSpan<60?n.properties.Node_LifeSpan:0}).concat([0.5])));
 document.getElementById('title').textContent=e.name;
 document.getElementById('info').textContent=e.emitters.length+' emitter/appearance pairs. '+e.nodes.filter(function(n){return n.class}).map(function(n){return n.class.replace(/^(Fx|BF)/,'')}).join(', ')}
function loop(ts){var dt=Math.min(0.05,(ts-(last||ts))/1000)*parseFloat(document.getElementById('speed').value);last=ts;step(dt);draw();requestAnimationFrame(loop)}
initGL();var L=document.getElementById('list'),items=[];
DATA.effects.forEach(function(e,i){var d=document.createElement('div');d.textContent=e.name;d.onclick=function(){items.forEach(function(x){x.className=''});d.className='on';start(e)};L.appendChild(d);items.push(d)});
document.getElementById('q').oninput=function(){var s=this.value.toLowerCase();items.forEach(function(d){d.style.display=d.textContent.toLowerCase().indexOf(s)>=0?'':'none'})};
document.getElementById('restart').onclick=function(){if(cur)start(cur)};
var first=DATA.effects.findIndex(function(e){return /explo/.test(e.name)});items[first<0?0:first].onclick();items[first<0?0:first].scrollIntoView({block:'center'});
requestAnimationFrame(loop);
</script></body></html>'''

if __name__ == '__main__':
    sys.exit(main(sys.argv[1:]))
