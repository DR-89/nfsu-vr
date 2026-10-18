from pathlib import Path
from lxml import etree as E
import copy
base=Path('assets/touch/thanit'); out=base/'reconstructed'; ns='http://www.w3.org/2000/svg'
for name in ('left','right','camera','brake','gas','nitro','pause'):
 r=E.fromstring(f'''<svg xmlns="{ns}" width="512" height="512" viewBox="0 0 512 512"><defs>
 <radialGradient id="shell-fill"><stop stop-color="#448b8e" stop-opacity=".70"/><stop offset=".72" stop-color="#23575b" stop-opacity=".76"/><stop offset="1" stop-color="#123c42" stop-opacity=".82"/></radialGradient>
 <linearGradient id="shell-light" x2=".8" y2="1"><stop stop-color="#e0eeee"/><stop offset="1" stop-color="#72aeac"/></linearGradient>
 <filter id="shell-glow" x="-50%" y="-50%" width="200%" height="200%"><feGaussianBlur stdDeviation="14"/></filter>
 </defs><circle cx="256" cy="256" r="194" fill="none" stroke="#27ffe3" stroke-width="17" opacity=".48" filter="url(#shell-glow)"/>
 <circle cx="256" cy="256" r="194" fill="url(#shell-fill)" stroke="#30ffe6" stroke-width="7"/>
 <circle cx="256" cy="256" r="178" fill="none" stroke="#b6fff1" stroke-opacity=".16" stroke-width="18"/>
 </svg>''')
 if name in ('left','right'):
  points = '286,170 206,256 286,342' if name == 'left' else '226,170 306,256 226,342'
  E.SubElement(r,'{'+ns+'}polyline',points=points,fill='none',stroke='url(#shell-light)',
   **{'stroke-width':'30','stroke-linecap':'round','stroke-linejoin':'round'})
 elif name=='gas':
  g=E.SubElement(r,'{'+ns+'}g')
  E.SubElement(g,'{'+ns+'}rect',x='207',y='120',width='98',height='272',rx='39',fill='url(#shell-light)')
  for y in (161,203,245,287,329):
   for x in (236,276):
    E.SubElement(g,'{'+ns+'}ellipse',cx=str(x),cy=str(y),rx='13',ry='15',fill='#368482',stroke='#9bcac4',**{'stroke-width':'4'})
 elif name=='pause':
  for x in (190,269): E.SubElement(r,'{'+ns+'}rect',x=str(x),y='174',width='53',height='164',rx='24',fill='url(#shell-light)')
 else:
  source=E.parse(str(base/(name+'.svg'))).getroot();source=copy.deepcopy(source)
  w,h={'left':(200,220),'right':(200,220),'brake':(250,170),'camera':(230,160),'nitro':(240,245)}[name]
  source.set('x',str((512-w)/2));source.set('y',str((512-h)/2));source.set('width',str(w));source.set('height',str(h));r.append(source)
 E.ElementTree(r).write(str(out/(name+'.svg')),xml_declaration=True,encoding='UTF-8')
