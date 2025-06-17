//
//  file: %mod-image.c
//  summary: "image datatype"
//  section: datatypes
//  project: "Rebol 3 Interpreter and Run-time (Ren-C branch)"
//  homepage: https://github.com/metaeducation/ren-c/
//
//=////////////////////////////////////////////////////////////////////////=//
//
// Copyright 2012 REBOL Technologies
// Copyright 2012-2019 Ren-C Open Source Contributors
// REBOL is a trademark of REBOL Technologies
//
// See README.md and CREDITS.md for more information.
//
// Licensed under the Lesser GPL, Version 3.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
// https://www.gnu.org/licenses/lgpl-3.0.html
//
//=////////////////////////////////////////////////////////////////////////=//
//
// This code originated from R3-Alpha.  It was not particularly well thought
// out in terms of how a 2-dimensional array of pixels would fit into the
// typically linear "series" model of Rebol.  But the primary goal of the
// implementation was to reuse Rebol's existing code for the BINARY! datatype
// with a "thin" layer on top of it, minimizing overhead.
//
// GUI is not a priority for the Ren-C design, so little effort has been
// spent trying to rethink it.  It was even abandoned for a time.  But it was
// brought back as a test of the extension model for handling new generics,
// to demonstrate IMPLEMENT_GENERIC() with a non-builtin type.
//
// See further remarks in %extensions/image/README.md
//

#include "sys-core.h"
#include "tmp-mod-image.h"

#include "sys-image.h"


//
//  Set_Pixel_Tuple: C
//
static void Set_Pixel_Tuple(Byte* dp, const Element* tuple)
{
    dp[0] = Cell_Sequence_Byte_At(tuple, 0);  // red
    dp[1] = Cell_Sequence_Byte_At(tuple, 1);  // green
    dp[2] = Cell_Sequence_Byte_At(tuple, 2); // blue
    if (Cell_Sequence_Len(tuple) > 3)
        dp[3] = Cell_Sequence_Byte_At(tuple, 3);  // alpha
    else
        dp[3] = 0xff;  // default alpha to opaque
}


//
//  Tuples_To_RGBA: C
//
// Read BLOCK! of TUPLE! into sequential RGBA memory runs.
//
static void Tuples_To_RGBA(
    Byte* rgba,
    REBLEN size,
    const Element* head,
    REBLEN len
){
    if (len > size)
        len = size;  // avoid over-run

    const Element* item = head;
    for (; len > 0; len--, rgba += 4, ++head)
        Get_Tuple_Bytes(rgba, item, 4);
}


//
//  Fill_Line: C
//
static void Fill_Line(Byte* ip, const Byte pixel[4], REBLEN len, bool only)
{
    for (; len > 0; len--) {
        *ip++ = pixel[0]; // red
        *ip++ = pixel[1]; // green
        *ip++ = pixel[2]; // blue
        if (only)
            ++ip; // only RGB, don't change alpha...just skip it
        else
            *ip++ = pixel[3]; // alpha
    }
}


//
//  Fill_Alpha_Line: C
//
static void Fill_Alpha_Line(Byte* rgba, Byte alpha, REBINT len)
{
    for (; len > 0; len--, rgba += 4)
        rgba[3] = alpha;
}


//
//  Fill_Rect: C
//
static void Fill_Rect(
    Byte* ip,
    const Byte pixel[4],
    REBLEN w,
    REBINT dupx,
    REBINT dupy,
    bool only
){
    for (; dupy > 0; dupy--, ip += (w * 4))
        Fill_Line(ip, pixel, dupx, only);
}


//
//  Fill_Alpha_Rect: C
//
static void Fill_Alpha_Rect(
    Byte* ip,
    Byte alpha,
    REBINT w,
    REBINT dupx,
    REBINT dupy
){
    for (; dupy > 0; dupy--, ip += (w * 4))
        Fill_Alpha_Line(ip, alpha, dupx);
}


//
//  Find_Non_Tuple_In_Array: C
//
// Searches array from current index, returns non-tuple cell if found.
//
static Option(const Element*) Find_Non_Tuple_In_Array(const Element* any_array)
{
    const Element* tail;
    const Element* v = Cell_List_At(&tail, any_array);

    for (; v != tail; ++v)
        if (not Is_Block(v))
            return v;

    return nullptr;
}


