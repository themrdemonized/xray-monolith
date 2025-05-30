(var {: page
      : list-bool}
     (import :.../builder))

(page :id :monsters 
      (list-bool :id :heat_vision_zombie_cold)
      (list-bool :id :pseudogiant_can_damage_objects_on_stomp)
      (list-bool :id :telekinetic_objects_include_corpses)
      (list-bool :id :monster_stuck_fix
                 :restart true))

