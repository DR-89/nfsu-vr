from lxml import etree as E
import re,copy
from pathlib import Path
r=E.parse('/home/pavel/Developer/ThanitMay_27.svg').getroot();ns=r.nsmap[None];g=r.xpath('//*[@id="group-R5"]')[0]
def bounds(d):
 t=re.findall('[a-zA-Z]|[-+]?(?:[0-9]*\\.)?[0-9]+(?:[eE][-+]?[0-9]+)?',d);i=0;x=y=0;sx=sy=0;pts=[];cmd=''
 while i<len(t):
  if t[i].isalpha():cmd=t[i];i+=1
  c=cmd.upper(); rel=cmd.islower()
  if c=='Z':x,y=sx,sy;cmd='';continue
  n={'M':2,'L':2,'H':1,'V':1,'C':6,'S':4,'Q':4,'T':2}.get(c)
  if n is None: return None
  vals=list(map(float,t[i:i+n]));i+=n
  if c=='H': x=vals[0]+(x if rel else 0);pts.append((x,y))
  elif c=='V':y=vals[0]+(y if rel else 0);pts.append((x,y))
  else:
   pairs=[(vals[k]+(x if rel else 0),vals[k+1]+(y if rel else 0)) for k in range(0,n,2)];pts.extend(pairs);x,y=pairs[-1]
   if c=='M':sx,sy=x,y;cmd='l' if rel else 'L'
 return (min(a for a,b in pts),min(b for a,b in pts),max(a for a,b in pts),max(b for a,b in pts)) if pts else None
for name,box in {'shift_down':(380,3340,700,3810),'shift_up':(930,3340,1250,3810)}.items():
 out=E.Element(r.tag,nsmap={None:ns},width='256',height='384',viewBox=f'{box[0]} {box[1]} {box[2]-box[0]} {box[3]-box[1]}');out.append(copy.deepcopy(r[0]));cnt=0
 for e in g:
  if E.QName(e).localname!='path':continue
  b=bounds(e.get('d',''))
  if not b:continue
  tr=list(map(float,re.findall(r'[-+]?(?:[0-9]*\.)?[0-9]+',e.get('transform',''))))
  if len(tr)!=6:continue
  a,bb,c,d,tx,ty=tr; xx=[a*b[0]+tx,a*b[2]+tx];yy=[d*b[1]+ty,d*b[3]+ty];v=(min(xx),min(yy),max(xx),max(yy))
  if v[0]>=box[0] and v[1]>=box[1] and v[2]<=box[2] and v[3]<=box[3]:out.append(copy.deepcopy(e));cnt+=1
 # Retain only referenced definitions.
 defs=out[0];ids={e.get('id'):e for e in defs};need=set();queue=list(out)[1:]
 while queue:
  for e in queue.pop().iter():
   for value in e.attrib.values():
    for key in re.findall(r'url\(#([^)]*)\)',value):
     if key not in need and key in ids:need.add(key);queue.append(ids[key])
 for e in list(defs):
  if e.get('id') not in need:defs.remove(e)
 E.ElementTree(out).write('assets/touch/thanit/reconstructed/'+name+'.svg',encoding='UTF-8',xml_declaration=True);print(name,cnt)
