(var {: iter-zip}
     (require :prelude/fennel/iterator))

(var PATTERN-FILE-PATH "^(.-)([^\\/]-)%.([^\\/%.]-)%.?$")

(λ strip-extension [s]
  "Remove any file extension from path `s`."
  (var (path name _) (s:match PATTERN-FILE-PATH))
  (.. path name))

(λ resolve-relative-path [path]
  "Collapse `.+` segments in the given unix-style `path`."

  ;; Split our path string into a list of segments
  (var head (icollect [seg (path:gmatch "[^/]+")]
              seg))
  
  ;; Allocate our output list
  (var tail [])
  
  ;; Loop until `head` is empty
  (while (> (length head) 0)
    ;; Pop a value off the end of `head`
    (var val (table.remove head))

    ;; Test for non-dot characters
    (if (val:match "[^.]")
        ;; If it contains any, push onto the front of `tail`
        (table.insert tail 1 val)
        ;; Otherwise, loop until we run out of dots
        (while (> (length val) 0)
          ;; Ensure head is not empty
          (when (= 0 (length head))
            (error (.. "Relative path cannot escape scripts root:\n"
                       path)))
          ;; Drop a dot
          (set val (val:sub 2))
          ;; Remove an element from the end of `head`
          (table.remove head))))

  ;; Recombine `tail` into a string and return
  (table.concat tail "/"))

(λ path-includes? [path]
  (λ impl [subj]
    "Return true if unix-style path `path` includes `subj`."

    ;; Resolve relative path segments
    (var abs-path (resolve-relative-path path))
    
    ;; Split both the path and subject into segment lists
    (var path-segs [(abs-path:gmatch "[^/]+")])
    (var subj-segs [(subj:gmatch "[^/]+")])

    ;; Zip the lists into an iterator of pairs and fold a bool over it
    (accumulate [equal true
                 [va vb] (iter-zip path-segs subj-segs)
                 &until (not equal)]
      (and equal    ;; All previous segments must match
           va vb    ;; If either result is nil, length mismatch
           (or (and (va:match "%*") ;; If va is a convert to pattern and match
                    (vb:match (va:gsub "%*" "%.%*")))
               (= (va:lower) (vb:lower))))))) ;; Case-insensitive comparison

{: PATTERN-FILE-PATH
 : strip-extension
 : path-includes?}
