Rebol [
    name: Image
    notes: "See %extensions/README.md for the format and fields of this file"

    extended-words: [rgb alpha]

    extended-types: [image!]
]

use-librebol: 'no  ; fiddles with Stubs/Nodes

sources: %mod-image.c
