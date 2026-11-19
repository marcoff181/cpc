# Sequential CPC Sketch

- `fm85.h`: FM85 sketch interface:. See implementation details in [`fm85.c`](fm85.c).

- `u32Table.h`: interface of the specialized hash table designed for FM85. It is used, for example, to store "surprising values". See implementation details in [`u32Table.c`](u32Table.c)

- `mycity.h`: contains an implementation of CityHash64(), specifically tailored for this kind of sketch. The more convenient `xxhash.h` is used in this implementation (see below).

- `xxhash.h`: non-cryptographic hash functions. Convenient interface for splitting 128 hash values into two 64 bit chunks required by this FM85 implementation.

- `icon.h`: implementations of the ICON estimator.

- `fm85Merging.h`: 