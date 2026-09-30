# Terv: natív D3D11 a játék D3D-hívásainál, Xenos-emuláció nélkül

*2026-09-30. Állapot: F0 (feltérképezés) folyamatban, F1 (megfigyelő horgok) elkezdve.*

## Mi a cél

Ma a játék a saját, beépített Xbox 360 D3D-könyvtárával GPU-parancsokat
(PM4-csomagokat) ír egy gyűrűbe. A mi `gpu.cpp`-nk ezeket dekódolja, egy
Xenos-regiszterfájlba írja, és a `render_*` réteg ebből a regiszterállapotból
rajzol D3D11-gyel. Ez működik, de a Xenos GPU viselkedését utánozza, és a
nehezen megtalálható hibák (predikáció, csak-mélység mód, maradék shader,
EDRAM, resolve, kerítések, VBlank-jelző, erőforrás-ujjlenyomatok) mind ebből
a rétegből jöttek.

A cél: a játék **D3D-függvényeinek hívását** átvenni (a `title_fixes.cpp`
mintájára, a lefordított függvények felülírásával), és ott, a hívás
pillanatában, közvetlenül D3D11-et hívni. A PM4-parancsok meg sem születnek,
így nincs mit emulálni.

## Mi szűnik meg

| Ma (emuláció) | A D3D-szinten |
| --- | --- |
| PM4-dekódolás, két dekódoló (`gpu.cpp`) | nincs parancsfolyam |
| Xenos-regiszterfájl, `RenderState::Snapshot` | a hívás paraméterei és a D3D-eszköz saját állapota |
| predikáció, bin mask/select | a hívás maga mondja meg, melyik menet |
| EDRAM-modell, felületek sorokban, resolve-trükkök | valódi D3D11 render targetek, `CopyResource`/`ResolveSubresource` |
| „maradék shader", csak-mélység mód | a `SetPixelShader(NULL)` natív null-shader |
| kerítések, `WAIT_REG_MEM`, swap-jelző, VBlank-ütem | natív `Present`, natív képütemezés |
| textúra- és pufferujjlenyomatok (mintavételezés, képkockán belüli változás) | `Lock`/`Unlock` pontosan megmondja, mi változott és mikor |

## Ami marad, mert nem lehet másként

- **A shaderek fordítása.** A játék shaderei lefordított Xenos-mikrokódként
  vannak a lemezen; forrásuk nincs. A mikrokód → HLSL fordító
  (`xenos_hlsl.cpp`) megmarad, de a shader **létrehozásakor** fut, egyszer,
  nem a parancsfolyamból kihalászva. Ez fordítás, nem emuláció: a kimenet
  natív D3D11-shader.
- **A textúrák átalakítása.** A lemezen lévő textúrák Xbox-formátumúak
  (csempézett, big-endian). Ezeket betöltéskor (`Unlock`-kor) egyszer
  alakítjuk át natív DXGI-formátumra.

## Amit a felmérés már megmutatott

- A játékba épített D3D a **0x822E0000–0x82300000** tartományban van:
  **541 függvény**, kb. 32 000 utasítás.
- A játék ebből **182 függvényt hív közvetlenül** – ez az átveendő felület.
  Sok közülük egyszerű állapotbeállító.
- A rajzolás kevés ponton megy át:
  - `sub_822F3620` (DRAW_INDX, 5 hívóhely), `sub_822F3EA0` és
    `sub_822F4338` (DRAW_INDX, egy-egy hívóhely a játék
    renderelő-hátterében, 0x821410A0/0x82141108);
  - `sub_822F6858` (DRAW_INDX_2, csak a könyvtáron belülről);
  - `sub_822EFFF0` (DRAW_INDX_2 + EVENT_WRITE, 31 hívóhely: valószínűleg
    törlés vagy resolve);
  - `sub_822FB4B0` (IM_LOAD: a shaderek betöltése);
  - `sub_822F6430` (EVENT_WRITE_SHD: kerítés);
  - a predikáció beállítói (SET_BIN_MASK): 11 függvény, pl. `sub_822E9658`.
- Az eszköz a 0x82001144-en lévő mutatón át érhető el; ismert mezők:
  +10772 (a swap-jelzőt is tartalmazó leíró), +15120..15136
  (VBlank-visszahívás, számlálók, csere-visszaszámlálás).

