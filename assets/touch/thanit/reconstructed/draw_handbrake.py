"""Generate classic mechanical handbrake artwork. Run from repository root."""
from pathlib import Path
from lxml import etree as E

folder = Path('assets/touch/thanit/reconstructed')
namespace = 'http://www.w3.org/2000/svg'
root = E.parse(str(folder / 'pause.svg')).getroot()
# Shared shell: definitions, glow, circular body and inner rim.
for element in list(root)[4:]:
    root.remove(element)
root.append(E.fromstring(f'''<g xmlns="{namespace}" stroke-linecap="round" stroke-linejoin="round">
  <!-- Low console mounting and pivot cover. -->
  <path d="M 146 337 Q 151 319 172 314 L 254 314 Q 279 317 293 336 L 306 356 L 140 356 Z" fill="#70aaa7"/>
  <path d="M 158 338 H 283" stroke="#c4e4dd" stroke-width="6" opacity=".7"/>
  <!-- Raised mechanical arm, widening towards its pivot. -->
  <path d="M 181 326 Q 186 303 210 287 L 273 242 L 287 259 L 231 310 Q 221 320 219 331 Z" fill="url(#shell-light)"/>
  <path d="M 204 302 L 269 254" stroke="#e4f6f0" stroke-width="5" opacity=".65"/>
  <!-- Long moulded handle, with the release button on the free end. -->
  <g transform="rotate(-32 279 247)">
    <path d="M 240 230 Q 246 224 258 224 H 357 Q 372 224 375 236 V 251 Q 372 263 357 263 H 258 Q 247 263 240 257 Z" fill="url(#shell-light)"/>
    <path d="M 256 230 H 356" stroke="#eef9f4" stroke-width="5" opacity=".8"/>
    <path d="M 267 248 H 345" stroke="#519693" stroke-width="6" opacity=".65"/>
    <rect x="373" y="234" width="17" height="18" rx="5" fill="#e5f6ef"/>
    <path d="M 366 233 V 253" stroke="#599e98" stroke-width="4"/>
  </g>
  <path d="M 169 323 Q 179 306 195 308 Q 215 309 224 333 L 227 342 H 163 Z" fill="#88bfb7"/>
  <path d="M 178 322 Q 188 314 198 321" stroke="#d6eee6" stroke-width="5" fill="none"/>
</g>'''))
E.ElementTree(root).write(str(folder / 'handbrake.svg'), encoding='UTF-8', xml_declaration=True)

# Separate symbol for reuse without the circular shell.
import copy
symbol = E.Element(root.tag, nsmap={None: namespace}, width='256', height='256',
                   viewBox='120 160 285 215')
symbol.append(copy.deepcopy(root[0]))
symbol.append(copy.deepcopy(root[-1]))
E.ElementTree(symbol).write(str(folder / 'handbrake-symbol.svg'), encoding='UTF-8', xml_declaration=True)
