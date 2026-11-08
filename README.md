Skifree SDL
=============================

A source port of the classic [Microsoft Entertainment Pack](https://en.wikipedia.org/wiki/Microsoft_Entertainment_Pack) game "skifree" to cross platform SDL2.

Forked from the decompiled skifree [skifree_decomp](https://github.com/yuv422/skifree_decomp) by Eric Fry

![Untitled](https://github.com/jeff-1amstudios/skifree_sdl/assets/1063652/95b53385-4d16-4de5-8f9d-10a877fee6a9)


## Dependencies
### Resources
You will need to place the original gfx and icon resources into `resources/`.
You can download the original version
from the [official Skifree website](https://ski.ihoc.net/) and extract the resources with a a PE resources extractor (e.g. [ResourcesExtract](https://www.nirsoft.net/utils/resources_extract.html))

Alternatively, you can fetch pre-extracted resources: 
```sh
curl -Lo /tmp/ski32_resources.zip https://archive.org/download/ski32_resources/ski32_resources.zip
unzip -d resources /tmp/ski32_resources.zip
```

### Libraries
- SDL2
- SDL2_image
- SDL2_ttf

## Compiling

This is a cmake project.

```sh
git clone https://github.com/jeff-1amstudios/skifree_sdl
cd skifree_sdl
# grab resources
curl -Lo /tmp/ski32_resources.zip https://archive.org/download/ski32_resources/ski32_resources.zip
unzip -d resources /tmp/ski32_resources.zip

mkdir build
cd build
cmake ..
```

Per-frame physics is enabled by default. To use the original 40 ms movement updates instead:

```sh
./build/skifree_sdl --no-per-frame-physics
```

On macOS, run the app bundle with the option using:

```sh
open build/skifree_sdl.app --args --no-per-frame-physics
```

## MacOS
On MacOS we build an app bundle `skifree_sdl.app`. Use right-click > Open the first time to get around [unverified developer warnings](https://support.apple.com/en-nz/guide/mac-help/mh40616/mac).

![Screenshot 2023-11-06 at 2 07 45 pm](https://github.com/jeff-1amstudios/skifree_sdl/assets/1063652/4edce399-ddeb-499a-a554-aebb7a70dfad)

## Todo
- Mouse support
- Sound maybe(?) - https://foone.wordpress.com/2017/06/20/uncovering-the-sounds-of-skifree/
