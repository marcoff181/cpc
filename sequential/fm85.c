// Copyright 2018, Kevin Lang, Oath Research

#include "common.h"
#include "u32Table.h"
#include "fm85.h"
#include "fm85Util.h"
#include "fm85Compression.h"


/*******************************************************/
/* HIP code ********************************************/

// Call this whenever a new coupon has been collected.

void updateHIP(FM85 * self, Short rowCol) {
    Long k = (1LL << self->lgK);
    Short col = (Short) (rowCol & 63);  
    double oneOverP = ((double) k) / self->kxp;
    self->hipEstAccum += oneOverP;
    self->hipErrAccum += ((oneOverP * oneOverP) - oneOverP);
    self->kxp -= invPow2Tab[col + 1]; // notice the "+1"
}

/*******************************************************/

double getHIPEstimate (FM85 * self) {
    if (self->mergeFlag != 0) {
        FATAL_ERROR("tried to get HIP estimate of merged sketch");
    }
    return self->hipEstAccum;
}

/*******************************************************/

// The KXP register is a double with roughly 50 bits of precision, but
// it might need roughly 90 bits to track the value with perfect accuracy.
// Therefore we recalculate KXP occasionally from the sketch's full bitmatrix
// so that it will reflect changes that were previously outside the mantissa.

void refreshKXP (FM85 * self, U64 * bitMatrix) {
  Long k = (1LL << self->lgK);
  Long i;
  Short j;

 // for improved numerical accuracy, we separately sum the bytes of the U64's
  double byteSums [8]; // allocating on the stack

  for (j = 0; j < 8; j++) { byteSums[j] = 0.0; }

  for (i = 0; i < k; i++) {
    U64 word = bitMatrix[i];
    for (j = 0; j < 8; j++) { 
      U8 byte = word & 0xff;
      byteSums[j] += kxpByteLookup[byte];
      word >>= 8;
    }
  }

  double total = 0.0;
  for (j = 7; j >= 0; j--) { // the reverse order is important
    double factor = invPow2Tab[8*j]; // pow (256.0, (-1.0 * ((double) j)));
    total += factor * byteSums[j];
  }

  //  fprintf (stderr, "%.3f\n", ((double) self->numCoupons) / k);
  //  fprintf (stderr, "%.19g\told value of KXP\n", self->kxp);
  //  fprintf (stderr, "%.19g\tnew value of KXP\n", total);
  //  fflush (stderr);

  self->kxp = total;

}

/* HIP code ********************************************/
/*******************************************************/

// The following is used during testing, and is basically package private.

/**
 * Take two 64-bit hash values and a logarithmic value lgK
 * to compute a 32-bit integer representing a (row, col) tuple,
 * where the row is derived from the lgK least significant bits
 * of the first hash and the column is determined by the number
 * of leading zeros in the second hash, ensuring that the column
 * value is clipped to a maximum of 63. The function also includes
 * a safeguard against returning a specific "empty" value
 * by modifying the row if necessary.
 */
static U32 rowColFromTwoHashes(U64 hash0, U64 hash1, Short lgK) {
    // T19: The current implementation does not support values
    // of lgK larger than 26 because rowCol is only 32 bits long
    assert(lgK <= 26);

    Long k = (1LL << lgK);

    Short col = countLeadingZerosInUnsignedLong(hash1); // 0 <= col <= 64
    if (col > 63) col = 63; // clip so that 0 <= col <= 63

    // T19: Since k = 2^lgK, k - 1 is a sequence of lgK consecutive 1s.
    // Thus, the following line extracts the k least significant
    // bits of hash0, which select the coupon row.
    Long row = hash0 & (k - 1);

    // T19: Merge row and col together in a single 32 bit variable.
    // Since the logic bitmap of the sketch is 64 columns long,
    // 6 bits are enough to represent columns int the range [0, 63].
    // This is why icon.c doesn't include high values of lgK by
    // default, because in the current FM85 implementation there
    // are only 26 bits left for representing the row.
    U32 rowCol = (U32) ((row << 6) | col);

    // To avoid the hash table's "empty" value, we change the row of the following pair.
    // This case is extremely unlikely, but we might as well handle it.
    if (rowCol == ALL32BITS) {
        rowCol ^= (1 << 6);
    } 
    return rowCol;
}

