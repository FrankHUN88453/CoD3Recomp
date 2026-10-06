# Terv: natív D3D11 a játék D3D-hívásainál, Xenos-emuláció nélkül

*2026-09-30, frissítve 2026-10-06. Állapot: F0 és F1 kész; a D3D könyvtár visszafejtve (privát CoD3Decomp); N1 (a rajzolás állapota a rajzhíváskor, paritással) kész, következik N2.*

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

## N1 eredménye: a rajzolás állapota a rajzhíváskor (2026-10-06)

A dekompilált D3D könyvtárból ismert az eszköz szerkezete: minden regiszter
árnyékmásolata, a piszkos maszkok, a konstansok és a shader-objektumok
helye (`CoD3Host/native_state.cpp` fejléce). Az első kérdés az volt, hogy
egy rajzolás teljes állapota megvan-e már a rajzhívás pillanatában, vagy
csak a GPU regiszterfájljából, a végrehajtáskor rakható össze.

**Amit a mérés megmutatott** (`COD3_NATIVECHECK=1`):

- Az árnyékok önmagukban **nem elegendőek**. A játék saját renderelője az
  anyagok állapotát előre összerakott csomagblokkként maga másolja a
  parancspufferbe (`sub_821563A8` és társai, a helyet `sub_82156B38` kéri),
  és törli a megfelelő piszkos biteket; az árnyékok ilyenkor régi értéket
  tartanak. A modellmátrixok SET_CONSTANT-tal mennek, a D3D által adott, a
  hívó által kitöltött helyre (`sub_822F8D38`); más konstansok memóriából
  (LOAD_ALU_CONSTANT).
- A csempés rögzítésnél a Z-menet saját módot, programvezérlést és
  csak-pozíciós vertex-programot kap, predikáltan.

**A megoldás:** eszközönként a saját parancsfolyam modellje, sorrendben
olvasva: minden rajzhívásnál a rajzolásig, minden szegmensváltásnál
(`sub_822F2818` kick, `sub_822F2678` új szegmens) a szegmens végéig. Ez a
modell tudja minden regiszter utolsó értékét, float4 konstansonként, hogy
memóriából jön-e, és menetenként a programokat és a predikált írásokat.
Minden rajzcsomaghoz rekord készül (a csomag utolsó szava a kulcs), és a
`gpu.cpp` a végrehajtáskor összeveti a regiszterfájllal.

**Eredmény (Saint-Lô, 75 s):** az indulás első 25 rajzolását kivéve
minden végrehajtott rajzolásnak van rekordja, és mind a ~2400 regiszterszó
(állapotblokkok, fetch-, float-, bool- és loop-konstansok), valamint a
vertex- és a pixelprogram **mindkét menetben egyezik**: kb. 390 000
rajzolás 5 másodpercenként, **0 eltérés**. Más pufferből nem szivárog át
állapot; a rögzített folyam önmagában teljes.

**Ami ebből a tervre következik:**

- A játék saját anyagblokkjai PM4-csomagok, ezért egy szűk
  csomagolvasó (regiszterírások, SET_CONSTANT, LOAD_ALU_CONSTANT, IM_LOAD,
  bin mask) a rögzítéskor megmarad – ahogy a shaderfordító is. Ez nem
  GPU-emuláció: nincs benne végrehajtás, kerítés, várakozás, gyűrű.
- **N2:** a renderelő a rekordból rajzol (`COD3_NATIVE=1`), nem a
  regiszterfájlból; a kép nem változhat (a paritás miatt).
- **N3:** a rekordok sorrendje natív listákban (szegmensenként, a
  visszajátszásnál menetenként), a végrehajtó ezeket futtatja, a PM4-út a
  rajzolásokhoz már nem kell.

## N2 és N3 eredménye: rajzolás a rekordokból, natív listák (2026-10-07)

**N2** (`COD3_NATIVE=1`): a renderelő minden rajzolásnál a rekordot
használja (`NativeState::Begin`): a regisztereket, a konstansokat és a
programokat onnan, nem a regiszterfájlból. A kép ugyanaz. A PM4-értelmezés
közben teljesen lefut, ezért ez önmagában lassabb az alapnál (erdő,
1080p, MSAA nélkül: 69 fps az alap 91-gyel szemben); átmeneti lépcső.

**N3** (`COD3_NATIVE=2`): a rögzítéskor beolvasott minden csomagról
feljegyzés készül (a fejléce, és hogy a parancsfeldolgozónak még futtatnia
kell-e). Nem kell futtatni azt, ami a rekordokban már benne van: a rajzolás
regisztereibe eső írásokat, a konstansokat (SET_CONSTANT,
LOAD_ALU_CONSTANT), a programbetöltéseket és a kitöltő csomagokat. Ami
marad: a rajzolások (a rekordjukból), a várakozások, események,
megszakítások, swapok, bin maszkok, és a rajzolás állapotán kívüli
regiszterírások. A `gpu.cpp` egy indirekt puffert csomagról csomagra
ellenőrizve futtat ebből: amíg a csomagok sorban, változatlanul követik
egymást, és minden rajzolásnak van rekordja, csak a futtatandókat; a
maradékot (a kick a beolvasás után még ír egy zárórekordot) a régi úton,
ha abban nincs rajzolás; különben az egész puffert a régi úton.

Két dolog kellett a sebességhez:

- a csomagfeljegyzések lapos tömbökben vannak (64 KB-os lapok fizikai cím
  szerint), nem fában: a fa másodpercenként milliónyi beszúrással a
  játékot 37 fps-re lassította;
- a rekord nem teljes 9,6 KB-os másolat, hanem 64 szavas darabokból áll:
  egy rajzolás csak a megváltozott darabokat másolja egy 32 MB-os
  gyűrűbe, a többin osztozik az előzőkkel; a renderelő is csak a
  megváltozott darabokat írja át a képébe. A paritás így is teljes
  (0 eltérés ~358 000 rajzolásban 5 másodpercenként).

**Eredmény (erdő, 1080p, MSAA nélkül, vsync ki):** alap 91 fps, N3
95–118 fps; a parancsfeldolgozó a csomagok ~15%-át futtatja, a többit a
rekordok adják. A kép az alapéval azonos. Vigyázat a méréskor: a
beállításokban `vsync = 1`, és 120 Hz-es kijelzőn a 8,3 ms-nál kicsit
hosszabb képkockák 60 fps-re kvantálódnak – összehasonlításhoz
`COD3_VSYNC=0`.

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
