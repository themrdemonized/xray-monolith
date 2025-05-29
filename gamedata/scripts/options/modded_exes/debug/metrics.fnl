(var {: page
      : track}
     (import :.../builder))

(page :id :metrics 
      (track :id :lua_gcstep
             :def 400
             :step 10)
      (track :id :mouse_buffer_size
             :def 1024
             :step 32)
      (track :id :keyboard_buffer_size
             :def 128
             :step 32))