/*******************************************************/

Boolean fm85Initialized = 0;

// This stuff would be handled by Java's mechanism
// for initializing class variables.

void fm85Init(void) {
    if (!fm85Initialized) {
        fm85Initialized = 1;

        // T19: See fm85Compression.c
        makeTheDecodingTables();

        // T19: For these 4 calls, see fm85Util.c
        fillByteLeadingZerosTable();
        fillByteTrailingZerosTable();
        // T19: Do not change the order of the following calls!!!
        fillInvPow2Tab();
        fillKxpByteLookup();
    }
}

/*******************************************************/
// The flavor is a function of K and C (the number of collected coupons).

SketchFlavor determineFlavor(Short lgK, Long c) {
    Long k = (1LL << lgK);
    Long c2 = c << 1;
    Long c8 = c << 3;
    Long c32 = c << 5;
    if (c == 0)      return EMPTY;    //      0 = C < 1
    if (c32 < 3 * k) return SPARSE;   //    1  <= C < 3K/32
    if (c2 < k)      return HYBRID;   // 3K/32 <= C < K/2
    if (c8 < 27 * k) return PINNED;   //   K/2 <= C < 27K/8
    else             return SLIDING;  // 27K/8 <= C
}

// Note: the <= occurs with equality except SPARSE-vs-HYBRID for K = 2^4

/**
 * Determine the flavor of the given FM85 structure. It calls the determineFlavor
 * function, passing the lgK and numCoupons members of the FM85 structure
 * to determine the appropriate flavor.
 */
SketchFlavor determineSketchFlavor(FM85 * self) {
    return determineFlavor(self->lgK, self->numCoupons);
}

/*******************************************************/

// T19: When the sketch is in a "windowed state" (i.e. all
// flavors except EMPTY and SPARSE), the offset of the window can
// be deduced from lgK and c. The window does not slide
// until C >= 27K/8, that is, the offset is 0; when this
// threshold is trepassed, the offset increases by 1 every
// time C increases by K.

/**
 * The determineCorrectOffset function calculates the offset of
 * the CPC window from scratch, based on lgK and c only.
 */
Short determineCorrectOffset(Short lgK, Long c) {
    Long k = (1LL << lgK);
    Long tmp = (c << 3) - 19 * k;        // 8C - 19K
    if (tmp < 0) return ((Short) 0);
    return ((Short) (tmp >> (lgK + 3))); // tmp / 8K
}

/*******************************************************/

FM85 * fm85Make(Short lgK) {
    assert(lgK >= 4 && lgK <= 26);

    FM85 * self = (FM85 *) malloc(sizeof(FM85));
    assert(self != NULL);

    self->lgK = lgK;
    self->isCompressed = 0;
    self->mergeFlag = 0;

    self->numCoupons = 0LL;

    self->windowOffset = 0;
    self->slidingWindow = (U8 *) NULL;
    self->surprisingValueTable = (u32Table *) NULL;

    self->numCompressedSurprisingValues = 0;
    self->compressedSurprisingValues = (U32 *) NULL;
    self->csvLength = 0;
    self->compressedWindow = (U32 *) NULL;
    self->cwLength = 0;

    self->firstInterestingColumn = 0;

    Long k = (1LL << self->lgK);

    self->kxp = ((double) k) * 1.0;
    self->hipEstAccum = 0.0;
    self->hipErrAccum = 0.0;

    return self;
}

/*******************************************************/

