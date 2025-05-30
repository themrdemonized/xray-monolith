(var {: page
      : list-bool
      : track}
     (import :.../builder))

(page :id :crash_saves 
      (list-bool :id :crash_save)
      (track :id :crash_save_count
             :def 10
             :step 1))
