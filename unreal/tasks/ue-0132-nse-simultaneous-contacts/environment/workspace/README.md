# NightSkyEngine

Night Sky Engine is a free and open source fighting game framework made in Unreal Engine 5. It is designed to be powerful yet easy to learn, and can be used to make 2D and 2.5D fighting games.

This working copy targets Unreal Engine 5.8.2. The upstream framework originally targeted 5.7.

This branch is intended to target higher-end machines, such as consoles and high-end PCs. If you're looking for lower fidelity visuals, please use the `lowend` branch.

Documentation link: https://wistfulhopes.github.io/NightSkyEngine/

Discord link: https://discord.gg/mJTU9PV7jH

## Credits

The following additional plugins are used:

- GGPOUE4, originally by @BwdYeti, then forked by @erebuswolf, and finally modified for use in Night Sky Engine.
- CommonLoadingScreen, originally part of the official Lyra Sample Project by Epic Games.

Collision editor co-developed by [@MostExcellent](https://github.com/MostExcellent).

All sound effects are from [this site](http://osabisi.sakura.ne.jp/m2/).

## Jade Ascension arena

The existing TestMap stage now contains an original mystical cultivation arena with a clear
40 × 12 metre collision deck, layered pagodas and moon gate, blossom gardens, floating shrine
islands and a celestial sky. Authored and tested on Linux in Unreal Engine 5.8.2.

See [arena setup and reproduction](docs/JadeAscension.md), [asset provenance](docs/ASSETS.md),
and [verification results](docs/JadeAscension-validation.md). New geometry, lighting and
materials do not change the framework's fighting rules or add stage-transition mechanics.

### Astral Warden fighter

`BP_Manny` now uses an original masked armor costume on the existing Manny rig, with jade/cyan
and crimson/amber palettes. Body, shadow and costume material references are updated together.
The original moves, animation blueprint and gameplay collision remain in place.
See [design and reproduction](docs/AstralWarden.md), [asset provenance](docs/ASSETS.md),
and [validation](docs/AstralWarden-validation.md). Editable Blender source and FBX are under
`SourceArt/AstralWarden`. No new move set, facial rig or cloth simulation is included.
