## CacheDumper

This utility program reads Scrap Mechanic cache files (e.g. TCO texture cache files) and dumps their contents to the disk.  
This is useful in certain situations, e.g. if you accidentally perma-deleted a texture and need to recover it from the cache.

**Note - The dumper currently supports ONLY texture cache files (.TCO)!**  

**Note #2 - The dumper was created and tested with Scrap Mechanic version 0.7.3.776.**  
**Compatibility with cache files from other versions is NOT guaranteed.**

## How to use

To use the dumper, **place the `CacheDumper.exe` in the Cache directory of either the game or a mod.  
Example: `Scrap Mechanic/Cache/CacheDumper.exe` or `YourMod/Cache/CacheDumper.exe`.**  

Then simply execute it to automatically parse and dump all cached textures from `Cache/Textures/`.  
If the dumper is invoked from a CMD, process information and error messages (if any) will be shown.  

**The dumped textures are written to the `Cache/Textures_OUT/` directory, in TGA file format.**

## Compiling

The provided solution file can be used to compile the dumper from source code.
