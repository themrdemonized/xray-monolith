(var {: page
      : check
      : list-enum}
     (import :/options/builder))

(page :id :3d_ballistics 
      (check :id :g_firepos)
      (check :id :g_firepos_zoom)
      (check :id :g_aimpos)
      (check :id :g_aimpos_zoom)
      (check :id :g_firedir_third_person)
      (list-enum :id :g_nearwall
                 :content [:OFF
                           :nearwall_hud_fov
                           :nearwall_position])
      (list-enum :id :g_nearwall_trace
                 :content [:nearwall_camera
                           :nearwall_item]))