IMPLEMENT_GENERIC(MAKE, Is_Image) {
    INCLUDE_PARAMS_OF_MAKE;  // spec [<opt-out> blank? pair! block!]
    UNUSED(ARG(TYPE));

    Element* spec = Element_ARG(DEF);

    if (Is_Blank(spec)) {  // empty image (same as make image! [])
        Init_Image_Black_Opaque(OUT, 0, 0);
        return OUT;
    }

    if (Is_Pair(spec)) {  // `make image! 10x20`
        REBINT w = Cell_Pair_X(spec);
        REBINT h = Cell_Pair_Y(spec);
        w = MAX(w, 0);
        h = MAX(h, 0);
        Init_Image_Black_Opaque(OUT, w, h);
        return OUT;
    }

    if (Is_Block(spec)) {  // make image! [size rgba index]
        const Element* tail;
        const Element* item = Cell_List_At(&tail, spec);
        if (item == tail or not Is_Pair(item))
            return PANIC(PARAM(DEF));

        REBINT w = Cell_Pair_X(item);
        REBINT h = Cell_Pair_Y(item);
        if (w < 0 or h < 0)
            return PANIC(PARAM(DEF));

        ++item;

        if (item == tail) {  // just `make image! [10x20]`, allow it
            Init_Image_Black_Opaque(OUT, w, h);
            ++item;
        }
        else if (Is_Blob(item)) {  // use bytes as-is

            // !!! R3-Alpha separated out the alpha channel from the RGB
            // data in MAKE, even though it stored all the data together.
            // We can't use a binary directly as the backing store for an
            // image unless it has all the RGBA components together.  While
            // some MAKE-like procedure might allow you to pass in separate
            // components, the value of a system one is to use the data
            // directly as-is...so Ren-C only supports RGBA.

            if (VAL_INDEX(item) != 0)
                return FAIL("MAKE IMAGE! w/BINARY! must have binary at HEAD");

            if (Cell_Series_Len_Head(item) != cast(REBLEN, w * h * 4))
                return FAIL("MAKE IMAGE! w/BINARY! needs RGBA pixels for size");

            Init_Image(OUT, Cell_Binary(item), w, h);
            ++item;

            // !!! Sketchy R3-Alpha concept: "image position".  The block
            // MAKE IMAGE! format allowed you to specify it.

            if (item != tail and Is_Integer(item)) {
                VAL_IMAGE_POS(OUT) = (Int32s(item, 1) - 1);
                ++item;
            }
        }
        else if (Is_Block(item)) {  // `make image! [1.2.3.255 4.5.6.128 ...]`
            Init_Image_Black_Opaque(OUT, w, h);  // inefficient, overwritten
            Byte* ip = VAL_IMAGE_HEAD(OUT); // image pointer

            Byte pixel[4];
            Set_Pixel_Tuple(pixel, item);
            Fill_Rect(ip, pixel, w, w, h, true);
            ++item;
            if (Is_Integer(item)) {
                Fill_Alpha_Rect(
                    ip, cast(Byte, VAL_INT32(item)), w, w, h
                );
                ++item;
            }
        }
        else if (Is_Block(item)) {
            Init_Image_Black_Opaque(OUT, w, h);  // inefficient, overwritten

            Option(const Element*) non_tuple = Find_Non_Tuple_In_Array(item);
            if (non_tuple)
                panic (Error_Bad_Value(unwrap(non_tuple)));

            Byte* ip = VAL_IMAGE_HEAD(OUT);  // image pointer

            Tuples_To_RGBA(
                ip, w * h, Cell_List_Item_At(item), Cell_Series_Len_At(item)
            );
        }
        else
            return PANIC(PARAM(DEF));

        if (item != tail)
            return PANIC("Too many elements in BLOCK! for MAKE IMAGE!");

        return OUT;
    }

    return PANIC(PARAM(DEF));
}


//
//  Copy_Rect_Data: C
//
static void Copy_Rect_Data(
    Sink(Element) dst,
    REBINT dx,
    REBINT dy,
    REBINT w,
    REBINT h,
    const Element* src,
    REBINT sx,
    REBINT sy
){
    if (w <= 0 || h <= 0)
        return;

    // Clip at edges:
    if (dx + w > VAL_IMAGE_WIDTH(dst))
        w = VAL_IMAGE_WIDTH(dst) - dx;
    if (dy + h > VAL_IMAGE_HEIGHT(dst))
        h = VAL_IMAGE_HEIGHT(dst) - dy;

    const Byte* sbits =
        VAL_IMAGE_HEAD(src)
        + (sy * VAL_IMAGE_WIDTH(src) + sx) * 4;
    Byte* dbits =
        VAL_IMAGE_HEAD(dst)
        + (dy * VAL_IMAGE_WIDTH(dst) + dx) * 4;
    while (h--) {
        memcpy(dbits, sbits, w*4);
        sbits += VAL_IMAGE_WIDTH(src) * 4;
        dbits += VAL_IMAGE_WIDTH(dst) * 4;
    }
}


// There is an image "position" stored in the binary.  This is a dodgy
// concept of a linear index into the image being an X/Y coordinate and
// permitting "series" operations.  In any case, for two images to compare
// alike they are compared according to this...but note the width and height
// aren't taken into account.
//
// https://github.com/rebol/rebol-issues/issues/801
//
IMPLEMENT_GENERIC(EQUAL_Q, Is_Image)
{
    INCLUDE_PARAMS_OF_EQUAL_Q;

    UNUSED(Bool_ARG(RELAX));

    Element* a = Element_ARG(VALUE1);
    Element* b = Element_ARG(VALUE2);

    if (VAL_IMAGE_WIDTH(a) != VAL_IMAGE_WIDTH(b))
        return LOGIC(false);

    if (VAL_IMAGE_HEIGHT(a) != VAL_IMAGE_HEIGHT(b))
        return LOGIC(false);

    if (VAL_IMAGE_POS(a) != VAL_IMAGE_POS(b))
        return LOGIC(false);

    assert(VAL_IMAGE_LEN_AT(a) == VAL_IMAGE_LEN_AT(b));

    int cmp = memcmp(VAL_IMAGE_AT(a), VAL_IMAGE_AT(b), VAL_IMAGE_LEN_AT(a));
    return LOGIC(cmp == 0);
}


//
//  Reset_Height: C
//
// Set height based on tail and width.
//
static void Reset_Height(Element* value)
{
    Element* binary = VAL_IMAGE_BIN(value);
    REBLEN w = VAL_IMAGE_WIDTH(value);
    VAL_IMAGE_HEIGHT(value) = w ? ((Cell_Series_Len_Head(binary) / w) / 4) : 0;
}


//
//  Init_Tuple_From_Pixel: C
//
static Element* Init_Tuple_From_Pixel(Sink(Element) out, const Byte* dp)
{
    return Init_Tuple_Bytes(out, dp, 4);
}


