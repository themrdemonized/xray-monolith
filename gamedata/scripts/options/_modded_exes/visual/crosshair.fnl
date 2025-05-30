(var {: page
      : slide
      : check
      : input
      : track
      : color}
     (import :.../builder))

(λ concat [& rest]
  (accumulate [dest []
               _ tbl (ipairs rest)]
    (accumulate [d dest
                 _ v (ipairs tbl)]
      (do (table.insert d v)
          d))))

(λ crosshair-base-commands [args]
  [(slide :id args.prefix
          :text args.text)
   (check :id (.. "g_crosshair_" args.prefix)
          :hint :modded_exes_visual_crosshair_g_crosshair_show)
   (check :id (.. "g_crosshair_" args.prefix "_recon")
          :hint :modded_exes_visual_crosshair_g_crosshair_recon)
   (check :id (.. "g_crosshair_" args.prefix "_recon_max_opacity")
          :def (or args.defs.recon_max_opacity 0.5)
          :step 0.05
          :hint
          :modded_exes_visual_crosshair_g_crosshair_recon_max_opacity)
   (check :id (.. "g_crosshair_" args.prefix "_use_shader")
          :hint :modded_exes_visual_crosshair_g_crosshair_use_shader
          :def (or args.defs.use_shader false))
   (input :id (.. "g_crosshair_" args.prefix "_shader")
          :hint :modded_exes_visual_crosshair_g_crosshair_shader
          :def (or args.defs.shader "hud\\cursor"))
   (input :id (.. "g_crosshair_" args.prefix "_texture")
          :hint :modded_exes_visual_crosshair_g_crosshair_texture 
          :def (or args.defs.texture "ui\\cursor_dot"))
   (track :id (.. "g_crosshair_" args.prefix "_size")
          :def 1
          :step 1
          :hint :modded_exes_visual_crosshair_g_crosshair_size
          :def (or args.defs.size 1))
   (track :id (.. "g_crosshair_" args.prefix "_depth")
          :def 0
          :step 1
          :hint :modded_exes_visual_crosshair_g_crosshair_depth
          :def (or args.defs.depth 0))
   (color :id (.. "g_crosshair_" args.prefix "_color"))])

(λ crosshair-distance-commands [args]
  [(check :id (.. "g_crosshair_" args.prefix "_distance_lerp")
          :hint :modded_exes_visual_crosshair_g_crosshair_distance_lerp
          :def (or args.defs.distance_lerp false))
   (track :id (.. "g_crosshair_" args.prefix "_distance_lerp_rate")
          :def 40
          :step 1
          :hint :modded_exes_visual_crosshair_g_crosshair_distance_lerp_rate
          :def (or args.defs.distance_lerp_rate 40))])

(λ crosshair-opacity-commands [args]
  [(track :id (.. "g_crosshair_" args.prefix "_occluded_opacity")
          :def 0.6
          :step 0.05
          :hint :modded_exes_visual_crosshair_g_crosshair_occluded_opacity
          :def (or args.defs.occluded_opacity 0.5))
   (track :id (.. "g_crosshair_" args.prefix "_occlusion_fade_rate")
          :def 20
          :step 1
          :hint :modded_exes_visual_crosshair_g_crosshair_occlusion_fade_rate
          :def (or args.defs.occlusion_fade_rate 40))])

(λ crosshair-line-commands [args]
  (check :id (.. "g_crosshair_" args.prefix "_line")
         :hint :modded_exes_visual_crosshair_g_crosshair_line
         :def (or args.defs.line false)))

(λ crosshair-camera-far-commands [args]
  (crosshair-base-commands args))

(λ crosshair-camera-near-commands [args]
  (concat (crosshair-base-commands args)
          (crosshair-distance-commands args)))

(λ crosshair-far-commands [args]
  (concat (crosshair-base-commands args)
          (crosshair-line-commands args)))

(λ crosshair-near-commands [args]
  (concat (crosshair-base-commands args)
          (crosshair-distance-commands args)
          (crosshair-opacity-commands args)
          (crosshair-line-commands args)))

(λ crosshair-pair-commands [args]
  (concat (slide :id args.prefix)
          (crosshair-far-commands
           {:prefix (.. args.prefix "_far")
            :text :ui_mm_far_modded_exes
            :defs args.defs.far})
          (crosshair-near-commands
           {:prefix (.. args.prefix "_near")
            :text :ui_mm_near_modded_exes
            :defs args.defs.near})))

(page
 (unpack
  (concat
   [:id :crosshair 
    (check :id :g_crosshair_show_always)
    (check :id :g_crosshair_show_independent)]
   [(slide :id :camera)]
   (crosshair-camera-far-commands
    {:prefix :camera_far
     :text :ui_mm_far_modded_exes 
     :defs {:show true
            :use_shader false
            :recon true

	    :shader "hud\\cursor" 
	    :texture "ui\\cursor_dot" 

	    :size 1
	    :depth 25}})
   (crosshair-camera-near-commands
    {:prefix :camera_near
     :text :ui_mm_near_modded_exes
     :defs {:show false
            :use_shader true
            :distance_lerp false
            :recon false

	    :shader "hud\\cursor" 
	    :texture "ui\\cursor_cross" 

	    :distance_lerp_rate 40

	    :size 16
	    :depth 0

	    :recon_max_opacity 0.5}})
   (crosshair-pair-commands
    {:prefix :weapon
     :defs {:far {:show false
                  :use_shader true
                  :recon false
                  :line false

	          :shader "hud\\cursor"
	          :texture "ui\\cursor_plus"

	          :distance_lerp_rate 40

	          :size 4
	          :depth 25}

            :near {:show false
                   :use_shader true
                   :distance_lerp false
                   :recon true
                   :line false

	           :shader "hud\\cursor" 
	           :texture "ui\\cursor_cross" 

	           :distance_lerp_rate 40

	           :size 16
	           :depth 0

	           :occluded_opacity 0.25
	           :occlusion_fade_rate 40
                   :recon_max_opacity 0.5}}})
   (crosshair-pair-commands
    {:prefix :device
     :defs {:far {:show false
                  :use_shader true
                  :recon false
                  :line false

	          :shader "hud\\cursor"
	          :texture "ui\\cursor_plus"

	          :distance_lerp_rate 40

	          :size 4
	          :depth 25}

            :near {:show false
                   :use_shader true
                   :distance_lerp false
                   :recon true
                   :line false

		   :shader "hud\\cursor"
		   :texture "ui\\cursor_cross"

		   :distance_lerp_rate 40

		   :size 16
		   :depth 0

		   :occluded_opacity 0.25
		   :occlusion_fade_rate 40
		   :recon_max_opacity 0.5}}}))))
