;;; Smart constructor API for options menu data tables

(var {:fennel {:type {: nil?
                      : number?
                      : string?
                      : table?}

               :plist {: plist->table
                       : plist->header+list
                       : table->plist}}}
     (require :prelude))

;;; Controls

(λ line [& plist]
  "Divider line."
  (var table (plist->table plist))
  (set table.id (or table.id :divider))
  (set table.type :line)
  table)

(λ slide [& plist]
  "Image with text."
  (var table (plist->table plist))
  (var id table.id)
  (assert (string? id) (.. "Slide ID is not a string" (tostring id)) )
  (set table.id (.. "slide_modded_exes_" id))
  (set table.type :slide)
  (set table.link (or table.link :ui_options_slider_other))
  (set table.text (or table.text (.. "ui_mm_" id "_modded_exes")))
  (set table.size (or table.size [512 50]))
  table)

(λ button [& plist]
  "Button."
  (var table (plist->table plist))
  (set table.type :button)
  (set table.functor_ui [table.functor_ui])
  (set table.precondition [table.precondition])
  table)

(λ check [& plist]
  "Boolean checkbox."
  (var table (plist->table plist))
  (set table.type :check)
  (set table.val 1)
  (set table.def (or table.def false))
  (set table.cmd (or table.cmd table.id))
  table)

(λ track [& plist]
  "Numeric slider."
  (var table (plist->table plist))
  
  ;; Populate typing information
  (set table.type :track)
  (set table.val 2)

  ;; Use ID as command unless specified explicitly
  (if (nil? table.cmd)
      (set table.cmd table.id))

  ;; If a valid command is specified...
  (if (string? table.cmd)
      ;; Get bounds of console variable
      (do (var bounds (: (get_console) :get_variable_bounds table.cmd))
          (if (and bounds.min bounds.max)
              (do (set table.min (or table.min bounds.min))
                  (set table.max (or table.max bounds.max)))))
      ;; Otherwise nil it to ensure data sanity
      (set table.cmd nil))
  
  ;; Ensure provided range is valid
  (if (and (number? table.min)
           (number? table.max)
           (> table.min table.max))
      (let [min table.max
            max table.min]
        (set table.min min)
        (set table.max max)))

  table)

(λ color [& plist]
  "ARGB color sliders."
  (var table (plist->table plist))
  
  (λ curr-color [id n]
    (var curr (tostring (get_console_cmd nil id)))
    (var (r g b a) (string.match curr "(%d+) (%d+) (%d+) (%d)+"))
    (if n
        (tonumber
         (case n
           :r r
           :g g
           :b b
           :a a))
        255))

  (λ func-color [id n]
    (var v (. ui_options.opt_temp
              (.. "modded_exes/visual/crosshair/" id "_" n)))

    (if v
        (do (var curr (tostring (get_console_cmd nil id)))
            (var (r g b a) (string.match curr "(%d+) (%d+) (%d+) (%d)+"))
            (var out (case n
                       :r (v g b a)
                       :g (r v b a)
                       :b (r g v a)
                       :a (r g b v)
                       _ (r g b a)))
            (exec_console_cmd
             (.. id
                 "("
                 (tostring r) ""
                 (tostring g) ""
                 (tostring b) ""
                 (tostring a)
                 ")")))))

  (values
   (track :id (.. table.id "_a")
          :min 0
          :max 255
          :def 255
          :step 1
          :curr [curr-color table.id :a]
          :functor [func-color table.id :a]
          :cmd false )
   
   (track :id (.. table.id "_r")
          :min 0
          :max 255
          :def 255
          :step 1
          :curr [curr-color table.id :r]
          :functor [func-color table.id :r]
          :cmd false )
   
   (track :id (.. table.id "_g")
          :min 0
          :max 255
          :def 255
          :step 1
          :curr [curr-color table.id :g]
          :functor [func-color table.id :g]
          :cmd false )
   
   (track :id (.. table.id "_b")
          :min 0
          :max 255
          :def 255
          :step 1
          :curr [curr-color table.id :b]
          :functor [func-color table.id :b]
          :cmd false )))

(λ input [& plist]
  "String input."
  (var table (plist->table plist))
  (set table.type :input)
  (set table.val 0)
  (set table.cmd (or table.cmd table.id))
  table)

(λ list [& plist]
  "Multiple-choice key-value list."
  (var table (plist->table plist))
  (set table.type :list)
  (set table.curr (or (and table.curr [table.curr])
                       [#(get_console_cmd 0 table.id)]))
  (var content table.content)
  (set table.content [#content])
  (set table.cmd (or table.cmd table.id))
  (set table.restart (or table.restart false))
  table)

(λ list-enum [& plist]
  "Integer-indexed key list."
  (var table (plist->table plist))
  
  (set table.val 0)
  
  (var content table.content)
  (assert (table? content)
          (.. "Enum List content is not a table: " (tostring content)))
  (set table.content [])
  
  (each [i c (ipairs content)]
    (set (. table.content i) [(tostring (- i 1)) c]))
  
  (list (unpack (table->plist table))))

(λ list-bool [& plist]
  "Binary choice list."
  (var table (plist->table plist))
  
  (set table.val 0)
  (set table.content [[:1 :ON]
                      [:0 :OFF]])
  
  (list (unpack (table->plist table))))

(λ group [& plist]
  "Page group.
   Takes a list; expects the first N elements to be symbol-value property pairs,
   and subsequent elements to be child controls."

  ;; Extract the plist header from our input
  (var (header body) (plist->header+list plist))

  ;; Emplace the remaining elements in the header's group field
  (set header.gr body)
  
  header)

(λ page [& plist]
  "Page.
   Takes a list; expects the first N elements to be symbol-value property pairs,
   and subsequent elements to be child controls."
  
  ;; A page is a group
  (var page (group (unpack plist)))
  
  ;; But with an implicit show parameter
  (when (nil? page.sh)
    (set page.sh true))
  
  ;; And a built-in header slide
  (table.insert page.gr 1 (slide :id page.id))
  
  page)

{: line
 : slide
 : button
 : check
 : track
 : color
 : input
 : list
 : list-enum
 : list-bool
 : page
 : group}