## F0 eredménye: hogyan rajzol a játék (2026-09-30)

A megfigyelő horgok (`CoD3Host/d3d_hooks.cpp`, `COD3_D3DHOOKS=1`) és a
`pool_trace.cpp` mérései szerint, Saint-Lô elején:

- **Négy munkaszál rögzít**, párhuzamosan, nagyjából egyenlő arányban. Mind
  a négy ugyanabból a belépési pontból fut (`sub_8212A520` →
  `sub_8212A780`), a renderelő-hátteret (`sub_8214F770` → … →
  `sub_82140A90`) hajtják.
- **Egy ötödik szál cserél** (`sub_822F4A10`, képkockánként egyszer), egy
  hatodik, a D3D saját **visszajátszó szála** (`sub_82302A90`) írja a
  rögzített puffereket a gyűrűbe (`sub_822F1E68`).
- Képkockánként: 4 rögzítés (`sub_82302DB0` kezdi, `sub_82302E88`
  zárja), 14 beküldés (`sub_822F2818`), 28 puffer a gyűrűbe.
- **A rajzolások szinte mind D3D-függvényen mennek át:** a fő rajzoló a
  `sub_822F3A28` (~890 hívás képkockánként), mellette `sub_822F30F0`
  (~48), `sub_822F3620` (~45), `sub_822F6858`/`sub_822F6DF8` (~8), a
  `sub_822EFFF0` (~20, téglalap + esemény). Ez együtt kb. 1000, ami
  egyezik a renderelő által látott 1067 rajzolással (a különbség a
  mélység- és színmenet visszajátszása). A játék saját, D3D-t megkerülő
  rajzolója (`sub_82154930`) öt másodpercenként egyszer fut.
- Rajzolásonként átlagosan két shaderbetöltés (`sub_822FB4B0`) történik.

**Következmény a felépítésre:** a rajzolás a D3D-hívásoknál átvehető, de
nem lehet a hívás pillanatában D3D11-gyel rajzolni, mert négy szál rögzít
egyszerre, és a sorrendet a visszajátszó szál adja meg. Ezért:

- minden munkaszál a saját **natív parancslistájába** rögzít (a mai
  `DrawCommand` mintájára, de regiszterek nélkül: shaderek,
  állapotobjektumok, nézetek, és a rajzoláshoz tartozó adatok másolata);
- a rögzítés kezdete/vége (`sub_82302DB0`/`sub_82302E88`) a lista
  kezdete/vége; a lista a vendég parancspufferének címéhez kötődik;
- amikor a visszajátszó szál egy puffert a gyűrűbe írna, a hozzá tartozó
  natív listát a végrehajtó (egyetlen D3D11-szál) lefuttatja, a
  mélység/szín menetet a rögzített menetjelölés szerint szűrve;
- a mai végrehajtó (`render_d3d11.cpp`) megmarad, csak regiszterek helyett
  natív listát kap.

## Felépítés

1. **Horog-réteg** (`d3d_hooks.cpp`): a könyvtár API-függvényeinek
   felülírása (`PPC_FUNC(sub_...)`), ahogy a `title_fixes.cpp` teszi. Minden
   horog kiolvassa a paramétereket a vendég regisztereiből, és a natív
   rétegnek adja; az eredeti függvény nem fut le (vagy csak az
   állapotkönyvelése, ha a játék visszaolvassa).
2. **Natív eszköz** (`native_d3d11.cpp`): a mostani `render_d3d11.cpp`
   végrehajtójából kiindulva, de Xenos-fogalmak nélkül: D3D11-erőforrások,
   állapotobjektumok, rajzolás.
3. **Erőforrás-leképezés**: a játék D3D-objektumai (textúra, felület,
   puffer, shader) a vendégmemóriában élnek, fejlécük a fetch-konstans.
   Mindegyikhez a vendégbeli címe szerint egy natív objektum tartozik,
   létrehozáskor elkészítve, `Unlock`-kor frissítve, felszabadításkor
   eldobva.
4. **Parancsrögzítés**: ha a játék előre rögzített parancspuffert játszik
   vissza (a mélység-előmenet és a színmenet ugyanabból a pufferből,
   predikációval), a horog-réteg natív parancslistát rögzít, és a
   visszajátszáskor a megfelelő menet rajzolásait futtatja.

## Fázisok