//
//  Find_Color: C
//
static Byte* Find_Color(
    Byte* ip,
    const Byte pixel[4],
    REBLEN len,
    bool only
){
    for (; len > 0; len--, ip += 4) {
        if (ip[0] != pixel[0])
            continue; // red not equal
        if (ip[1] != pixel[1])
            continue; // green not equal
        if (ip[2] != pixel[2])
            continue; // blue not equal
        if (not only and ip[3] != pixel[3])
            continue; // paying attention to alpha, and not equal
        return ip;
    }
    return nullptr;
}


//
//  Find_Alpha: C
//
static Byte* Find_Alpha(Byte* ip, Byte alpha, REBLEN len)
{
    for (; len > 0; len--, ip += 4) {
        if (alpha == ip[3])
            return ip; // alpha equal in rgba[3]
    }
    return nullptr;
}


//
//  RGB_To_Bin: C
//
static void RGB_To_Bin(Byte* bin, Byte* rgba, REBINT len, bool alpha)
{
    if (alpha) {
        for (; len > 0; len--, rgba += 4, bin += 4) {
            bin[0] = rgba[0];
            bin[1] = rgba[1];
            bin[2] = rgba[2];
            bin[3] = rgba[3];
        }
    } else {
        // Only the RGB part:
        for (; len > 0; len--, rgba += 4, bin += 3) {
            bin[0] = rgba[0];
            bin[1] = rgba[1];
            bin[2] = rgba[2];
        }
    }
}


//
//  Bin_To_RGB: C
//
static void Bin_To_RGB(Byte* rgba, REBLEN size, const Byte* bin, REBLEN len)
{
    if (len > size)
        len = size; // avoid over-run

    for (; len > 0; len--, rgba += 4, bin += 3) {
        rgba[0] = bin[0]; // red
        rgba[1] = bin[1]; // green
        rgba[2] = bin[2]; // blue
        // don't touch alpha of destination
    }
}


//
//  Bin_To_RGBA: C
//
static void Bin_To_RGBA(
    Byte* rgba,
    REBLEN size,
    const Byte* bin,
    REBINT len,
    bool only
){
    if (len > (REBINT)size) len = size; // avoid over-run

    for (; len > 0; len--, rgba += 4, bin += 4) {
        rgba[0] = bin[0]; // red
        rgba[1] = bin[1]; // green
        rgba[2] = bin[2]; // blue
        if (not only)
            rgba[3] = bin[3]; // write alpha of destination if requested
    }
}


//
//  Alpha_To_Bin: C
//
static void Alpha_To_Bin(Byte* bin, const Byte* rgba, REBINT len)
{
    for (; len > 0; len--, rgba += 4)
        *bin++ = rgba[3];
}


//
//  Bin_To_Alpha: C
//
static void Bin_To_Alpha(Byte* rgba, REBLEN size, const Byte* bin, REBINT len)
{
    if (len > (REBINT)size) len = size; // avoid over-run

    for (; len > 0; len--, rgba += 4)
        rgba[3] = *bin++;
}


//
//  Mold_Image_Data: C
//
// Output RGBA image data
//
// !!! R3-Alpha always used 4 bytes per pixel for images, so the idea that
// images would "not have an alpha channel" only meant that they had all
// transparent bytes.  In order to make images less monolithic (and enable
// them to be excised from the core into an extension), the image builds
// directly on a BINARY! that the user can pass in and extract.  This has
// to be consistent with the internal format, so the idea of "alpha-less"
// images is removed from MAKE IMAGE! and related molding.
//
static void Mold_Image_Data(Molder* mo, const Element* value)
{
    USED(&Mold_Image_Data);

    REBLEN num_pixels = VAL_IMAGE_LEN_AT(value); // # from index to tail
    const Byte* rgba = VAL_IMAGE_AT(value);

    Append_Ascii(mo->string, " #{");

    REBLEN i;
    for (i = 0; i < num_pixels; ++i, rgba += 4) {
        if ((i % 10) == 0)
            Append_Codepoint(mo->string, LF);
        Form_RGBA(mo, rgba);
    }

    Append_Ascii(mo->string, "\n}");
}


//
//  Clear_Image: C
//
// Clear image data (sets R=G=B=A to 0)
//
static void Clear_Image(Element* img)
{
    USED(&Clear_Image);

    REBLEN w = VAL_IMAGE_WIDTH(img);
    REBLEN h = VAL_IMAGE_HEIGHT(img);
    Byte* p = VAL_IMAGE_HEAD(img);
    memset(p, 0, w * h * 4);
}


