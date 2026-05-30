# ccleste

![screenshot](https://raw.githubusercontent.com/lemon-sherbet/ccleste/master/screenshot.png)

ccleste is a C source port of the [original celeste (Celeste classic)](https://www.lexaloffle.com/bbs/?tid=2145) for the PICO-8, designed to be portable. PC and 3DS are the main supported platforms, though other people are [maintaining ports to other platforms](https://github.com/lemon32767/ccleste/network/members).

This fork adds a Nintendo 64 port using the libdragon SDK. Go to [the releases tab](https://github.com/nadiaholmquist/ccleste/releases) for the latest pre-built binaries.

celeste.c + celeste.h is where the game code is, translated from the pico 8 lua code by hand.
These files don't depend on anything other than the c standard library and don't perform any allocations (it uses its own internal global state).

n64main.c provides the Nintendo 64 port. It can be compiled by running

```
make -f Makefile.n64
```

You will need to have the libdragon SDK installed and `N64_INST` set to point to its installation directory to build.

# Controls

|Nintendo 64        |Action              |
|:-----------------:|-------------------:|
|LEFT               | Move left          |
|RIGHT              | Move right         |
|DOWN               | Look down          |
|UP                 | Look up            |
|A                  | Jump               |
|B                  | Dash               |
|START              | Pause/Options      |

# credits

Sound wave files are taken from [https://github.com/JeffRuLz/Celeste-Classic-GBA/tree/master/maxmod_data](https://github.com/JeffRuLz/Celeste-Classic-GBA/tree/master/maxmod_data),
music ogg files were obtained by converting the .wav dumps from pico 8, which I did using audacity & ffmpeg.

For the N64 port, slightly modified (to trim unused channels) versions of the XM files from the above GBA port were used.

All credit for the original game goes to the original developers (Maddy Thorson & Noel Berry).