FM85 * fm85Copy (FM85 * self) {
  assert (self != NULL);
  FM85 * newObj = (FM85 *) shallowCopy ((void *) self, sizeof(FM85));

  if (self->surprisingValueTable != NULL) {
    newObj->surprisingValueTable = u32TableCopy (self->surprisingValueTable);
  }
  if (self->slidingWindow != NULL) {
    Long k = (1LL << self->lgK);
    size_t theSize = k * sizeof(U8);
    newObj->slidingWindow = (U8 *) shallowCopy ((void *) self->slidingWindow, theSize);
  }
  if (self->compressedSurprisingValues != NULL) {
    size_t theSize = self->csvLength * sizeof(U32);
    newObj->compressedSurprisingValues = (U32 *) shallowCopy ((void *) self->compressedSurprisingValues, theSize);
  }
  if (self->compressedWindow != NULL) {
    size_t theSize = self->cwLength * sizeof(U32);
    newObj->compressedWindow = (U32 *) shallowCopy ((void *) self->compressedWindow, theSize);
  }

  return (newObj);
}

/*******************************************************/

void fm85Free(FM85 * self) {
    if (self != NULL) {
        if (self->surprisingValueTable != NULL)       u32TableFree(self->surprisingValueTable);
        if (self->slidingWindow != NULL)              free (self->slidingWindow);
        if (self->compressedSurprisingValues != NULL) free(self->compressedSurprisingValues);
        if (self->compressedWindow != NULL)           free (self->compressedWindow);
        free(self);
    }
}

/*******************************************************/

// Warning: this is called in several places, including during the
// transitional moments during which sketch invariants involving
// flavor and offset are out of whack and in fact we are re-imposing
// them. Therefore it cannot rely on determineFlavor() or
// determineCorrectOffset(). Instead it interprets the low level data
// structures "as is".

// This produces a full-size k-by-64 bit matrix from any Live sketch.

U64 * bitMatrixOfSketch(FM85 * self) {
    assert (self->isCompressed == 0);

    Long k = (1LL << self->lgK);
    Short offset = self->windowOffset;

    assert (offset >= 0 && offset <= 56);

    Long i = 0;
    U64 * matrix = (U64 *) malloc((size_t) (k * sizeof(U64)));

    assert (matrix != NULL);

    // Fill the matrix with default rows in which the "early zone" is filled with ones.
    // This is essential for the routine's O(k) time cost (as opposed to O(C)).
    U64 defaultRow = (1ULL << offset) - 1;
    for (i = 0; i < k; i++) {
        matrix[i] = defaultRow;
    }

    if (self->numCoupons == 0) { 
        return matrix; // Returning a matrix of zeros rather than NULL.
    }

    U8 * window = self->slidingWindow;
    if (window != NULL) { // In other words, we are in window mode, not sparse mode.
        for (i = 0; i < k; i++) { // set the window bits, trusting the sketch's current offset.
            matrix[i] |= (((U64) window[i]) << offset);
        }
    }

    u32Table * table = self->surprisingValueTable;
    assert(table != NULL);
    U32 * slots = table->slots;
    Long numSlots = (1LL << table->lgSize);
    for (i = 0; i < numSlots; i++) { 
        U32 rowCol = slots[i];
        if (rowCol != ALL32BITS) {
            Short col = (Short) (rowCol & 63);
            Long  row = (Long)  (rowCol >> 6);
            // Flip the specified matrix bit from its default value.
            // In the "early" zone the bit changes from 1 to 0.
            // In the "late" zone the bit changes from 0 to 1.
            matrix[row] ^= (1ULL << col);
        }
    }

    return matrix;
}

/*******************************************************/

// T19: The promotion to SPARSE triggers once the first
// coupon fires; as a consequence, the hash table of
// (row, col) coupons is allocated

void promoteEmptyToSparse(FM85 * self) {
    assert(self->numCoupons == 0);
    assert(self->surprisingValueTable == NULL);

    self->surprisingValueTable = u32TableMake(2, 6 + self->lgK);
}

/*******************************************************/
// In terms of flavor, this promotes SPARSE to HYBRID.