//
//  Modify_Image: C
//
// CHANGE/INSERT/APPEND image
//
// !!! R3-Alpha had the concept that images were an "ANY-SERIES!", which was
// slippery.  What does it mean to "append" a red pixel to a 10x10 image?
// What about to "insert"?  CHANGE may seem to make sense in a positional
// world where the position was a coordinate and you change to a rectangle
// of data that is another image.
//
// While the decode/encode abilities of IMAGE! are preserved, R3-Alpha code
// like this has been excised from the core and into an extension for a
// reason.  (!)  The code is deprecated, but kept around and building for any
// sufficiently motivated individual who wanted to review it.
//
static Bounce Modify_Image(Level* level_, SymId sym)
{
    INCLUDE_PARAMS_OF_CHANGE;  // currently must have same frame as CHANGE

    assert(sym == SYM_CHANGE or sym == SYM_INSERT or sym == SYM_APPEND);

    Element* value = Element_ARG(SERIES);  // !!! confusing name
    Cell_Binary_Ensure_Mutable(VAL_IMAGE_BIN(value));

    if (Is_Nulled(ARG(VALUE))) {  // void
        if (sym == SYM_APPEND)  // append returns head position
            VAL_IMAGE_POS(value) = 0;
        return COPY(value);  // don't panic on read only if it would be noop
    }
    if (Is_Antiform(ARG(VALUE)))
        return PANIC(PARAM(VALUE));
    Element* arg = Element_ARG(VALUE);

    if (Bool_ARG(LINE))
        panic (Error_Bad_Refines_Raw());

    Binary* bin = Cell_Binary_Ensure_Mutable(VAL_IMAGE_BIN(value));

    REBLEN index = VAL_IMAGE_POS(value);
    REBLEN tail = VAL_IMAGE_LEN_HEAD(value);
    Byte* ip;

    REBINT w = VAL_IMAGE_WIDTH(value);
    if (w == 0)
        return COPY(value);

    if (sym == SYM_APPEND) {
        index = tail;
        sym = SYM_INSERT;
    }

    REBINT x = index % w;  // offset on the line
    REBINT y = index / w;  // offset line

    bool only = false;

    if (Is_Block(arg)) {
        Option(const Element*) non_tuple = Find_Non_Tuple_In_Array(arg);
        if (non_tuple)
            panic (Error_Bad_Value(unwrap non_tuple));
    }

    REBINT dup = 1;
    REBINT dup_x = 0;
    REBINT dup_y = 0;

    if (Bool_ARG(DUP)) {  // "it specifies fill size"
        if (Is_Integer(ARG(DUP))) {
            dup = VAL_INT32(ARG(DUP));
            dup = MAX(dup, 0);
            if (dup == 0)
                return COPY(value);
        }
        else if (Is_Pair(ARG(DUP))) {  // rectangular dup
            dup_x = Cell_Pair_X(ARG(DUP));
            dup_y = Cell_Pair_Y(ARG(DUP));
            dup_x = MAX(dup_x, 0);
            dup_x = MIN(dup_x, cast(REBINT, w) - x);  // clip dup width
            dup_y = MAX(dup_y, 0);
            if (sym != SYM_INSERT)
                dup_y = MIN(dup_y, cast(REBINT, VAL_IMAGE_HEIGHT(value)) - y);
            else
                dup = dup_y * w;
            if (dup_x == 0 or dup_y == 0)
                return COPY(value);
        }
        else
            return PANIC(PARAM(DUP));
    }

    REBINT part = 1;
    REBINT part_x = 0;
    REBINT part_y = 0;

    if (Bool_ARG(PART)) {  // only allowed when arg is a series
        if (Is_Blob(arg)) {
            if (Is_Integer(ARG(PART))) {
                part = VAL_INT32(ARG(PART));
            } else if (Is_Blob(ARG(PART))) {
                part = (VAL_INDEX(ARG(PART)) - VAL_INDEX(arg)) / 4;
            } else
                panic (PARAM(PART));
            part = MAX(part, 0);
        }
        else if (Is_Image(arg)) {
            if (Is_Integer(ARG(PART))) {
                part = VAL_INT32(ARG(PART));
                part = MAX(part, 0);
            }
            else if (Is_Image(ARG(PART))) {
                if (VAL_IMAGE_WIDTH(ARG(PART)) == 0)
                    panic (PARAM(PART));

                part_x = VAL_IMAGE_POS(ARG(PART)) - VAL_IMAGE_POS(arg);
                part_y = part_x / VAL_IMAGE_WIDTH(ARG(PART));
                part_y = MAX(part_y, 1);
                part_x = MIN(part_x, cast(REBINT, VAL_IMAGE_WIDTH(arg)));
                goto len_compute;
            }
            else if (Is_Pair(ARG(PART))) {
                part_x = Cell_Pair_X(ARG(PART));
                part_y = Cell_Pair_Y(ARG(PART));
            len_compute:
                part_x = MAX(part_x, 0);
                part_x = MIN(part_x, cast(REBINT, w) - x);  // clip part width
                part_y = MAX(part_y, 0);
                if (sym != SYM_INSERT)
                    part_y = MIN(
                        part_y,
                        cast(REBINT, VAL_IMAGE_HEIGHT(value) - y)
                    );
                else
                    part = part_y * w;
                if (part_x == 0 || part_y == 0)
                    return COPY(value);
            }
            else
                return PANIC(PARAM(PART));
        }
        else
            return PANIC(PARAM(VALUE));  // /PART not allowed
    }
    else {
        if (Is_Image(arg)) {  // Use image for /PART sizes
            part_x = VAL_IMAGE_WIDTH(arg);
            part_y = VAL_IMAGE_HEIGHT(arg);
            part_x = MIN(part_x, cast(REBINT, w) - x);  // clip part width
            if (sym != SYM_INSERT)
                part_y = MIN(part_y, cast(REBINT, VAL_IMAGE_HEIGHT(value)) - y);
            else
                part = part_y * w;
        }
        else if (Is_Blob(arg)) {
            part = Cell_Series_Len_At(arg) / 4;
        }
        else if (Is_Block(arg)) {
            part = Cell_Series_Len_At(arg);
        }
        else if (!Is_Integer(arg) && !Is_Block(arg))
            return PANIC(PARAM(VALUE));
    }

    // Expand image data if necessary:
    if (sym == SYM_INSERT) {
        if (index > tail) index = tail;
        Expand_Flex(bin, index, dup * part);

        //length in 'pixels'
        RESET_IMAGE(Binary_Head(bin) + (index * 4), dup * part);
        Reset_Height(value);
        tail = Cell_Series_Len_Head(value);
        only = false;
    }
    ip = VAL_IMAGE_HEAD(value);

    // Handle the datatype of the argument.
    if (Is_Integer(arg) || Is_Block(arg)) {  // scalars
        if (index + dup > tail) dup = tail - index;  // clip it
        ip += index * 4;
        if (Is_Integer(arg)) { // Alpha channel
            REBINT arg_int = VAL_INT32(arg);
            if ((arg_int < 0) || (arg_int > 255))
                panic (Error_Out_Of_Range(arg));

            if (Is_Pair(ARG(DUP)))  // rectangular fill
                Fill_Alpha_Rect(
                    ip, cast(Byte, arg_int), w, dup_x, dup_y
                );
            else
                Fill_Alpha_Line(ip, cast(Byte, arg_int), dup);
        }
        else if (Is_Block(arg)) {  // RGB
            Byte pixel[4];
            Set_Pixel_Tuple(pixel, arg);
            if (Is_Pair(ARG(DUP)))  // rectangular fill
                Fill_Rect(ip, pixel, w, dup_x, dup_y, only);
            else
                Fill_Line(ip, pixel, dup, only);
        }
    } else if (Is_Image(arg)) {
        // dst dx dy w h src sx sy
        Copy_Rect_Data(value, x, y, part_x, part_y, arg, 0, 0);
    }
    else if (Is_Blob(arg)) {
        Size size;
        const Byte* data = Cell_Blob_Size_At(&size, arg);
        if (part > cast(REBINT, size))
            part = size;  // clip it
        ip += index * 4;
        for (; dup > 0; dup--, ip += part * 4)
            Bin_To_RGBA(ip, part, data, part, only);
    }
    else if (Is_Block(arg)) {
        if (index + part > tail) part = tail - index;  // clip it
        ip += index * 4;
        for (; dup > 0; dup--, ip += part * 4)
            Tuples_To_RGBA(ip, part, Cell_List_Item_At(arg), part);
    }
    else
        return PANIC(PARAM(VALUE));

    Reset_Height(value);

    if (sym == SYM_APPEND)
        VAL_IMAGE_POS(value) = 0;
    return COPY(value);
}


