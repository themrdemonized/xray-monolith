(λ begins-with? [head str]
  "Return `true` if `str` begins with `head`."
  (str:match (.. "^" head)))

(λ ends-with? [tail str]
  "Return `true` if `str` ends with `tail`."
  (str:match (.. tail "$")))

{: begins-with?
 : ends-with?}
