# Asset provenance

## Jade Ascension — 2026-09-15

- All thirteen new 3D meshes are original procedural artwork made in Blender 5.2.1.
  Generator, editable blend and FBXs: `SourceArt/JadeAscension/`.
- `T_CelestialVista.png` is original AI-generated backdrop art made with the built-in imagegen
  tool. The project-bound source is `SourceArt/JadeAscension/T_CelestialVista.png`; its imported
  Unreal texture is under `/Game/NightSkyEngine/Stages/JadeAscension/Textures`.
- Generation prompt: cinematic original xianxia cultivation realm at blue hour, wide 3:1
  panorama; layered distant karst peaks and floating islands over lavender/turquoise clouds,
  faint peach horizon, pale moon left of centre, jade aurora; painterly-realistic atmospheric
  detail; sky upper 65%, low-contrast lower centre; no foreground buildings, arena, people,
  words, logos or borders. Requested 3072 × 1024; delivered image is retained at its native 2172 × 724 size.
- All new materials are authored in Unreal with reproducible Python instructions. No downloaded
  game models, textures, icons, logos, Fab/marketplace assets or new third-party plugins.
- Visual references only: Street Fighter 6's Genbu Temple (readable foreground, shrine garden
  depth and blossoms) and Tekken's Sanctum / Genmaji Temple (large sacred landmarks, layered
  architecture, warm/cool lighting). No reference pixels or game geometry were imported.
  Reference pages: https://www.streetfighter.com/6/ and https://tekken.com/media;
  visual examples reviewed through public stage screenshots, including
  https://dashfight.com/news/the-top-5-stages-in-tekken-8-5323 and
  https://www.famitsu.com/article/202405/5662 .

Mesh dimensions, masks, safety envelope and reproduction are documented in `JadeAscension.md`.
Existing NightSkyEngine content and its upstream licenses remain unchanged.

## Astral Warden — 2026-09-15

- New mask, cowl, segmented armor, geometric trim, power inlays, sash and coat tabs are original
  procedural Blender artwork. Editable source and FBX: `SourceArt/AstralWarden/`.
- Undersuit body topology and skinning are derived from the Epic Manny mannequin already
  present in this repository. Original skeleton and physics asset are retained. The existing
  Epic asset license applies to that base; it is not represented as CC0 or newly sculpted art.
- All seven new Unreal surface materials are authored procedurally. No external textures,
  purchased assets, marketplace plugins, game character meshes or reference pixels imported.
- Design references only: Street Fighter's bold readable silhouettes and Guilty Gear's mask,
  ceremonial clothing and mechanical detail contrasts. Images reviewed at
  https://www.fightersgeneration.com/characters5/nagoriyuki.html and
  https://www.fightersgeneration.com/news1/sf6-may24-4.htm . No reference assets copied.