//
//  Find_Image: C
//
// Finds a value in a series and returns the series at the start of it.  For
// parameters of FIND, see the action definition.
//
// !!! old and very broken code, untested and probably (hopefully) not
// used by R3-View... (?)
//
static Bounce Find_Image(Level* level_)
{
    INCLUDE_PARAMS_OF_FIND;

    Element* image = Element_ARG(SERIES);
    Element* pattern = Element_ARG(PATTERN);
    REBLEN index = VAL_IMAGE_POS(image);
    REBLEN tail = VAL_IMAGE_LEN_HEAD(image);
    Byte* ip = VAL_IMAGE_AT(image);

    REBLEN len = tail - index;
    if (len == 0)
        return nullptr;

    // !!! There is a general problem with refinements and actions in R3-Alpha
    // in terms of reporting when a refinement was ignored.  This is a
    // problem that archetype-based dispatch will need to address.
    //
    if (
        Bool_ARG(CASE)
        or Bool_ARG(SKIP)
        or Bool_ARG(MATCH)
        or Bool_ARG(PART)
    ){
        panic (Error_Bad_Refines_Raw());
    }

    bool only = false;

    Byte* p;

    if (Is_Tuple(pattern)) {
        only = (Cell_Sequence_Len(pattern) < 4);

        Byte pixel[4];
        Set_Pixel_Tuple(pixel, pattern);
        p = Find_Color(ip, pixel, len, only);
    }
    else if (Is_Integer(pattern)) {
        REBINT i = VAL_INT32(pattern);
        if (i < 0 or i > 255)
            return PANIC(Error_Out_Of_Range(pattern));

        p = Find_Alpha(ip, i, len);
    }
    else if (Is_Image(pattern)) {
        return nullptr;
    }
    else if (Is_Blob(pattern)) {
        return nullptr;
    }
    else
        return PANIC(PARAM(PATTERN));

    // Post process the search (failure or apply /match and /tail):

    Copy_Cell(OUT, image);
    assert((p - VAL_IMAGE_HEAD(image)) % 4 == 0);

    REBINT n = cast(REBLEN, (p - VAL_IMAGE_HEAD(image)) / 4);
    if (Bool_ARG(MATCH)) {
        if (n != cast(REBINT, index)) {
            return nullptr;
        }
        n++;
    }

    VAL_IMAGE_POS(image) = n;
    return OUT;
}


//
//  Image_Has_Alpha: C
//
// !!! See code in R3-Alpha for VITT_ALPHA and the `save` flag.
//
static bool Image_Has_Alpha(const Element* v)
{
    USED(&Image_Has_Alpha);

    Byte* p = VAL_IMAGE_HEAD(v);

    int i = VAL_IMAGE_WIDTH(v) * VAL_IMAGE_HEIGHT(v);
    for(; i > 0; i--, p += 4) {
        if (p[3] != 0) // non-zero (e.g. non-transparent) alpha component
            return true;
    }
    return false;
}


//
//  Make_Complemented_Image: C
//
static void Make_Complemented_Image(Sink(Element) out, const Element* v)
{
    Byte* img = VAL_IMAGE_AT(v);
    REBINT len = VAL_IMAGE_LEN_AT(v);

    Init_Image_Black_Opaque(out, VAL_IMAGE_WIDTH(v), VAL_IMAGE_HEIGHT(v));

    Byte* dp = VAL_IMAGE_HEAD(out);
    for (; len > 0; len --) {
        *dp++ = ~ *img++; // copy complemented red
        *dp++ = ~ *img++; // copy complemented green
        *dp++ = ~ *img++; // copy complemented blue
        *dp++ = ~ *img++; // copy complemented alpha !!! Is this intended?
    }
}


