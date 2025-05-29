(var {: page
      : track
      : list-bool}
     (import :/options/builder))

(page :id :camera 
      (track :id :freelook_cam_limit
             :def 1.57
             :step 0.01)
      (list-bool :id :freelook_while_reloading))
