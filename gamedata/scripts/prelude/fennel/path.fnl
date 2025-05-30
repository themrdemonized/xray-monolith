(var {: list-values}
     (require :prelude/fennel/list))

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
    
    ;; Split the subject into a segment iterator
    (var subj-segs [(subj:gmatch "[^/]+")])
    
    ;; Split the path into a segment list
    (var path-list (icollect [seg (abs-path:gmatch "[^/]+")]
                     seg))

    ;; Test the last segment for multi-glob
    (var count (length path-list))
    (var tail (. path-list count))
    (var multi (tail:match "^%*%*+$"))
    
    ;; Convert the path list into an iterator
    (var path-segs [(list-values path-list)])

    ;; Zip the path and subject iterators and fold a bool over them
    (accumulate [equal true
                 [va vb] (iter-zip path-segs subj-segs)
                 &until (not equal)]
      (case (values va vb)
        ;; If both segments are valid, lowercase both,
        ;; convert va into a pattern, and match against it
        (a b) (do (var va (va:lower))
                  (var vb (vb:lower))
                  (set va (va:gsub "%*" "%.%*"))
                  (vb:match va))
        ;; If vb is nil, length mismatch
        (a nil) false
        ;; If va is nil, we either have a length mismatch or are a multi-glob
        (nil _) multi))))

{: PATTERN-FILE-PATH
 : strip-extension
 : path-includes?}
