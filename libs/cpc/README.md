# Sequential CPC Sketch

- `fm85.h`: FM85 sketch interface:. See implementation details in [`fm85.c`](fm85.c).

- `u32Table.h`: interface of the specialized hash table designed for FM85. It is used, for example, to store "surprising values". See implementation details in [`u32Table.c`](u32Table.c)

- `mycity.h`: contains an implementation of CityHash64(), specifically tailored for this kind of sketch. The more convenient `xxhash.h` is used in this implementation (see below).

- `xxhash.h`: non-cryptographic hash functions. Convenient interface for splitting 128 hash values into two 64 bit chunks required by this FM85 implementation.

- `icon.h`: implementations of the ICON estimator.

- `fm85Merging.h`: 

# Origin and modification
This code comes from https://github.com/kevinjlang/cpc and is licensed as Apache-2.0. At the moment our modification consists in adding comments while studying the implementation and the removal of some tests files not needed in our context.