void promoteSparseToWindowed(FM85 * self) {
    Long k = (1LL << self->lgK);
    Long c32 = self->numCoupons << 5;
    assert(c32 == 3 * k || (self->lgK == 4 && c32 > 3 * k));

    U8 * window = (U8 *) malloc((size_t) (k * sizeof(U8)));
    assert(window != NULL);
    bzero((void *) window, (size_t) k); // zero the memory (because we will be OR'ing into it)

    u32Table * newTable = u32TableMake(2, 6 + self->lgK);

    u32Table * oldTable = self->surprisingValueTable;
    U32 * oldSlots = oldTable->slots;
    Long oldNumSlots = (1LL << oldTable->lgSize); 

    assert(self->windowOffset == 0);

    for (Long i = 0; i < oldNumSlots; i++) { 
        U32 rowCol = oldSlots[i];
        if (rowCol != ALL32BITS) {
            Short col = (Short) (rowCol & 63);
            if (col < 8) {
                // T19: Coupon inside window
                Long row = (Long) (rowCol >> 6);
                window[row] |= (1 << col);
            } else {
                // T19: Surprising value
                // cannot use u32TableMustInsert(), because it doesn't provide for growth
                Boolean isNovel = u32TableMaybeInsert(newTable, rowCol);

                // T19: All insertions should be novel, because the hash table is empty
                assert(isNovel == 1);
            }
        }
    }
    // fprintf (stderr, "Number of surprising values dropped from %lld to %lld\n", oldTable->numItems, newTable->numItems);

    assert(self->slidingWindow == NULL);
    self->slidingWindow = window;

    self->surprisingValueTable = newTable;
    u32TableFree(oldTable);
}

/*******************************************************/
// this moves the sliding window

/**
 * T19: 
 */
void modifyOffset(FM85 * self, Short newOffset) {
    assert(newOffset >= 0 && newOffset <= 56);
    assert(newOffset == self->windowOffset + 1);
    assert(newOffset == determineCorrectOffset(self->lgK, self->numCoupons));

    assert (self->slidingWindow != NULL);
    assert (self->surprisingValueTable != NULL);

    Long k = (1LL << self->lgK);

    // Construct the full-sized bit matrix that corresponds to the sketch
    U64 * bitMatrix = bitMatrixOfSketch(self);
    assert(bitMatrix != NULL);

    // refresh the KXP register on every 8th window shift.
    if ((newOffset & 0x7) == 0) {
        refreshKXP(self, bitMatrix);
    }

    u32TableClear(self->surprisingValueTable); // the new number of surprises will be about the same

    u32Table * table = self->surprisingValueTable;
    U8 * window = self->slidingWindow;
    U64 maskForClearingWindow = (0xffULL << newOffset) ^ ALL64BITS;
    U64 maskForFlippingEarlyZone = (1ULL << newOffset) - 1;
    U64 allSurprisesORed = 0;
    Long i = 0;

    for (i = 0; i < k; i++) {
        U64 pattern = bitMatrix[i];
        window[i] = (U8) ((pattern >> newOffset) & 0xff);
        pattern &= maskForClearingWindow;
        // The following line converts surprising 0's to 1's in the "early zone", 
        // (and vice versa, which is essential for this procedure's O(k) time cost).
        pattern ^= maskForFlippingEarlyZone; 
        allSurprisesORed |= pattern; // a cheap way to recalculate firstInterestingColumn
        while (pattern != 0) {
            Short col = countTrailingZerosInUnsignedLong(pattern);
            pattern = pattern ^ (1ULL << col); // erase the 1.
            U32 rowCol = (i << 6) | col;
            Boolean isNovel = u32TableMaybeInsert(table, rowCol);
            assert(isNovel == 1);
        }
    }

    free(bitMatrix);
    self->windowOffset = newOffset;

    self->firstInterestingColumn = countTrailingZerosInUnsignedLong(allSurprisesORed);
    if (self->firstInterestingColumn > newOffset) {
        self->firstInterestingColumn = newOffset; // corner case
    }
}


  //  fprintf (stderr, "Changed the offset from %d to %d because C = %lld;", 
  //	  (int) self->windowOffset, (int) newOffset, self->numCoupons); 
  //  fprintf (stderr, "\t[%d]\n", (int) self->firstInterestingColumn);
  // fflush (stderr);

/*******************************************************/

