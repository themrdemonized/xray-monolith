(var {: page
      : track
      : list-bool}
     (require :options/builder))

(page :id :mouse 
      (track :id :mouse_sens_vertical
             :def 1
             :step 0.01)
      (list-bool :id :mouse_wheel_change_weapon))
