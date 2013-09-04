# VMAN (Voxel MANager)
Version 1.0


## Introduction

A library which manages voxel volumes of arbitrary size.

Features:
- Portable across all POSIX complatible systems and Windows.
- Provides just a simple C API, which simplifies writing bindings for other languages.
- Is completly thread safe, so your game server should have no problems handly many requests at once.
- Tries not to waste resources like system memory or disk space.


### Chunks

VMAN divides the volume into chunks for easier management.
But this is invisible to the user. (Except if you take a look at the directory where they're stored.)
The API exposes no way to access them directly.
TODO: Explain why chunks are hidden to the user.


### Layers

Voxel can consist of multiple data layers.
Each layer can have an arbitrary size and custom serialization functions.
(E.g. if you need floating point values for performance reasons, but want to store the data as a single byte.)

Layers are optional: Voxels can store data in them, but don't have to.
In that case the layer defaults to zero at that position.
TODO: Explain the concept of layers better. => Complatiblity, versioning and custom data (For mods like in Minecraft or similar games)


## Usage and documentation

You'll find the documentation at http://henry4k.github.io/vman/1.0/index.html. 


## Downloads

Stable and nightly builds can be found here: http://vman.henry4k.de/builds
(Not yet. I'll setup the build service when I've got some time.)


## Compiling

- mkdir build
- cd build
- ccmake ..
- make
- sudo make install
:)


## Licence

Hurr durr WTPL all this shit!
O _ o ... not.


## Contact

...
- lol, no
