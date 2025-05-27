(var {: group} (require :options/builder))

(group :id :gameplay 
       (require (.. _PACKAGE :/aim))
       (require (.. _PACKAGE :/3d_ballistics))
       (require (.. _PACKAGE :/first_person_death))
       (require (.. _PACKAGE :/monsters)))