IMPLEMENT_GENERIC(MOLDIFY, Is_Image)
{
    INCLUDE_PARAMS_OF_MOLDIFY;

    Element* cell = Element_ARG(ELEMENT);
    Molder* mo = Cell_Handle_Pointer(Molder, ARG(MOLDER));
    bool form = Bool_ARG(FORM);

    UNUSED(form); // no difference between MOLD and FORM at this time

    Begin_Non_Lexical_Mold(mo, cell);
    Append_Int(mo->string, VAL_IMAGE_WIDTH(cell));
    Append_Ascii(mo->string, "x");
    Append_Int(mo->string, VAL_IMAGE_HEIGHT(cell));
    End_Non_Lexical_Mold(mo);

    return TRIPWIRE;
}


static bool Adjust_Image_Pick_Index_Is_Valid(
    REBINT *index, // gets adjusted
    const Element* value, // image
    const Value* picker
) {
    REBINT n;
    if (Is_Pair(picker)) {
        n = (
            (Cell_Pair_Y(picker) - 1) * VAL_IMAGE_WIDTH(value)
            + (Cell_Pair_X(picker) - 1)
        ) + 1;
    }
    else if (Is_Integer(picker))
        n = VAL_INT32(picker);
    else if (Is_Decimal(picker))
        n = cast(REBINT, VAL_DECIMAL(picker));
    else
        panic (picker);

    *index += n;
    if (n > 0)
        (*index)--;

    if (
        n == 0
        or *index < 0
        or *index >= cast(REBINT, VAL_IMAGE_LEN_HEAD(value))
    ){
        return false; // out of range
    }

    return true;
}


//
//  REBTYPE: C
//
IMPLEMENT_GENERIC(OLDGENERIC, Is_Image)
{
    Element* image = Known_Element(ARG_N(1));

    REBINT index = VAL_IMAGE_POS(image);
    REBINT tail = Binary_Len(Cell_Binary(VAL_IMAGE_BIN(image)));

    // Clip index if past tail:
    //
    if (index > tail)
        index = tail;

    Option(SymId) id = Symbol_Id(Level_Verb(LEVEL));

    switch (id) {
      case SYM_BITWISE_NOT:
        Make_Complemented_Image(OUT, image);
        return OUT;

      case SYM_SKIP:
      case SYM_AT: {
        Value* arg = ARG_N(2);

        // This logic is somewhat complicated by the fact that INTEGER args use
        // base-1 indexing, but PAIR args use base-0.

        REBINT diff;
        if (Is_Pair(arg)) {
            if (id == SYM_AT)
                id = SYM_SKIP;
            diff = (Cell_Pair_Y(arg) * VAL_IMAGE_WIDTH(image))
                + Cell_Pair_X(arg) + (id == SYM_SKIP ? 0 : 1);
        } else
            diff = Get_Num_From_Arg(arg);

        index += diff;
        if (id == SYM_SKIP) {
            if (Is_Logic(arg))
                --index;
        }
        else {
            if (diff > 0)
                --index; // For at, pick, poke.
        }

        if (index > tail)
            index = tail;
        else if (index < 0)
            index = 0;

        VAL_IMAGE_POS(image) = cast(REBLEN, index);
        return COPY(image); }

      case SYM_CLEAR:
        if (index < tail) {
            Set_Flex_Len(
                Cell_Binary_Ensure_Mutable(VAL_IMAGE_BIN(image)),
                cast(REBLEN, index)
            );
            Reset_Height(image);
        }
        return COPY(image);

      case SYM_REMOVE: {
        INCLUDE_PARAMS_OF_REMOVE;
        UNUSED(PARAM(SERIES));

        Binary* bin = Cell_Binary_Ensure_Mutable(VAL_IMAGE_BIN(image));

        REBINT len;
        if (Bool_ARG(PART)) {
            Value *val = ARG(PART);
            if (Is_Integer(val)) {
                len = VAL_INT32(val);
            }
            else if (Is_Image(val)) {
                if (VAL_IMAGE_WIDTH(val) == 0)
                    return PANIC(PARAM(PART));
                len = VAL_IMAGE_POS(val) - VAL_IMAGE_POS(image);
            }
            else
                return PANIC(PARAM(PART));
        }
        else len = 1;

        index = cast(REBINT, VAL_IMAGE_POS(image));
        if (index < tail and len != 0) {
            Remove_Flex_Units(bin, index, len);
        }
        Reset_Height(image);
        return COPY(image); }

      case SYM_APPEND:
      case SYM_INSERT:
      case SYM_CHANGE:
        return Modify_Image(level_, unwrap id);

      case SYM_FIND:
        return Find_Image(level_);

      default:
        break;
    }

    return UNHANDLED;
}


static void Copy_Image_Value(Sink(Element) out, const Element* arg, REBINT len)
{
    len = MAX(len, 0); // no negatives
    len = MIN(len, cast(REBINT, VAL_IMAGE_LEN_AT(arg)));

    REBINT w = VAL_IMAGE_WIDTH(arg);
    w = MAX(w, 1);

    REBINT h;
    if (len <= w) {
        h = 1;
        w = len;
    }
    else
        h = len / w;

    if (w == 0)
        h = 0;

    Init_Image_Black_Opaque(out, w, h);
    memcpy(VAL_IMAGE_HEAD(out), VAL_IMAGE_AT(arg), w * h * 4);
}


