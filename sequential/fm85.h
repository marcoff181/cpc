/**
 * T19:
 * 
 * The FM85 sketch data structure is logically represented as a bit matrix
 * of K = 2^lgK rows by 64 columns in this specific implementation+.
 * 
 *           0   1         62  63
 *         ---------     ---------
 *    0    | 0 | 0 | ... | 0 | 0 |
 *         ---------     ---------
 *         .         .
 *         .          .
 *         .           .
 *         ---------     ---------
 *  K - 1  | 0 | 0 | ... | 0 | 0 |
 *         ---------     ---------
 * 
 * ...
 * 
 * + The number of columns is related to how coupons are distributed by the
 * hash function and how we choose to interpret the hash values of incoming
 * objects. This implementation sets a coupon in column x where x is the number
 * of consecutive 0s in the hash value of an object; it uses 64 columns because
 * hashes may contain up to 64 consecutive bits set to 0.
 */


// Copyright 2018, Kevin Lang, Oath Research

#ifndef GOT_FM85_H

#include "common.h"
#include "u32Table.h"

// Note: except for brief transitional moments, these sketches always obey
// the following strict mapping between the flavor of a sketch and the
// number of coupons that it has collected.

/**
 * EMPTY:    no coupons
 * SPARSE:   (row, col) pairs stored inside a hash table
 * HYBRID:   allocate window, coupons with large cols are stored separately as surprising values
 * PINNED:   same as HYBRID at runtime, but the window is compressed separately
 * SLIDING:  mixed surprising values (before and after the window), same compression as PINNED
 */
typedef enum flavorType {
    EMPTY,   //          C = 0
    SPARSE,  //     1 <= C < 3K/32
    HYBRID,  // 3K/32 <= C < K/2
    PINNED,  //   K/2 <= C < 27K/8  [NB: 27/8 = 3 + 3/8]
    SLIDING  // 27K/8 <= C
} SketchFlavor;

/*******************************************************/

typedef struct fm85_sketch_type
{
    // The following variables occur in all sketch types.
    Short lgK;
    // T19: New updates can only occur on uncompressed sketches.
    Boolean isCompressed;
    // T19: Once a sketch is merged, the HIP estimator does not work
    // anymore. Use this flag to prevent HIP requests.
    Boolean mergeFlag; // Is the sketch the result of merging?

    Long numCoupons; // The number of coupons collected so far.

    // The following variables occur in the updateable semi-compressed type.
    U8 * slidingWindow;
    Short windowOffset; // Derivable from numCoupons, but made explicit for speed.
    u32Table * surprisingValueTable;

    // The following variables occur in the non-updateable fully-compressed type.
    U32 * compressedWindow; // A bitstream.
    Long  cwLength; // The number of 32-bit words in this bitstream. (Not needed in Java).
    Long  numCompressedSurprisingValues;
    U32 * compressedSurprisingValues; // A bitstream.
    Long  csvLength; // The number of 32-bit words in this bitstream. (Not needed in Java).

    // Note that (as an optimization) the two bitstreams could be concatenated.

    // T19: This attribute keeps track of the first column of the sketch
    // that contains a 0 (from left to right). It allows to skip updates
    // where a new coupon spawns in a region filled with 1s.
    Short firstInterestingColumn; // This is part of a speed optimization.

    // T19: Useful variables for HIP estimator.
    double kxp;
    double hipEstAccum;
    double hipErrAccum;

} FM85;

/*******************************************************/
// These routines are exported.

// T19: Basic FM85 utilities

/**
 * T19: prepare useful structures which are shared among
 * all sketches that are going to be instantiated with
 * fm85Make()
 */
void fm85Init(void); // Call this before anything else.

FM85 * fm85Make(Short lgK);
FM85 * fm85Copy(FM85 * self);
void fm85Free(FM85 * sketch);

/**
 * Update the state of an FM85 object using two hash values,
 * hash0 and hash1. It calculates a row and column index
 * from the hashes and then applies the update
 * to the corresponding row and column in the FM85 structure.
 */
void fm85Update(FM85 * sketch, U64 hash0, U64 hash1);

/**
 * WARNING: HIP depends on the history of updates, and
 * calling that estimator on a merged sketch causes the
 * program to crash. Call ICON instead.
 */
double getHIPEstimate(FM85 * sketch);


// T19: See icon.h and icon.c
// getIconEstimate() is defined in a separate file.

/*******************************************************/

// These routines are internal.

/**
 * Update the internal state of an FM85 structure based on the
 * provided rowCol value, which represents a column extracted from
 * the least significant bits and a row from the remaining ones.
 * It performs optimizations by skipping updates for columns that
 * are already filled with 1s, checks for compression constraints,
 * and decides between sparse or windowed updates based on the
 * number of coupons and the current configuration of the FM85
 * instance.
 */
void fm85RowColUpdate(FM85 * sketch, U32 rowCol);

SketchFlavor determineFlavor(Short lgK, Long c);

/**
 * Determine the flavor of the given FM85 structure. It calls the determineFlavor
 * function, passing the lgK and numCoupons members of the FM85 structure
 * to determine the appropriate flavor.
 */
SketchFlavor determineSketchFlavor(FM85 * self);

/**
 * The determineCorrectOffset function calculates the offset of
 * the CPC window from scratch, based on lgK and c only.
 */
Short determineCorrectOffset(Short lgK, Long c);

/**
 * Generate a full, explicit bit matrix for a given FM85 structure, ensuring that
 * the structure is not compressed and initializing the matrix with default values.
 * It populates the matrix based on the sliding window and updates specific bits
 * according to the values in the surprising value table, returning the
 * constructed matrix.
 * 
 * Useful for sliding the window and for merging two sketches.
 */
U64 * bitMatrixOfSketch(FM85 * self);

// these are only used internally
// void promoteEmptyToSparse(FM85 * self);
// void promoteSparseToWindowed(FM85 * self);
// void modifyOffset(FM85 * self, Short newOffset);
// void updateSparse(FM85 * self, U32 rowCol);
// void updateWindowed(FM85 * self, U32 rowCol);

/*******************************************************/

#define GOT_FM85_H
#endif