void updateSparse(FM85 * self, U32 rowCol) {
    Long k = (1LL << self->lgK);
    Long c32pre = self->numCoupons << 5;
    assert(c32pre < 3 * k); // C < 3K/32, in other words flavor == SPARSE
    assert(self->surprisingValueTable != NULL);

    Boolean isNovel = u32TableMaybeInsert(self->surprisingValueTable, rowCol);
    if (isNovel) {
        self->numCoupons += 1;
        updateHIP(self, rowCol);
        Long c32post = self->numCoupons << 5;
        if (c32post >= 3 * k) {
            promoteSparseToWindowed(self); // C >= 3K/32
        }
    }
}

/*******************************************************/

// the flavor is HYBRID, PINNED, or SLIDING.
void updateWindowed(FM85 * self, U32 rowCol) {
    assert(self->windowOffset >= 0 && self->windowOffset <= 56);
    Long k = (1LL << self->lgK);
    Long c32pre = self->numCoupons << 5;
    assert (c32pre >= 3*k); // C < 3K/32, in other words flavor >= HYBRID
    Long c8pre = self->numCoupons << 3;
    Long w8pre = ((Long) self->windowOffset) << 3;
    assert(c8pre < (27 + w8pre) * k); // C < (K * 27/8) + (K * windowOffset)

    Boolean isNovel = 0;

    Short col = (Short) (rowCol & 63);

    if (col < self->windowOffset) { // track the surprising 0's "before" the window
        isNovel = u32TableMaybeDelete(self->surprisingValueTable, rowCol); // inverted logic
    } else if (col < self->windowOffset + 8) { // track the 8 bits inside the window
        assert(col >= self->windowOffset);
        Long row = (Long) (rowCol >> 6);
        U8 oldBits = self->slidingWindow[row];
        U8 newBits = oldBits | (1 << (col - self->windowOffset));

        // T19: Novelty detection inside the window is carried out manually
        // (there is no hash table)
        if (newBits != oldBits) {
            self->slidingWindow[row] = newBits;
            isNovel = 1;
        }
    } else { // track the surprising 1's "after" the window
        assert(col >= self->windowOffset + 8);
        isNovel = u32TableMaybeInsert(self->surprisingValueTable, rowCol); // normal logic
    }

    if (isNovel) {
        self->numCoupons += 1;
        updateHIP(self, rowCol);
        Long c8post = self->numCoupons << 3;
        // T19: The window must slide to the right for the first time
        // when C >= 27K/8, then for every K coupons
        if (c8post >= (27 + w8pre) * k) {
            modifyOffset(self, self->windowOffset + 1);
            assert(self->windowOffset >= 1 && self->windowOffset <= 56);
            Long w8post = ((Long) self->windowOffset) << 3;
            assert(c8post < (27 + w8post) * k); // C < (K * 27/8) + (K * windowOffset)
        }
    }
}

/*******************************************************/

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
void fm85RowColUpdate(FM85 * self, U32 rowCol) {
    // T19: Extract the bits representing the column.
    // Remember: 63 is a sequence of 6 consecutive 1s;
    // rowCol stores col in the 6 least significant bits.
    Short col = (Short) (rowCol & 63);

    /**
     * Skip update if the new coupon spawns behind
     * the first column that contains a 0, that is,
     * in a region filled with 1s.
     */
    if (col < self->firstInterestingColumn) {
        return; // important speed optimization
    }

    if (self->isCompressed) {
        FATAL_ERROR("Cannot update a compressed sketch.");
    }

    /**
     * Observe c and update according to the current flavor.
     */
    Long c = self->numCoupons;
    if (c == 0) {
        promoteEmptyToSparse(self);
    }

    /**
     * Promotion to windowed happens inside updateSparse(...)
     * EMPTY and SPARSE don't have a window yet; HYBRID, PINNED,
     * and SLIDING do.
     * 
     * Every flavor but EMPTY has a hash table of (row, col)s
     */
    Long k = (1LL << self->lgK);
    if ((c << 5) < 3 * k) {
        updateSparse(self, rowCol);
    } else {
        updateWindowed(self, rowCol);
    }
}


void fm85Update(FM85 * self, U64 hash0, U64 hash1) {
    U32 rowCol = rowColFromTwoHashes(hash0, hash1, self->lgK);
    fm85RowColUpdate(self, rowCol);
}
