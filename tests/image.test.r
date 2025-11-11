; %image.test.r
;
; !!! Originally IMAGE! was a built-in datatype.  It had many semantic
; problems of not acting like a series, and Ren-C tried to focus on the
; core language questions.

(image? make image! 100x100)
(not image? 1)
(image! = type of make image! 0x0)
; minimum
(image? #[image! [0x0 #{}]])

; image! same contents
(equal? a-value: #[image! [1x1 #{000000FF}]] a-value)
(equal? #[image! [1x1 #{000000FF}]] #[image! [1x1 #{000000FF}]])

; Literal offset not supported in R2.
(equal? #[image! [1x1 #{000000FF} 2]] #[image! [1x1 #{000000FF} 2]])
; Literal offset not supported in R2.
(not equal? #[image! [1x1 #{000000FF} 2]] #[image! [1x1 #{000000FF}]])

; !!! The IMAGE! data type is being moved to an extension, but NEXT is a
; specialization of SKIP and doesn't go through the GENERIC mechanism.  This
; can be revisited and addressed a number of ways, but IMAGE! is not a
; Beta/One feature.  However loading/encoding/decoding are kept working.
;

[(true comment [
    (
        a-value: #[image! [1x1 #{000000FF}]]
        not equal? a-value next a-value
    )
    (equal? #[image! [0x0 #{}]] next #[image! [1x1 #{000000FF}]])
    (equal? #[image! [1x0 #{}]] next #[image! [1x1 #{000000FF}]])
    (equal? #[image! [0x1 #{}]] next #[image! [1x1 #{000000FF}]])
    (not equal? #[image! [0x0 #{}]] next #[image! [1x1 #{000000FF}]])
    (not equal? #[image! [1x0 #{}]] next #[image! [1x1 #{000000FF}]])
    (not equal? #[image! [0x1 #{}]] next #[image! [1x1 #{000000FF}]])
])]

; No implicit to binary! from image!
(not equal? #{00} #[image! [1x1 #{000000FF}]])
; No implicit to binary! from image!
(not equal? #{00000000} #[image! [1x1 #{000000FF}]])
; No implicit to binary! from image!
(not equal? #{0000000000} #[image! [1x1 #{000000FF}]])
(equal? equal? #{00} #[image! [1x1 #{00000000}]] equal? #[image! [1x1 #{00000000}]] #{00})


(not same? #{00} #[image! [1x1 #{00000000}]])
; symmetry
(equal? same? #{00} #[image! [1x1 #{00000000}]] same? #[image! [1x1 #{00000000}]] #{00})

(not equal? #{00} #[image! [1x1 #{00000000}]])
; symmetry
(equal? equal? #{00} #[image! [1x1 #{00000000}]] equal? #[image! [1x1 #{00000000}]] #{00})

(
    a-value: make image! 0x0
    same? a-value all [a-value]
)

(
    a-value: make image! 0x0
    same? a-value all [true a-value]
)

(
    a-value: make image! 0x0
    true = all [a-value true]
)

(
    a-value: make image! 0x0
    same? a-value any [a-value]
)

(
    a-value: make image! 0x0
    same? a-value any [false a-value]
)

(
    a-value: make image! 0x0
    same? a-value any [a-value false]
)

(
    a-value: make image! 0x0
    same? a-value do reduce [a-value]
)

(if make image! 0x0 [okay])

(
    a-value: make image! 0x0
    same? a-value reeval a-value
)

(binary? encode 'bmp make image! 10x20)
[#2040
    (binary? encode 'png make image! 10x20)
]

(
    a-value: make image! 1x1
    0.0.0.255 = a-value.1
)
(
    a-value: make image! 0x0
    same? a-value a-value
)

(
    a-value: make image! 0x0
    f: does [a-value]
    same? a-value f
)

[#1706
    ((make image! [1x1 #{00000000}]) = not+ make image! [1x1 #{ffffffff}])
]
((make image! [1x1 #{ffffffff}]) = not+ make image! [1x1 #{00000000}])

(false = not make image! 0x0)
