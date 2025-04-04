REBOL [
    Name: Image
    Notes: "See %extensions/README.md for the format and fields of this file"

    Extended-Words: [rgb alpha]

    Extended-Types: [image!]
]

use-librebol: 'no  ; fiddles with Stubs/Nodes

sources: %mod-image.c