IMPLEMENT_GENERIC(COPY, Is_Image)
{
    INCLUDE_PARAMS_OF_COPY;

    Element* image = Element_ARG(VALUE);

    if (Bool_ARG(DEEP))
        return PANIC(Error_Bad_Refines_Raw());

    if (not Bool_ARG(PART)) {
        Copy_Image_Value(OUT, image, VAL_IMAGE_LEN_AT(image));
        return OUT;
    }

    Element* part = Element_ARG(PART);  // can be image, integer, pair.

    if (Is_Image(part)) {
        if (VAL_IMAGE_BIN(part) != VAL_IMAGE_BIN(image))
            return PANIC(PARAM(PART));

        REBINT len = VAL_IMAGE_POS(part) - VAL_IMAGE_POS(image);
        Copy_Image_Value(OUT, image, len);
        return OUT;
    }

    if (Is_Integer(part)) {
        REBINT len = VAL_INT32(part);
        Copy_Image_Value(OUT, image, len);
        return OUT;
    }

    if (Is_Pair(part)) {
        REBINT w = Cell_Pair_X(part);
        REBINT h = Cell_Pair_Y(part);
        w = MAX(w, 0);
        h = MAX(h, 0);
        REBINT diff = MIN(
            Cell_Series_Len_Head(VAL_IMAGE_BIN(image)),
            VAL_IMAGE_POS(image)
        );
        diff = MAX(0, diff);
        REBINT width = VAL_IMAGE_WIDTH(image);
        REBINT y;
        REBINT x;
        if (width != 0) {
            y = diff / width;  // compute y offset
            x = diff %= width;  // compute x offset
        } else {
            y = x = 0;  // avoid div zero
        }
        w = MIN(w, width - x);
        h = MIN(h, VAL_IMAGE_HEIGHT(image) - y);
        Init_Image_Black_Opaque(OUT, w, h);
        Copy_Rect_Data(OUT, 0, 0, w, h, image, x, y);
        /*
            VAL_IMAGE_TRANSP(OUT) = VAL_IMAGE_TRANSP(image);  // ???
        */
        return OUT;
    }

    return PANIC(PARAM(PART));
}


IMPLEMENT_GENERIC(PICK, Is_Image)
{
    INCLUDE_PARAMS_OF_PICK;

    Element* image = Element_ARG(LOCATION);
    const Element* picker = Element_ARG(PICKER);

    REBINT index = cast(REBINT, VAL_IMAGE_POS(image));
    REBINT len = VAL_IMAGE_LEN_HEAD(image) - index;
    len = MAX(len, 0);

    Byte* src = VAL_IMAGE_AT(image);

    if (Is_Word(picker)) {
        switch (Cell_Word_Id(picker)) {
          case SYM_SIZE:
            Init_Pair(OUT, VAL_IMAGE_WIDTH(image), VAL_IMAGE_HEIGHT(image));
            goto adjust_index;

          case EXT_SYM_RGB: {
            Binary* nser = Make_Binary(len * 3);
            Set_Flex_Len(nser, len * 3);
            RGB_To_Bin(Binary_Head(nser), src, len, false);
            Term_Binary(nser);
            Init_Blob(OUT, nser);
            goto adjust_index; }

          case EXT_SYM_ALPHA: {
            Binary* nser = Make_Binary(len);
            Set_Flex_Len(nser, len);
            Alpha_To_Bin(Binary_Head(nser), src, len);
            Term_Binary(nser);
            Init_Blob(OUT, nser);
            goto adjust_index; }

          default:
            break;
        }
        return PANIC(PARAM(PICKER));
    }

  adjust_index:

    if (Adjust_Image_Pick_Index_Is_Valid(&index, image, picker))
        Init_Tuple_From_Pixel(OUT, VAL_IMAGE_AT_HEAD(image, index));
    else
        Init_Nulled(OUT);

    return OUT;
}


