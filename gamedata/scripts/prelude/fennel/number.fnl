;; Numbers

(λ even? [number]
  ;; Returns true if NUMBER is even.
  (assert (number? number))
  (= 0 (% number 2)))

(λ odd? [number]
  ;; Returns true if NUMBER is odd.
  (assert (number? number))
  (= 1 (% number 2)))

{: even?
 : odd?}
