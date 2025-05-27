(var {: page
      : track}
     (require :options/builder))

(page :id :wallmarks 
      (track :id :g_wallmark_range_static
             :def 100
             :step 10)
      (track :id :g_wallmark_range_skeleton
             :def 50
             :step 10))
