//
//  file: %sys-image.h
//  summary: {Definitions for IMAGE! Datatype}
//  project: "Rebol 3 Interpreter and Run-time (Ren-C branch)"
//  homepage: https://github.com/metaeducation/ren-c/
//
//=////////////////////////////////////////////////////////////////////////=//
//
// Copyright 2012 REBOL Technologies
// Copyright 2012-2019 Ren-C Open Source Contributors
// REBOL is a trademark of REBOL Technologies
//
// See README.md and CREDITS.md for more information
//
// Licensed under the Lesser GPL, Version 3.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
// https://www.gnu.org/licenses/lgpl-3.0.html
//
//=////////////////////////////////////////////////////////////////////////=//
//
// See %extensions/image/README.md
//
//=//// NOTES /////////////////////////////////////////////////////////////=//
//
// * ...


//=//// IMAGE STUB SUBCLASS ///////////////////////////////////////////////=//

// The optimization of using the LINK() and MISC() fields of a Binary is
// not used in Ren-C's image, because that would preclude the use of a
// BLOB! from another source who needed those fields for some other form
// of tracking.  (Imagine if vector used MISC() for its signed flag, and
// you tried to `make image! bytes of my-vector`, overwriting the flag
// with the image width.)  Instead, a singular array to hold the binary
// is made.  A `make image!` that did not use a foreign source could
// optimize this and consider it the binary owner, at same cost as R3-Alpha.

#if CPLUSPLUS_11
    struct Image : public Stub {};
#else
    typedef Stub Image;
#endif


//=//// IMAGE STUB SLOT USAGE /////////////////////////////////////////////=//

#define LINK_IMAGE_WIDTH(s)     (s)->link.length
#define MISC_IMAGE_HEIGHT(s)    (s)->misc.length
// INFO not currently used
// BONUS not currently used



INLINE Image* VAL_IMAGE(const Cell* v) {
    assert(Is_Image(v));
    return cast(Image*, CELL_NODE1(v));
}

#define VAL_IMAGE_BIN(v)        cast(Element*, Stub_Cell(VAL_IMAGE(v)))
#define VAL_IMAGE_WIDTH(v)      LINK_IMAGE_WIDTH(VAL_IMAGE(v))
#define VAL_IMAGE_HEIGHT(v)     MISC_IMAGE_HEIGHT(VAL_IMAGE(v))

#define VAL_IMAGE_HEAD(v) \
    Binary_Head(Cell_Binary_Ensure_Mutable(VAL_IMAGE_BIN(v)))

#define VAL_IMAGE_AT_HEAD(v,pos) \
    (VAL_IMAGE_HEAD(v) + (pos * 4))


// !!! The functions that take into account the current index position in the
// IMAGE!'s ANY-SERIES! payload are sketchy, in the sense that being offset
// into the data does not change the width or height...only the length when
// viewing the image as a 1-dimensional series.  This is not likely to make
// a lot of sense.

#define VAL_IMAGE_POS(v) \
    (v)->payload.split.two.i

#define VAL_IMAGE_AT(v) \
    VAL_IMAGE_AT_HEAD(v, VAL_IMAGE_POS(v))

INLINE REBLEN VAL_IMAGE_LEN_HEAD(const Cell* v) {
    return VAL_IMAGE_HEIGHT(v) * VAL_IMAGE_WIDTH(v);
}

INLINE REBLEN VAL_IMAGE_LEN_AT(const Cell* v) {
    if (VAL_IMAGE_POS(v) >= cast(REBIDX, VAL_IMAGE_LEN_HEAD(v)))
        return 0;  // avoid negative position
    return VAL_IMAGE_LEN_HEAD(v) - VAL_IMAGE_POS(v);
}

INLINE Element* Init_Image(
    Init(Element) out,
    const Binary* bin,
    REBLEN width,
    REBLEN height
){
    assert(Is_Node_Managed(bin));

    Array* blob_holder = cast(Array*, Prep_Stub(
        FLAG_FLAVOR(CELLS)
            | NODE_FLAG_MANAGED
            | (not STUB_FLAG_LINK_NODE_NEEDS_MARK)  // width, integer
            | (not STUB_FLAG_MISC_NODE_NEEDS_MARK)  // height, integer
            | (not STUB_FLAG_INFO_NODE_NEEDS_MARK),  // info, not used ATM
        Alloc_Stub()
    ));
    Init_Blob(Force_Erase_Cell(Stub_Cell(blob_holder)), bin);

    Reset_Extended_Cell_Header_Noquote(
        out,
        EXTRA_HEART_IMAGE,
        (not CELL_FLAG_DONT_MARK_NODE1)  // image stub needs mark
            | CELL_FLAG_DONT_MARK_NODE2  // index shouldn't be marked
    );

    CELL_NODE1(out) = blob_holder;

    VAL_IMAGE_WIDTH(out) = width;  // see why this isn't put on bin...
    VAL_IMAGE_HEIGHT(out) = height;  // (...it would corrupt shared series!)

    VAL_IMAGE_POS(out) = 0;  // !!! sketchy concept, is in BINARY!

    return out;
}

INLINE void RESET_IMAGE(Byte* p, REBLEN num_pixels) {
    Byte* start = p;
    Byte* stop = start + (num_pixels * 4);
    while (start < stop) {
        *start++ = 0;  // red
        *start++ = 0;  // green
        *start++ = 0;  // blue
        *start++ = 0xff;  // opaque alpha, R=G=B as 0 means black pixel
    }
}

// Creates WxH image, black pixels, all opaque.
//
INLINE Cell* Init_Image_Black_Opaque(
    Init(Element) out,
    REBLEN w,
    REBLEN h
){
    Size size = (w * h) * 4;  // RGBA pixels, 4 bytes each
    Binary* bin = Make_Binary(size);
    Term_Binary_Len(bin, size);
    Manage_Flex(bin);

    RESET_IMAGE(Binary_Head(bin), (w * h));  // length in 'pixels'

    return Init_Image(out, bin, w, h);
}
