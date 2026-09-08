"""Generate a self-contained cube drop scene and optionally time 600 rendered frames."""
import argparse, json, math, pathlib, struct, subprocess, time, re, statistics
p=argparse.ArgumentParser(); p.add_argument('--cubes',type=int,default=1000); p.add_argument('--run',action='store_true'); a=p.parse_args()
if a.cubes < 1: p.error('cubes must be positive')
out=pathlib.Path('scenes'); out.mkdir(exist_ok=True); stem=f'avbd-{a.cubes}'
# Six outward-facing cube faces, with the same 48-byte layout as the viewer.
verts=[]
for axis in range(3):
 for sign in [-1,1]:
  u=(axis+1)%3; v=(axis+2)%3
  corners=[]
  for x,y in [(-1,-1),(1,-1),(1,1),(-1,1)]:
   pos=[0.,0.,0.]; pos[axis]=sign*.5;pos[u]=x*.5;pos[v]=y*.5;corners.append(pos)
  normal=[0.,0.,0.];normal[axis]=sign
  tangent=[0.,0.,0.];tangent[u]=1
  for i in ([0,1,2,0,2,3] if sign>0 else [0,2,1,0,3,2]):
   verts.extend(corners[i]+normal+tangent+[1.,0.,0.])
(out/f'{stem}.b72').write_bytes(struct.pack('<'+'f'*len(verts),*verts))
side=math.ceil(math.sqrt(a.cubes)); width=side*1.5+4
s=['s72-v2',dict(type='SCENE',name=stem,roots=['camera node','light node','ground node']+[f'cube {i}' for i in range(a.cubes)])]
def node(name,translation,**kw):return dict(type='NODE',name=name,translation=translation,rotation=[0,0,0,1],scale=[1,1,1],children=[],**kw)
s += [dict(type='CAMERA',name='camera',perspective=dict(aspect=16/9,vfov=math.pi/3,near=.1,far=2000)),node('camera node',[0,width*.85,width*.9],camera='camera'),dict(type='LIGHT',name='sun',tint=[1,1,1],sun=dict(angle=.01,strength=1)),node('light node',[0,10,0],light='sun')]
s[5]['rotation']=[-math.sin(math.pi/4),0,0,math.cos(math.pi/4)]
s[3]['rotation']=[-math.sin(math.pi/8),0,0,math.cos(math.pi/8)]
s += [dict(type='MATERIAL',name='white',lambertian=dict(albedo=[.7,.8,1])),dict(type='MESH',name='cube mesh',topology='TRIANGLE_LIST',count=36,material='white',attributes={k:dict(src=f'{stem}.b72',offset=o,stride=48,format=f) for k,o,f in [('POSITION',0,'R32G32B32_SFLOAT'),('NORMAL',12,'R32G32B32_SFLOAT'),('TANGENT',24,'R32G32B32A32_SFLOAT'),('TEXCOORD',40,'R32G32_SFLOAT')]})]
for name,size,static in [('cube',[1,1,1],False),('ground',[width,1,width],True)]:
 s += [dict(type='COLLIDER',name=name+' collider',offset=dict(translation=[0,0,0],rotation=[0,0,0,1]),box=dict(extents=size)),dict(type='RIGIDBODY',name=name+' body',mass=1,collider=name+' collider',is_static=static)]
s.append(node('ground node',[0,-.5,0],mesh='cube mesh',rigidbody='ground body'));s[-1]['scale']=[width,1,width]
for i in range(a.cubes):s.append(node(f'cube {i}',[(i%side-(side-1)/2)*1.5,3,(i//side-(side-1)/2)*1.5],mesh='cube mesh',rigidbody='cube body'))
path=out/f'{stem}.s72';path.write_text(json.dumps(s))
if a.run:
 commands='AVAILABLE 0.016666667\n'*599+f'AVAILABLE 0.016666667 scenes/{stem}-10s.png\n'
 start=time.perf_counter()
 with (out/f'{stem}.log').open('w') as log:
  r=subprocess.run(['bin/viewer.exe','--scene',str(path),'--camera','camera','--physics','gpu','--headless','--no-debug','--drawing-size','640','360'],input=commands,text=True,stdout=log,stderr=subprocess.STDOUT)
 elapsed=time.perf_counter()-start
 print(f'cubes={a.cubes} frames=600 total_seconds={elapsed:.3f} average_fps_including_startup={600/elapsed:.2f} exit={r.returncode}')
 times=[float(x) for x in re.findall(r'REPORT frame-time ([\d.]+)ms',(out/f'{stem}.log').read_text())][60:-1]
 if times: print(f'median_frame_ms={statistics.median(times):.3f} p95_frame_ms={sorted(times)[int(len(times)*.95)]:.3f}')
 raise SystemExit(r.returncode)