Minden fázis végén a játék játszható, és a régi út egy kapcsolóval
(`COD3_NATIVE=0`) visszakapcsolható, amíg a natív út mindent nem tud.

### F0 – Feltérképezés
- A 182 hívott API-függvény azonosítása és elnevezése (mint a
  `builtins.py`-nál a pálya-beépítetteknél): viselkedés, paraméterek, mely
  eszközmezőket írják.
- Az eszköz-struktúra (render state-ek, textúrák, stream source-ok,
  konstansok, célok) leírása.
- Kérdések: melyik szálról hív a játék D3D-t; használ-e rögzített
  parancspuffert (`RunCommandBuffer`); ír-e GPU-parancsot a D3D-n kívül
  (a filmlejátszó?).
- **Kimenet:** `docs/d3d-api.md`, a függvénytábla egy szkripttel
  generálva.

### F1 – Megfigyelő horgok
- A horgok csak naplóznak, és továbbhívják az eredetit.
- Képkockánként összevetjük a rajzolások számát és sorrendjét a PM4-úttal.
- **Kimenet:** biztos kép arról, hogy a horgok minden rajzolást látnak.

### F2 – Erőforrások
- Textúra-, puffer- és felület-létrehozás, `Lock`/`Unlock`, felszabadítás
  → natív D3D11-erőforrások.
- Az ujjlenyomatos gyorsítótár (`render_resources.cpp`) kiváltása.
- **Kimenet:** minden textúra és puffer a pontos pillanatban frissül.

### F3 – Shaderek
- A shader-létrehozó függvények → a mikrokód fordítása és a natív shader
  elkészítése egyszer, létrehozáskor.
- `SetVertexShader`/`SetPixelShader` → a natív shader bekötése;
  `NULL` → nincs pixelshader.

### F4 – Rajzolás
- A rajzolófüggvények (`sub_822F3620` és társai) → natív rajzolás az
  eszköz állapotából; a PM4-kibocsátás kihagyva.
- Állapotok: mélység, keverés, kivágás, raszterizáló, konstansok.
- Rajzolásonkénti visszakapcsolás hibakereséshez.

### F5 – Célok, törlés, resolve, csempézés, menetek
- `SetRenderTarget`/`SetDepthStencilSurface` → D3D11-célok.
- Törlés, resolve → natív törlés és másolás.
- A predikált csempézés és a mélység/szín menet → natív menetek, csempézés
  nélkül (PC-n nincs 10 MB-os EDRAM-korlát).

### F6 – Megjelenítés
- A csere (`Swap`/`Present`) → natív `Present`.
- A VBlank-, swap-jelző- és kerítés-emuláció kivezetése; natív
  képütemezés (VSync, korlátlan FPS).

### F7 – A régi út kivezetése
- Ha minden pálya, film és menü paritásban van, a PM4-dekódoló, a
  regiszterfájl és az EDRAM-modell törlése.

## Ellenőrzés

- **Képösszevetés:** minden fázisnál ugyanazon pálya ugyanazon pillanatáról
  a régi és a natív út képe (`scripts/compare_frames.py`).
- **Pályabejárás:** mind a 15 pálya betöltése és futtatása (a meglévő
  szkriptelt futásokkal, háttérben, külön mentésmappával).
- **Villogásmérés:** az egymást követő képkockák egy képkockás
  eltéréseinek számlálása (ezzel találtuk meg az árnyékhibát is).
- **Teljesítmény:** FPS és CPU-idő képkockánként.

## Kockázatok

- **Beágyazott (inline) állapotbeállítók:** az XDK D3D sok beállítót a
  játék kódjába fordít; ezek közvetlenül az eszköz-struktúrát írják. Ezeket
  nem kell horgolni: rajzoláskor az eszköz állapotát olvassuk.
- **Rögzített parancspufferek:** ha a játék parancspuffert rögzít és
  többször lejátszik, a natív rögzítést és visszajátszást meg kell írni.
- **Szálak:** a D3D11 közvetlen kontextus egy szálról használható; ha a
  játék több szálról hív D3D-t, sorba kell rendezni.
- **Memexport-shaderek, GPU-ról visszaolvasott adatok:** ha vannak, külön
  kezelést kívánnak.
- **Méret:** ez a projekt eddigi legnagyobb átalakítása. A fázisok úgy
  vannak felépítve, hogy közben mindig legyen működő, kiadható build.