IMPLEMENT_GENERIC(POKE, Is_Image)
{
    INCLUDE_PARAMS_OF_POKE;

    Element* image = Element_ARG(LOCATION);
    const Element* picker = Element_ARG(PICKER);

    if (Is_Antiform(ARG(VALUE)))
        return FAIL(PARAM(VALUE));

    Element* poke = Element_ARG(VALUE);

    Cell_Binary_Ensure_Mutable(VAL_IMAGE_BIN(image));

    REBINT index = cast(REBINT, VAL_IMAGE_POS(image));
    REBINT len = VAL_IMAGE_LEN_HEAD(image) - index;
    len = MAX(len, 0);

    Byte* src = VAL_IMAGE_AT(image);

    if (Is_Word(picker)) {
        switch (Cell_Word_Id(picker)) {
          case SYM_SIZE:
            if (not Is_Pair(poke) or Cell_Pair_X(poke) == 0)
                return PANIC(PARAM(VALUE));

            VAL_IMAGE_WIDTH(image) = Cell_Pair_X(poke);
            VAL_IMAGE_HEIGHT(image) = MIN(
                Cell_Pair_Y(poke),
                cast(REBINT,
                    Cell_Series_Len_Head(VAL_IMAGE_BIN(image))
                    / Cell_Pair_X(poke)
                )
            );
            break;

          case EXT_SYM_RGB:
            if (Is_Block(poke)) {
                Byte pixel[4];
                Set_Pixel_Tuple(pixel, poke);
                Fill_Line(src, pixel, len, true);
            }
            else if (Is_Integer(poke)) {
                REBINT byte = VAL_INT32(poke);
                if (byte < 0 or byte > 255)
                    panic (Error_Out_Of_Range(poke));

                Byte pixel[4];
                pixel[0] = byte; // red
                pixel[1] = byte; // green
                pixel[2] = byte; // blue
                pixel[3] = 0xFF; // opaque alpha
                Fill_Line(src, pixel, len, true);
            }
            else if (Is_Blob(poke)) {
                Size size;
                const Byte* data = Cell_Bytes_At(&size, poke);
                Bin_To_RGB(
                    src,
                    len,
                    data,
                    size / 3
                );
            }
            else
                return PANIC(PARAM(VALUE));
            break;

          case EXT_SYM_ALPHA:
            if (Is_Integer(poke)) {
                REBINT n = VAL_INT32(poke);
                if (n < 0 || n > 255)
                    return PANIC(Error_Out_Of_Range(poke));

                Fill_Alpha_Line(src, cast(Byte, n), len);
            }
            else if (Is_Blob(poke)) {
                Size size;
                const Byte* data = Cell_Bytes_At(&size, poke);
                Bin_To_Alpha(src, len, data, size);
            }
            else
                return PANIC(PARAM(VALUE));
            break;

          default:
            panic (picker);
        }
        return nullptr;
    }

    if (not Adjust_Image_Pick_Index_Is_Valid(&index, image, picker))
        panic (Error_Out_Of_Range(picker));

    if (Is_Block(poke)) { // set whole pixel
        Set_Pixel_Tuple(VAL_IMAGE_AT_HEAD(image, index), poke);
        return nullptr;
    }

    // set the alpha only

    REBINT alpha;
    if (
        Is_Integer(poke)
        and VAL_INT64(poke) > 0
        and VAL_INT64(poke) < 255
    ){
        alpha = VAL_INT32(poke);
    }
    else if (IS_CHAR(poke))
        alpha = Cell_Codepoint(poke);
    else
        return PANIC(Error_Out_Of_Range(poke));

    Byte* dp = VAL_IMAGE_AT_HEAD(image, index);
    dp[3] = alpha;

    return nullptr;
}


IMPLEMENT_GENERIC(HEAD_OF, Is_Image)
{
    INCLUDE_PARAMS_OF_TAIL_OF;

    Element* image = Element_ARG(ELEMENT);
    VAL_IMAGE_POS(image) = 0;
    return COPY(image);
}


IMPLEMENT_GENERIC(TAIL_OF, Is_Image)
{
    INCLUDE_PARAMS_OF_TAIL_OF;

    Element* image = Element_ARG(ELEMENT);
    VAL_IMAGE_POS(image) = VAL_IMAGE_LEN_HEAD(image);
    return COPY(image);
}


IMPLEMENT_GENERIC(HEAD_Q, Is_Image)
{
    INCLUDE_PARAMS_OF_HEAD_Q;

    Element* image = Element_ARG(ELEMENT);
    return Init_Logic(OUT, VAL_IMAGE_POS(image) == 0);
}


IMPLEMENT_GENERIC(TAIL_Q, Is_Image)
{
    INCLUDE_PARAMS_OF_TAIL_Q;

    Element* image = Element_ARG(ELEMENT);
    return Init_Logic(OUT, VAL_IMAGE_POS(image) >= VAL_IMAGE_LEN_HEAD(image));
}


//
//  export xy-of: native [
//
//  "Get current index into an IMAGE! value as a pair!"
//
//      return: [null? pair!]
//      image [<opt-out> image!]
//  ]
//
DECLARE_NATIVE(XY_OF)
{
    INCLUDE_PARAMS_OF_XY_OF;

    Element* image = Element_ARG(IMAGE);
    REBINT index = VAL_IMAGE_POS(image);
    return Init_Pair(
        OUT,
        index % VAL_IMAGE_WIDTH(image),
        index / VAL_IMAGE_WIDTH(image)
    );
}


IMPLEMENT_GENERIC(INDEX_OF, Is_Image)
{
    INCLUDE_PARAMS_OF_INDEX_OF;

    Element* image = Element_ARG(ELEMENT);
    return Init_Integer(OUT, VAL_IMAGE_POS(image) + 1);
}


IMPLEMENT_GENERIC(LENGTH_OF, Is_Image)
{
    INCLUDE_PARAMS_OF_LENGTH_OF;

    Element* image = Element_ARG(ELEMENT);
    REBINT index = VAL_IMAGE_POS(image);
    REBINT tail = VAL_IMAGE_LEN_HEAD(image);

    return Init_Integer(OUT, tail > index ? tail - index : 0);
}


// !!! The BINARY! currently has a position in it.  This notion of images
// being at an "index" is sketchy.  Assume that someone asking for the bytes
// doesn't care about the index.
//
IMPLEMENT_GENERIC(BYTES_OF, Is_Image)
{
    INCLUDE_PARAMS_OF_BYTES_OF;

    Element* image = Element_ARG(VALUE);

    const Binary* bin = Cell_Binary(VAL_IMAGE_BIN(image));
    return Init_Blob(OUT, bin);  // at 0 index
}


//
//  startup*: native [
//
//  "Startup IMAGE! Extension"
//
//      return: []
//  ]
//
DECLARE_NATIVE(STARTUP_P)
{
    INCLUDE_PARAMS_OF_STARTUP_P;

    return TRIPWIRE;
}


//
//  shutdown*: native [
//
//  "Shutdown IMAGE! Extension"
//
//      return: []
//  ]
//
DECLARE_NATIVE(SHUTDOWN_P)
{
    INCLUDE_PARAMS_OF_SHUTDOWN_P;

    return TRIPWIRE;
}
